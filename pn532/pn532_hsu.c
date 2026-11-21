/**
 * @file pn532_hsu.c
 * @brief Cooperative, bounded PN532 HSU transport.
 */
#include "pn532_hsu.h"
#include "pn532_rx.h"
#include <string.h>

#define PN532_HSU_MAX_TIMEOUT UINT32_C(0x7FFFFFFF)

/** @brief Create an operation-state snapshot. */
static pn532_async_result_t hsu_result(pn532_async_state_t state, pn532_status_t status) {
    pn532_async_result_t result = {
        state, 
        status
    };
    return result;
}

/** @brief Test elapsed time across uint32_t tick rollover. */
static bool hsu_expired(uint32_t now, uint32_t started, uint32_t period) {
    return (uint32_t)(now - started) >= period;
}

/** @brief Validate instance and clock without touching hardware. */
static bool hsu_ready(const pn532_hsu_t *device) {
    return device != NULL && device->io != NULL && device->io->now_ms != NULL;
}

/** @brief Record a failed RX operation and request a nonblocking flush. */
static pn532_async_result_t hsu_rx_fail(pn532_hsu_t *device, pn532_status_t error) {
    device->rx->length = 0U;
    device->rx_result = hsu_result(PN532_ASYNC_FAILED, error);
    (void)device->io->rx_flush(device->context);
    return device->rx_result;
}

/** @brief Accept one caller-owned TX buffer for asynchronous transmission. */
static pn532_status_t hsu_start_tx(void *context, const pn532_tx_frame_t *frame) {
    pn532_hsu_t *device = (pn532_hsu_t *)context;

    if (!hsu_ready(device) || !frame || !frame->data || frame->length < PN532_FRAME_CONTROL_SIZE ||
        frame->length > PN532_FRAME_MAX_SIZE || frame->timeout_ms == 0U || frame->timeout_ms > PN532_HSU_MAX_TIMEOUT)
    {
        return PN532_STATUS_INVALID_ARGUMENT;
    }
    if (device->tx_result.state == PN532_ASYNC_BUSY) return PN532_STATUS_BUSY;

    pn532_status_t result = device->io->tx_start(device->context, frame->data, frame->length);
    if (result != PN532_STATUS_OK) return result;
    device->tx = *frame;
    device->tx_started = device->io->now_ms(device->context);
    device->tx_abort_pending = false;
    device->tx_abort_reason = PN532_STATUS_OK;
    device->tx_result = hsu_result(PN532_ASYNC_BUSY, PN532_STATUS_OK);
    return PN532_STATUS_OK;
}

/** @brief Request TX abort while preserving ownership until terminal poll. */
static void hsu_request_tx_abort(pn532_hsu_t *device, pn532_status_t reason) {
    if (device->tx_result.state != PN532_ASYNC_BUSY || device->tx_abort_pending) return;
    device->tx_abort_pending = true;
    device->tx_abort_reason = reason;
    (void)device->io->tx_abort(device->context);
}

/** @brief Observe TX completion; never waits for DMA or UART TC. */
static pn532_async_result_t hsu_poll_tx(void *context) {
    pn532_hsu_t *device = (pn532_hsu_t *)context;
    pn532_async_result_t status;

    if (!hsu_ready(device)) return hsu_result(PN532_ASYNC_FAILED, PN532_STATUS_INVALID_ARGUMENT);
    if (device->tx_result.state != PN532_ASYNC_BUSY) return device->tx_result;
    status = device->io->tx_poll(device->context);
    if (status.state == PN532_ASYNC_DONE || status.state == PN532_ASYNC_FAILED) {
        device->tx_result = device->tx_abort_pending
            ? hsu_result(PN532_ASYNC_FAILED, device->tx_abort_reason) : status;
        return device->tx_result;
    }
    if (status.state != PN532_ASYNC_BUSY) hsu_request_tx_abort(device, PN532_STATUS_IO_ERROR);
    if (hsu_expired(device->io->now_ms(device->context), device->tx_started, device->tx.timeout_ms)) {
        hsu_request_tx_abort(device, PN532_STATUS_TIMEOUT);
    }
    return device->tx_result;
}

/** @brief Arm a receive frame buffer before sending a command. */
static pn532_status_t hsu_start_rx(void *context, pn532_rx_frame_t *frame) {
    pn532_hsu_t *device = (pn532_hsu_t *)context;

    if (!hsu_ready(device) || !frame || !frame->data || frame->capacity < PN532_FRAME_CONTROL_SIZE ||
        frame->timeout_ms == 0U || frame->timeout_ms > PN532_HSU_MAX_TIMEOUT) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }
    if (device->rx_result.state == PN532_ASYNC_BUSY) return PN532_STATUS_BUSY;
    /* Do not flush on each receive: ACK and Response can be queued back-to-back. */
    frame->length = 0U;
    device->rx = frame;
    device->rx_used = 0U;
    device->rx_expected = 0U;
    device->sync_a = 0xFFU;
    device->sync_b = 0xFFU;
    device->sync_found = false;
    device->rx_started = device->io->now_ms(device->context);
    device->rx_result = hsu_result(PN532_ASYNC_BUSY, PN532_STATUS_OK);
    return PN532_STATUS_OK;
}

