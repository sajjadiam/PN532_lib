/**
 * @file pn532.c
 * @brief Cooperative PN532 command/ACK/response state machine.
 *
 * @details PN532 UM0701-02, sections 6.2.1-6.2.3. Transport I/O is
 * bounded and nonblocking. No retries, heap allocation, or ISR callbacks.
 */
#include "pn532.h"

#include <string.h>

#define PN532_CORE_MAX_TIMEOUT UINT32_C(0x7FFFFFFF)

#define PN532_CORE_UNINITIALIZED 0U
#define PN532_CORE_IDLE          1U
#define PN532_CORE_ARM_ACK       2U
#define PN532_CORE_START_TX      3U
#define PN532_CORE_WAIT_TX       4U
#define PN532_CORE_WAIT_ACK      5U
#define PN532_CORE_ARM_RESPONSE  6U
#define PN532_CORE_WAIT_RESPONSE 7U
#define PN532_CORE_ABORT_TX      8U
#define PN532_CORE_NOTIFY        9U

/** @brief Return a per-instance transport operation table. */
static const pn532_transport_ops_t *core_ops(const pn532_t *device)
{
    return device->config.transport.ops;
}

/** @brief Return the private transport context. */
static void *core_context(const pn532_t *device)
{
    return device->config.transport.context;
}

/** @brief Read wrapping milliseconds in task context. */
static uint32_t core_now(const pn532_t *device)
{
    return device->config.now_ms(device->config.clock_context);
}

/** @brief Test a deadline using unsigned modulo-2^32 elapsed time. */
static bool core_expired(uint32_t now, uint32_t started, uint32_t timeout)
{
    return (uint32_t)(now - started) >= timeout;
}

/** @brief The instance is only usable after successful init. */
static bool core_ready(const pn532_t *device)
{
    return device != NULL &&
           device->state >= PN532_CORE_IDLE &&
           device->state <= PN532_CORE_NOTIFY &&
           device->config.transport.ops != NULL &&
           device->config.now_ms != NULL;
}

/**
 * @brief Stage a single eventual callback.
 *
 * @param abort_transport Clear an accepted RX request and any stale bytes.
 * @param uncertain A command may have run or stream framing may be lost.
 *
 * Never call this while a TX buffer might still be in use. In that case
 * transition to PN532_CORE_ABORT_TX and await a terminal TX poll first.
 */
static void core_finish(pn532_t *device,
                        pn532_result_t result,
                        pn532_status_t transport_status,
                        bool abort_transport,
                        bool uncertain)
{
    if (abort_transport) {
        (void)core_ops(device)->abort(core_context(device));
    }
    device->terminal_result = result;
    device->terminal_transport_status = transport_status;
    device->recovery_required = device->recovery_required || uncertain;
    device->cancel_pending = false;
    device->state = PN532_CORE_NOTIFY;
}

/** @brief Request nonblocking TX cancellation without releasing the buffer. */
static void core_abort_tx(pn532_t *device,
                          pn532_result_t reason,
                          pn532_status_t status)
{
    device->terminal_result = reason;
    device->terminal_transport_status = status;
    device->recovery_required = true;
    (void)core_ops(device)->abort(core_context(device));
    device->state = PN532_CORE_ABORT_TX;
}

/** @brief Decode the one frame currently owned by the RX descriptor. */
static pn532_frame_result_t core_decode(const pn532_t *device,
                                        pn532_frame_view_t *view)
{
    const pn532_frame_span_t frame = {
        device->rx.data, device->rx.length
    };
    return pn532_frame_decode(&frame, view);
}

