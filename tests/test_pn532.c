/**
 * @file test_pn532.c
 * @brief Deterministic command Core tests using an asynchronous frame backend.
 */
#include "pn532.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\\n", __FILE__, __LINE__, #x); } } while (0)

typedef struct {
    uint32_t tick;
    pn532_rx_frame_t *rx;
    bool tx_pending;
    bool tx_done;
    bool aborted;
    bool rx_pending;
    bool ack_ready;
    bool response_ready;
    bool use_nack;
    uint8_t ack[PN532_FRAME_CONTROL_SIZE];
    uint8_t nack[PN532_FRAME_CONTROL_SIZE];
    uint8_t response[PN532_FRAME_MAX_SIZE];
    uint16_t response_size;
    unsigned starts_tx;
    unsigned starts_rx;
    unsigned aborts;
} fake_t;

typedef struct {
    unsigned calls;
    pn532_completion_t last;
    uint8_t copied[PN532_FRAME_MAX_PAYLOAD_SIZE];
} observer_t;

static pn532_status_t fake_start_tx(void *context, const pn532_tx_frame_t *frame)
{
    fake_t *fake = (fake_t *)context;
    if (fake->tx_pending || frame == NULL || frame->data == NULL)
        return PN532_STATUS_BUSY;
    fake->tx_pending = true;
    fake->aborted = false;
    ++fake->starts_tx;
    return PN532_STATUS_OK;
}

static pn532_async_result_t fake_poll_tx(void *context)
{
    fake_t *fake = (fake_t *)context;
    pn532_async_result_t result = {PN532_ASYNC_BUSY, PN532_STATUS_OK};
    if (fake->aborted) {
        result.state = PN532_ASYNC_FAILED;
        result.status = PN532_STATUS_CANCELLED;
        fake->tx_pending = false;
    } else if (fake->tx_done) {
        result.state = PN532_ASYNC_DONE;
        fake->tx_pending = false;
    }
    return result;
}

static pn532_status_t fake_start_rx(void *context, pn532_rx_frame_t *frame)
{
    fake_t *fake = (fake_t *)context;
    if (fake->rx_pending || frame == NULL)
        return PN532_STATUS_BUSY;
    fake->rx = frame;
    fake->rx_pending = true;
    ++fake->starts_rx;
    return PN532_STATUS_OK;
}

static pn532_async_result_t fake_poll_rx(void *context)
{
    fake_t *fake = (fake_t *)context;
    pn532_async_result_t result = {PN532_ASYNC_BUSY, PN532_STATUS_OK};
    const uint8_t *bytes;
    uint16_t length;

    if (fake->starts_rx == 1U) {
        if (!fake->ack_ready) return result;
        bytes = fake->use_nack ? fake->nack : fake->ack;
        length = PN532_FRAME_CONTROL_SIZE;
    } else {
        if (!fake->response_ready) return result;
        bytes = fake->response;
        length = fake->response_size;
    }

    if (fake->rx->capacity < length) {
        result.state = PN532_ASYNC_FAILED;
        result.status = PN532_STATUS_BUFFER_TOO_SMALL;
    } else {
        memcpy(fake->rx->data, bytes, length);
        fake->rx->length = length;
        result.state = PN532_ASYNC_DONE;
    }
    fake->rx_pending = false;
    return result;
}

static pn532_status_t fake_abort(void *context)
{
    fake_t *fake = (fake_t *)context;
    ++fake->aborts;
    fake->rx_pending = false;
    if (fake->tx_pending) fake->aborted = true;
    return PN532_STATUS_OK;
}

static uint32_t fake_now(void *context)
{
    return ((fake_t *)context)->tick;
}

static const pn532_transport_ops_t fake_ops = {
    fake_start_tx, fake_poll_tx, fake_start_rx, fake_poll_rx, fake_abort
};

static void completed(void *context, const pn532_completion_t *completion)
{
    observer_t *observer = (observer_t *)context;
    ++observer->calls;
    observer->last = *completion;
    if (completion->response != NULL) {
        memcpy(observer->copied, completion->response,
               completion->response_length);
    }
}