/** @brief Feed one byte through the frame sync and length probe. */
static pn532_status_t hsu_rx_byte(pn532_hsu_t *device, uint8_t value) {
    pn532_frame_span_t prefix;
    uint16_t expected;

    if (!device->sync_found) {
        if (device->sync_a == 0U && device->sync_b == 0U && value == 0xFFU) {
            device->rx->data[0] = 0x00U;
            device->rx->data[1] = 0x00U;
            device->rx->data[2] = 0xFFU;
            device->rx_used = 3U;
            device->sync_found = true;
        }
        device->sync_a = device->sync_b;
        device->sync_b = value;
        return PN532_STATUS_OK;
    }
    if (device->rx_used >= device->rx->capacity) return PN532_STATUS_BUFFER_TOO_SMALL;
    device->rx->data[device->rx_used++] = value;
    prefix.data = device->rx->data;
    prefix.length = device->rx_used;

    pn532_frame_result_t result = pn532_frame_probe(&prefix, &expected);
    if (result == PN532_FRAME_NEED_MORE_DATA) return PN532_STATUS_OK;
    if (result != PN532_FRAME_OK) return PN532_STATUS_FRAME_ERROR;
    if (expected > device->rx->capacity) return PN532_STATUS_BUFFER_TOO_SMALL;
    device->rx_expected = expected;
    return PN532_STATUS_OK;
}

/** @brief Receive a bounded number of available bytes without waiting. */
static pn532_async_result_t hsu_poll_rx(void *context) {
    pn532_hsu_t *device = (pn532_hsu_t *)context;
    uint16_t processed;

    if (!hsu_ready(device)) return hsu_result(PN532_ASYNC_FAILED, PN532_STATUS_INVALID_ARGUMENT);
    if (device->rx_result.state != PN532_ASYNC_BUSY) return device->rx_result;
    if (hsu_expired(device->io->now_ms(device->context), device->rx_started, device->rx->timeout_ms)) {
        return hsu_rx_fail(device, PN532_STATUS_TIMEOUT);
    }

    for (processed = 0U; processed < device->rx_budget; ++processed) {
        uint8_t value = 0U;
        pn532_hsu_rx_result_t received = device->io->rx_read(device->context, &value, 1U);
        pn532_status_t status;

        if (received.status != PN532_STATUS_OK) return hsu_rx_fail(device, received.status);
        if (received.count == 0U) break;
        if (received.count != 1U) return hsu_rx_fail(device, PN532_STATUS_IO_ERROR);
        status = hsu_rx_byte(device, value);
        if (status != PN532_STATUS_OK) return hsu_rx_fail(device, status);
        if (device->rx_expected != 0U && device->rx_used == device->rx_expected) {
            device->rx->length = device->rx_used;
            device->rx_result = hsu_result(PN532_ASYNC_DONE, PN532_STATUS_OK);
            return device->rx_result;
        }
    }
    return device->rx_result;
}

/** @brief Request cancellation of RX and TX operations without waiting. */
static pn532_status_t hsu_abort(void *context) {
    pn532_hsu_t *device = (pn532_hsu_t *)context;

    if (!hsu_ready(device)) return PN532_STATUS_INVALID_ARGUMENT;
    if (device->rx_result.state == PN532_ASYNC_BUSY) (void)hsu_rx_fail(device, PN532_STATUS_CANCELLED);
    hsu_request_tx_abort(device, PN532_STATUS_CANCELLED);
    return PN532_STATUS_OK;
}

/** @brief Purge stale UART RX only after both operations are terminal. */
static pn532_status_t hsu_flush_rx(void *context) {
    pn532_hsu_t *device = (pn532_hsu_t *)context;
    pn532_status_t status;

    if (!hsu_ready(device)) return PN532_STATUS_INVALID_ARGUMENT;
    if (device->tx_result.state == PN532_ASYNC_BUSY ||
        device->rx_result.state == PN532_ASYNC_BUSY) return PN532_STATUS_BUSY;

    status = device->io->rx_flush(device->context);
    if (status != PN532_STATUS_OK) return status;

    device->rx_used = 0U;
    device->rx_expected = 0U;
    device->sync_a = 0xFFU;
    device->sync_b = 0xFFU;
    device->sync_found = false;
    if (device->rx != NULL) device->rx->length = 0U;
    device->rx_result = hsu_result(PN532_ASYNC_IDLE, PN532_STATUS_OK);
    return PN532_STATUS_OK;
}

pn532_status_t pn532_hsu_init(pn532_hsu_t *device, const pn532_hsu_io_t *io, void *context, uint16_t rx_budget) {
    if (!device || !io || !io->tx_start || !io->tx_poll ||
        !io->tx_abort || !io->rx_read || !io->rx_flush || !io->now_ms ||
        rx_budget == 0U || rx_budget > PN532_FRAME_MAX_SIZE)
    {
        return PN532_STATUS_INVALID_ARGUMENT;
    }
    memset(device, 0, sizeof(*device));
    device->io = io;
    device->context = context;
    device->rx_budget = rx_budget;
    device->tx_result = hsu_result(PN532_ASYNC_IDLE, PN532_STATUS_OK);
    device->rx_result = hsu_result(PN532_ASYNC_IDLE, PN532_STATUS_OK);
    return PN532_STATUS_OK;
}

const pn532_transport_ops_t pn532_hsu_transport_ops = {
    .start_tx   = hsu_start_tx, 
    .poll_tx    = hsu_poll_tx, 
    .start_rx   = hsu_start_rx, 
    .poll_rx    = hsu_poll_rx, 
    .abort      = hsu_abort,
    .flush_rx   = hsu_flush_rx
};