/** @brief Deliver the terminal event only from process() task context. */
static void core_notify(pn532_t *device)
{
    pn532_completion_t completion = {
        device->terminal_result,
        device->terminal_transport_status,
        NULL,
        0U
    };
    pn532_complete_fn callback = device->on_complete;
    void *user = device->user;

    if (completion.result == PN532_RESULT_OK) {
        pn532_frame_view_t view;
        if (core_decode(device, &view) != PN532_FRAME_OK ||
            view.length == 0U || view.data == NULL) {
            completion.result = PN532_RESULT_FRAME_ERROR;
            device->recovery_required = true;
        } else if (view.length > 1U) {
            completion.response = view.data + 1U;
            completion.response_length = (uint16_t)(view.length - 1U);
        }
    }

    /*
     * Keep NOTIFY until callback returns so recursive submit returns BUSY.
     * Response storage stays intact until the next accepted submit.
     */
    if (callback != NULL) {
        callback(user, &completion);
    }
    device->on_complete = NULL;
    device->user = NULL;
    device->state = PN532_CORE_IDLE;
}

/** @brief Validate the entire configuration before touching the instance. */
static bool core_valid_config(const pn532_config_t *config)
{
    const pn532_transport_ops_t *ops;

    if (config == NULL || config->transport.ops == NULL ||
        config->now_ms == NULL ||
        config->tx_storage.data == NULL ||
        config->rx_storage.data == NULL ||
        config->tx_storage.data == config->rx_storage.data ||
        config->tx_storage.capacity < 9U ||
        config->rx_storage.capacity < 9U ||
        config->tx_timeout_ms == 0U ||
        config->tx_timeout_ms > PN532_CORE_MAX_TIMEOUT ||
        config->ack_timeout_ms == 0U ||
        config->ack_timeout_ms > PN532_CORE_MAX_TIMEOUT) {
        return false;
    }
    ops = config->transport.ops;
    return ops->start_tx != NULL && ops->poll_tx != NULL &&
           ops->start_rx != NULL && ops->poll_rx != NULL &&
           ops->abort != NULL;
}

pn532_status_t pn532_init(pn532_t *device, const pn532_config_t *config)
{
    if (device == NULL || !core_valid_config(config)) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }
    memset(device, 0, sizeof(*device));
    device->config = *config;
    device->rx.data = config->rx_storage.data;
    device->rx.capacity = config->rx_storage.capacity;
    /*
     * RX is armed before TX. Disable the transport's short receive timer:
     * Core independently applies ACK and response deadlines at their
     * protocol-defined phase boundaries.
     */
    device->rx.timeout_ms = PN532_CORE_MAX_TIMEOUT;
    device->state = PN532_CORE_IDLE;
    return PN532_STATUS_OK;
}

pn532_status_t pn532_submit(pn532_t *device,
                            const pn532_command_t *command)
{
    pn532_frame_message_t message;
    pn532_frame_buffer_t output;
    pn532_frame_result_t frame_status;

    if (!core_ready(device)) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }
    if (device->state != PN532_CORE_IDLE) {
        return PN532_STATUS_BUSY;
    }
    if (device->recovery_required) {
        return PN532_STATUS_IO_ERROR;
    }
    if (command == NULL || command->data == NULL ||
        command->length == 0U ||
        command->length > PN532_FRAME_MAX_PAYLOAD_SIZE ||
        command->data[0] == UINT8_MAX ||
        command->on_complete == NULL ||
        command->response_timeout_ms == 0U ||
        command->response_timeout_ms > PN532_CORE_MAX_TIMEOUT) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }

    message.tfi = PN532_FRAME_TFI_HOST;
    message.data = command->data;
    message.length = command->length;
    output.data = device->config.tx_storage.data;
    output.capacity = device->config.tx_storage.capacity;
    output.length = 0U;
    frame_status = pn532_frame_encode(&message, &output);
    if (frame_status == PN532_FRAME_BUFFER_TOO_SMALL) {
        return PN532_STATUS_BUFFER_TOO_SMALL;
    }
    if (frame_status != PN532_FRAME_OK) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }

    device->tx.data = output.data;
    device->tx.length = output.length;
    device->tx.timeout_ms = device->config.tx_timeout_ms;
    device->rx.length = 0U;
    device->rx.timeout_ms = PN532_CORE_MAX_TIMEOUT;
    device->command_code = command->data[0];
    device->response_timeout_ms = command->response_timeout_ms;
    device->on_complete = command->on_complete;
    device->user = command->user;
    device->cancel_pending = false;
    device->terminal_transport_status = PN532_STATUS_OK;
    device->terminal_result = PN532_RESULT_OK;
    device->state = PN532_CORE_ARM_ACK;
    return PN532_STATUS_OK;
}