static void fixture(fake_t *fake, pn532_t *device,
                    uint8_t *tx, uint8_t *rx)
{
    const uint8_t payload[] = {0x03U, 0x32U, 0x01U, 0x06U, 0x07U};
    pn532_frame_buffer_t buffer = {
        fake->response, sizeof(fake->response), 0U
    };
    const pn532_frame_message_t msg = {
        PN532_FRAME_TFI_DEVICE, payload, sizeof(payload)
    };
    const pn532_config_t config = {
        {&fake_ops, fake}, NULL, fake_now, fake,
        {tx, PN532_FRAME_MAX_SIZE}, {rx, PN532_FRAME_MAX_SIZE},
        10U, 20U
    };

    memset(fake, 0, sizeof(*fake));
    CHECK(pn532_frame_encode_control(PN532_FRAME_TYPE_ACK,
                                    &(pn532_frame_buffer_t){
                                        fake->ack, sizeof(fake->ack), 0U
                                    }) == PN532_FRAME_OK);
    CHECK(pn532_frame_encode_control(PN532_FRAME_TYPE_NACK,
                                    &(pn532_frame_buffer_t){
                                        fake->nack, sizeof(fake->nack), 0U
                                    }) == PN532_FRAME_OK);
    CHECK(pn532_frame_encode(&msg, &buffer) == PN532_FRAME_OK);
    fake->response_size = buffer.length;
    CHECK(pn532_init(device, &config) == PN532_STATUS_OK);
}

static pn532_command_t firmware_command(observer_t *observer)
{
    static const uint8_t bytes[] = {0x02U};
    const pn532_command_t command = {
        bytes, sizeof(bytes), 30U, completed, observer
    };
    return command;
}

static void run_steps(pn532_t *device, unsigned count)
{
    unsigned index;
    for (index = 0U; index < count; ++index)
        pn532_process(device);
}

static void test_success(void)
{
    fake_t fake;
    pn532_t device;
    observer_t observer = {0};
    uint8_t tx[PN532_FRAME_MAX_SIZE];
    uint8_t rx[PN532_FRAME_MAX_SIZE];
    pn532_command_t command = firmware_command(&observer);

    fixture(&fake, &device, tx, rx);
    CHECK(pn532_submit(&device, &command) == PN532_STATUS_OK);
    run_steps(&device, 2U);
    CHECK(fake.starts_rx == 1U && fake.starts_tx == 1U);
    CHECK(observer.calls == 0U);
    fake.tx_done = true;
    pn532_process(&device);
    CHECK(observer.calls == 0U);
    fake.ack_ready = true;
    pn532_process(&device);
    pn532_process(&device);
    CHECK(fake.starts_rx == 2U);
    fake.response_ready = true;
    pn532_process(&device);
    CHECK(observer.calls == 0U);
    pn532_process(&device);
    CHECK(observer.calls == 1U);
    CHECK(observer.last.result == PN532_RESULT_OK);
    CHECK(observer.last.response_length == 4U);
    CHECK(memcmp(observer.copied, "\x32\x01\x06\x07", 4U) == 0);
    run_steps(&device, 2U);
    CHECK(observer.calls == 1U);
}

static void test_nack_then_next_command(void)
{
    fake_t fake;
    pn532_t device;
    observer_t observer = {0};
    uint8_t tx[PN532_FRAME_MAX_SIZE];
    uint8_t rx[PN532_FRAME_MAX_SIZE];
    pn532_command_t command = firmware_command(&observer);

    fixture(&fake, &device, tx, rx);
    fake.use_nack = true;
    fake.ack_ready = true;
    fake.tx_done = true;
    CHECK(pn532_submit(&device, &command) == PN532_STATUS_OK);
    run_steps(&device, 6U);
    CHECK(observer.calls == 1U);
    CHECK(observer.last.result == PN532_RESULT_NACK);
    CHECK(pn532_submit(&device, &command) == PN532_STATUS_OK);
}

static void test_ack_timeout_and_recovery_lock(void)
{
    fake_t fake;
    pn532_t device;
    observer_t observer = {0};
    uint8_t tx[PN532_FRAME_MAX_SIZE];
    uint8_t rx[PN532_FRAME_MAX_SIZE];
    pn532_command_t command = firmware_command(&observer);

    fixture(&fake, &device, tx, rx);
    fake.tx_done = true;
    CHECK(pn532_submit(&device, &command) == PN532_STATUS_OK);
    run_steps(&device, 3U);
    fake.tick = 21U;
    run_steps(&device, 2U);
    CHECK(observer.calls == 1U);
    CHECK(observer.last.result == PN532_RESULT_ACK_TIMEOUT);
    CHECK(fake.aborts == 1U);
    CHECK(pn532_submit(&device, &command) == PN532_STATUS_IO_ERROR);
}

static void test_tx_timeout_and_lifetime(void)
{
    fake_t fake;
    pn532_t device;
    observer_t observer = {0};
    uint8_t tx[PN532_FRAME_MAX_SIZE];
    uint8_t rx[PN532_FRAME_MAX_SIZE];
    pn532_command_t command = firmware_command(&observer);

    fixture(&fake, &device, tx, rx);
    CHECK(pn532_submit(&device, &command) == PN532_STATUS_OK);
    run_steps(&device, 2U);
    fake.tick = 11U;
    pn532_process(&device);
    CHECK(observer.calls == 0U);
    CHECK(fake.aborts == 1U);
    CHECK(pn532_submit(&device, &command) == PN532_STATUS_BUSY);
    pn532_process(&device);
    CHECK(observer.calls == 0U);
    pn532_process(&device);
    CHECK(observer.calls == 1U);
    CHECK(observer.last.result == PN532_RESULT_TX_TIMEOUT);
    CHECK(pn532_submit(&device, &command) == PN532_STATUS_IO_ERROR);
}

static void test_cancel_before_tx(void)
{
    fake_t fake;
    pn532_t device;
    observer_t observer = {0};
    uint8_t tx[PN532_FRAME_MAX_SIZE];
    uint8_t rx[PN532_FRAME_MAX_SIZE];
    pn532_command_t command = firmware_command(&observer);

    fixture(&fake, &device, tx, rx);
    CHECK(pn532_submit(&device, &command) == PN532_STATUS_OK);
    CHECK(pn532_cancel(&device) == PN532_STATUS_OK);
    pn532_process(&device);
    CHECK(observer.calls == 0U);
    pn532_process(&device);
    CHECK(observer.calls == 1U);
    CHECK(observer.last.result == PN532_RESULT_CANCELLED);
    CHECK(fake.starts_tx == 0U);
    CHECK(pn532_submit(&device, &command) == PN532_STATUS_OK);
}

static void test_invalid_response(void)
{
    fake_t fake;
    pn532_t device;
    observer_t observer = {0};
    uint8_t tx[PN532_FRAME_MAX_SIZE];
    uint8_t rx[PN532_FRAME_MAX_SIZE];
    pn532_command_t command = firmware_command(&observer);
    uint8_t invalid_payload[] = {0x04U, 0x00U};
    pn532_frame_buffer_t buffer;
    pn532_frame_message_t message = {
        PN532_FRAME_TFI_DEVICE, invalid_payload, sizeof(invalid_payload)
    };

    fixture(&fake, &device, tx, rx);
    buffer.data = fake.response;
    buffer.capacity = sizeof(fake.response);
    buffer.length = 0U;
    CHECK(pn532_frame_encode(&message, &buffer) == PN532_FRAME_OK);
    fake.response_size = buffer.length;
    fake.tx_done = true;
    fake.ack_ready = true;
    fake.response_ready = true;
    CHECK(pn532_submit(&device, &command) == PN532_STATUS_OK);
    run_steps(&device, 8U);
    CHECK(observer.calls == 1U);
    CHECK(observer.last.result == PN532_RESULT_UNEXPECTED_RESPONSE);
    CHECK(pn532_submit(&device, &command) == PN532_STATUS_IO_ERROR);
}

int main(void)
{
    test_success();
    test_nack_then_next_command();
    test_ack_timeout_and_recovery_lock();
    test_tx_timeout_and_lifetime();
    test_cancel_before_tx();
    test_invalid_response();
    printf("PN532 Core: %u checks, %u failures\\n", checks, failures);
    return failures == 0U ? 0 : 1;
}