/** @brief An ACK must be received only after physical TX completes. */
static void core_wait_tx(pn532_t *device)
{
    const pn532_async_result_t result =
        core_ops(device)->poll_tx(core_context(device));

    if (result.state == PN532_ASYNC_DONE) {
        device->phase_started_ms = core_now(device);
        device->state = PN532_CORE_WAIT_ACK;
    } else if (result.state == PN532_ASYNC_FAILED) {
        core_finish(device,
                    result.status == PN532_STATUS_TIMEOUT
                        ? PN532_RESULT_TX_TIMEOUT
                        : PN532_RESULT_TRANSPORT_ERROR,
                    result.status, true, true);
    } else if (result.state != PN532_ASYNC_BUSY) {
        core_abort_tx(device, PN532_RESULT_TRANSPORT_ERROR,
                      PN532_STATUS_IO_ERROR);
    } else if (core_expired(core_now(device),
                            device->phase_started_ms,
                            device->tx.timeout_ms)) {
        core_abort_tx(device, PN532_RESULT_TX_TIMEOUT,
                      PN532_STATUS_TIMEOUT);
    }
}

/** @brief Inspect the ACK-stage frame once RX reports completion. */
static void core_ack_received(pn532_t *device)
{
    pn532_frame_view_t view;

    if (core_decode(device, &view) != PN532_FRAME_OK) {
        core_finish(device, PN532_RESULT_FRAME_ERROR,
                    PN532_STATUS_OK, true, true);
    } else if (view.type == PN532_FRAME_TYPE_ACK) {
        device->phase_started_ms = core_now(device);
        device->state = PN532_CORE_ARM_RESPONSE;
    } else if (view.type == PN532_FRAME_TYPE_NACK) {
        core_finish(device, PN532_RESULT_NACK,
                    PN532_STATUS_OK, false, false);
    } else if (view.type == PN532_FRAME_TYPE_ERROR) {
        core_finish(device, PN532_RESULT_DEVICE_ERROR,
                    PN532_STATUS_OK, false, false);
    } else {
        core_finish(device, PN532_RESULT_UNEXPECTED_RESPONSE,
                    PN532_STATUS_OK, true, true);
    }
}

/** @brief Inspect a checked PN532 information response. */
static void core_response_received(pn532_t *device)
{
    pn532_frame_view_t view;

    if (core_decode(device, &view) != PN532_FRAME_OK) {
        core_finish(device, PN532_RESULT_FRAME_ERROR,
                    PN532_STATUS_OK, true, true);
    } else if (view.type == PN532_FRAME_TYPE_ERROR) {
        core_finish(device, PN532_RESULT_DEVICE_ERROR,
                    PN532_STATUS_OK, false, false);
    } else if ((view.type != PN532_FRAME_TYPE_NORMAL &&
                view.type != PN532_FRAME_TYPE_EXTENDED) ||
               view.tfi != PN532_FRAME_TFI_DEVICE ||
               view.length < 1U ||
               view.data[0] != (uint8_t)(device->command_code + 1U)) {
        core_finish(device, PN532_RESULT_UNEXPECTED_RESPONSE,
                    PN532_STATUS_OK, true, true);
    } else {
        core_finish(device, PN532_RESULT_OK,
                    PN532_STATUS_OK, false, false);
    }
}

/** @brief Interpret terminal and nonterminal RX results for either phase. */
static void core_wait_rx(pn532_t *device, bool ack_phase)
{
    pn532_async_result_t result;
    uint32_t timeout = ack_phase
        ? device->config.ack_timeout_ms
        : device->response_timeout_ms;

    if (core_expired(core_now(device), device->phase_started_ms, timeout)) {
        core_finish(device, ack_phase ? PN532_RESULT_ACK_TIMEOUT
                                      : PN532_RESULT_RESPONSE_TIMEOUT,
                    PN532_STATUS_TIMEOUT, true, true);
        return;
    }

    result = core_ops(device)->poll_rx(core_context(device));
    if (result.state == PN532_ASYNC_DONE) {
        if (ack_phase) {
            core_ack_received(device);
        } else {
            core_response_received(device);
        }
    } else if (result.state == PN532_ASYNC_FAILED) {
        core_finish(device,
                    result.status == PN532_STATUS_FRAME_ERROR
                        ? PN532_RESULT_FRAME_ERROR
                        : PN532_RESULT_TRANSPORT_ERROR,
                    result.status, true, true);
    } else if (result.state != PN532_ASYNC_BUSY) {
        core_finish(device, PN532_RESULT_TRANSPORT_ERROR,
                    PN532_STATUS_IO_ERROR, true, true);
    }
}

void pn532_process(pn532_t *device)
{
    pn532_status_t status;

    if (!core_ready(device) || device->state == PN532_CORE_IDLE) {
        return;
    }

    if (device->cancel_pending && device->state != PN532_CORE_ABORT_TX &&
        device->state != PN532_CORE_NOTIFY) {
        if (device->state == PN532_CORE_WAIT_TX) {
            core_abort_tx(device, PN532_RESULT_CANCELLED,
                          PN532_STATUS_OK);
        } else {
            bool sent = device->state != PN532_CORE_ARM_ACK &&
                        device->state != PN532_CORE_START_TX;
            core_finish(device, PN532_RESULT_CANCELLED,
                        PN532_STATUS_OK, true, sent);
        }
        return;
    }

    switch (device->state) {
    case PN532_CORE_ARM_ACK:
        status = core_ops(device)->start_rx(core_context(device),
                                             &device->rx);
        if (status == PN532_STATUS_OK) {
            device->state = PN532_CORE_START_TX;
        } else {
            core_finish(device, PN532_RESULT_TRANSPORT_ERROR,
                        status, false, true);
        }
        break;

    case PN532_CORE_START_TX:
        status = core_ops(device)->start_tx(core_context(device),
                                             &device->tx);
        if (status == PN532_STATUS_OK) {
            device->phase_started_ms = core_now(device);
            device->state = PN532_CORE_WAIT_TX;
        } else {
            core_finish(device, PN532_RESULT_TRANSPORT_ERROR,
                        status, true, true);
        }
        break;

    case PN532_CORE_WAIT_TX:
        core_wait_tx(device);
        break;

    case PN532_CORE_WAIT_ACK:
        core_wait_rx(device, true);
        break;

    case PN532_CORE_ARM_RESPONSE:
        status = core_ops(device)->start_rx(core_context(device),
                                             &device->rx);
        if (status == PN532_STATUS_OK) {
            device->state = PN532_CORE_WAIT_RESPONSE;
        } else {
            core_finish(device, PN532_RESULT_TRANSPORT_ERROR,
                        status, true, true);
        }
        break;

    case PN532_CORE_WAIT_RESPONSE:
        core_wait_rx(device, false);
        break;

    case PN532_CORE_ABORT_TX:
        {
            const pn532_async_result_t result =
                core_ops(device)->poll_tx(core_context(device));
            if (result.state == PN532_ASYNC_DONE ||
                result.state == PN532_ASYNC_FAILED) {
                core_finish(device, device->terminal_result,
                            device->terminal_transport_status, true, true);
            }
        }
        break;

    case PN532_CORE_NOTIFY:
        core_notify(device);
        break;

    default:
        /* An unreachable/invalid state should never attempt hardware I/O. */
        break;
    }
}

pn532_status_t pn532_cancel(pn532_t *device)
{
    if (!core_ready(device)) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }
    if (device->state != PN532_CORE_IDLE &&
        device->state != PN532_CORE_NOTIFY) {
        device->cancel_pending = true;
    }
    return PN532_STATUS_OK;
}
