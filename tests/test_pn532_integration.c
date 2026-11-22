/**
 * @file test_pn532_integration.c
 * @brief End-to-end PN532 Core + HSU tests with bytewise fake UART.
 *
 * @details No mock transport: commands cross the real Core -> HSU ->
 * UART backend; PN532 ACK and response frames cross the inverse path.
 * UART TC and abort completion are signalled independently of DMA start.
 */
#include "pn532.h"
#include "pn532_hsu.h"
#include "pn532_cmd.h"
#include "pn532_tag.h"

#include <stdio.h>
#include <string.h>

static unsigned int checks;
static unsigned int failures;

#define CHECK(expr) do {                                              \
    ++checks;                                                          \
    if (!(expr)) {                                                     \
        ++failures;                                                    \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);  \
    }                                                                  \
} while (0)

#define UART_FIFO_SIZE 1024U

typedef struct {
    uint8_t fifo[UART_FIFO_SIZE];
    uint16_t head;
    uint16_t tail;
    uint32_t tick;
    const uint8_t *tx_data;
    uint16_t tx_length;
    bool tx_active;
    bool tx_complete;
    bool abort_requested;
    bool abort_complete;
    bool overrun;
    unsigned int tx_starts;
    unsigned int rx_reads;
    unsigned int rx_flushes;
    unsigned int tx_aborts;
    unsigned int host_acks;
} fake_uart_t;

typedef struct {
    unsigned int count;
    pn532_completion_t last;
    uint8_t response_copy[PN532_FRAME_MAX_PAYLOAD_SIZE];
} observer_t;

typedef struct {
    fake_uart_t uart;
    pn532_hsu_t hsu;
    pn532_t core;
    observer_t observer;
    uint8_t tx[PN532_FRAME_MAX_SIZE];
    uint8_t rx[PN532_FRAME_MAX_SIZE];
} fixture_t;

static const uint8_t ack_frame[PN532_FRAME_CONTROL_SIZE] = {
    0x00U, 0x00U, 0xFFU, 0x00U, 0xFFU, 0x00U
};

static pn532_status_t uart_tx_start(void *context,
                                   const uint8_t *data, uint16_t length)
{
    fake_uart_t *uart = (fake_uart_t *)context;
    if (uart->tx_active) return PN532_STATUS_BUSY;
    if (data == NULL || length == 0U) return PN532_STATUS_INVALID_ARGUMENT;
    uart->tx_active = true;
    uart->tx_complete = false;
    uart->abort_requested = false;
    uart->abort_complete = false;
    uart->tx_data = data;
    uart->tx_length = length;
    ++uart->tx_starts;
    if (length == PN532_FRAME_CONTROL_SIZE &&
        memcmp(data, ack_frame, sizeof(ack_frame)) == 0) {
        ++uart->host_acks;
    }
    return PN532_STATUS_OK;
}

static pn532_async_result_t uart_tx_poll(void *context)
{
    fake_uart_t *uart = (fake_uart_t *)context;
    pn532_async_result_t result = {PN532_ASYNC_BUSY, PN532_STATUS_OK};
    if (uart->abort_requested && uart->abort_complete) {
        uart->tx_active = false;
        result.state = PN532_ASYNC_FAILED;
        result.status = PN532_STATUS_CANCELLED;
    } else if (!uart->abort_requested && uart->tx_complete) {
        uart->tx_active = false;
        result.state = PN532_ASYNC_DONE;
    }
    return result;
}

static pn532_status_t uart_tx_abort(void *context)
{
    fake_uart_t *uart = (fake_uart_t *)context;
    ++uart->tx_aborts;
    uart->abort_requested = true;
    return PN532_STATUS_OK;
}

static pn532_hsu_rx_result_t uart_rx_read(void *context, uint8_t *data,
                                          uint16_t capacity)
{
    fake_uart_t *uart = (fake_uart_t *)context;
    pn532_hsu_rx_result_t result = {PN532_STATUS_OK, 0U};
    ++uart->rx_reads;
    if (uart->overrun) {
        result.status = PN532_STATUS_OVERRUN;
    } else if (capacity > 0U && uart->head < uart->tail) {
        *data = uart->fifo[uart->head++];
        result.count = 1U;
    }
    return result;
}

static pn532_status_t uart_rx_flush(void *context)
{
    fake_uart_t *uart = (fake_uart_t *)context;
    ++uart->rx_flushes;
    uart->head = uart->tail;
    uart->overrun = false;
    return PN532_STATUS_OK;
}

static uint32_t uart_now_ms(void *context)
{
    return ((fake_uart_t *)context)->tick;
}

static const pn532_hsu_io_t uart_io = {
    uart_tx_start, uart_tx_poll, uart_tx_abort,
    uart_rx_read, uart_rx_flush, uart_now_ms
};

static void completion(void *context, const pn532_completion_t *event)
{
    observer_t *observer = (observer_t *)context;
    ++observer->count;
    observer->last = *event;
    if (event->response != NULL && event->response_length > 0U) {
        memcpy(observer->response_copy, event->response,
               event->response_length);
    }
}

static void fixture_init(fixture_t *fixture, uint16_t budget)
{
    pn532_config_t config;
    memset(fixture, 0, sizeof(*fixture));
    CHECK(pn532_hsu_init(&fixture->hsu, &uart_io,
                         &fixture->uart, budget) == PN532_STATUS_OK);

    memset(&config, 0, sizeof(config));
    config.transport.ops = &pn532_hsu_transport_ops;
    config.transport.context = &fixture->hsu;
    config.now_ms = uart_now_ms;
    config.clock_context = &fixture->uart;
    config.tx_storage.data = fixture->tx;
    config.tx_storage.capacity = sizeof(fixture->tx);
    config.rx_storage.data = fixture->rx;
    config.rx_storage.capacity = sizeof(fixture->rx);
    config.tx_timeout_ms = 15U;
    config.ack_timeout_ms = 25U;
    CHECK(pn532_init(&fixture->core, &config) == PN532_STATUS_OK);
}

static void enqueue(fake_uart_t *uart, const uint8_t *bytes, uint16_t length)
{
    CHECK((uint32_t)uart->tail + length <= UART_FIFO_SIZE);
    if ((uint32_t)uart->tail + length > UART_FIFO_SIZE) return;
    memcpy(&uart->fifo[uart->tail], bytes, length);
    uart->tail = (uint16_t)(uart->tail + length);
}

static uint16_t encode_response(uint8_t *destination, const uint8_t *data,
                                uint16_t size)
{
    const pn532_frame_message_t message = {
        PN532_FRAME_TFI_DEVICE, data, size
    };
    pn532_frame_buffer_t buffer = {
        destination, PN532_FRAME_MAX_SIZE, 0U
    };
    CHECK(pn532_frame_encode(&message, &buffer) == PN532_FRAME_OK);
    return buffer.length;
}

static void enqueue_firmware(fake_uart_t *uart)
{
    const uint8_t payload[] = {
        0x03U, 0x32U, 0x01U, 0x06U, 0x07U
    };
    uint8_t frame[PN532_FRAME_MAX_SIZE];
    uint16_t length = encode_response(frame, payload, sizeof(payload));
    enqueue(uart, frame, length);
}

static pn532_command_t firmware_request(fixture_t *fixture)
{
    static const uint8_t code = 0x02U;
    const pn532_command_t command = {
        &code, 1U, 60U, completion, &fixture->observer
    };
    return command;
}

static pn532_recovery_request_t recovery_request(fixture_t *fixture)
{
    const pn532_recovery_request_t request = {
        60U, completion, &fixture->observer
    };
    return request;
}

/** @brief Bound the bytes fetched by every cooperative process invocation. */
static void step(fixture_t *fixture)
{
    unsigned int before = fixture->uart.rx_reads;
    pn532_process(&fixture->core);
    CHECK(fixture->uart.rx_reads - before <= fixture->hsu.rx_budget);
}

static void steps(fixture_t *fixture, unsigned int count)
{
    unsigned int index;
    for (index = 0U; index < count; ++index) step(fixture);
}

static void until_completed(fixture_t *fixture, unsigned int count)
{
    unsigned int index;
    unsigned int previous = fixture->observer.count;
    for (index = 0U; index < count &&
         fixture->observer.count == previous; ++index) {
        step(fixture);
    }
    CHECK(fixture->observer.count == previous + 1U);
}

/** @brief Receive noise, ACK and response in one UART burst with low budget. */
static void test_back_to_back_frames(void)
{
    fixture_t fixture;
    pn532_command_t command;
    const uint8_t noise[] = {0xA5U, 0xE1U, 0x00U};
    const uint8_t expected[] = {0x32U, 0x01U, 0x06U, 0x07U};

    fixture_init(&fixture, 2U);
    command = firmware_request(&fixture);
    CHECK(pn532_submit(&fixture.core, &command) == PN532_STATUS_OK);
    steps(&fixture, 2U);
    CHECK(fixture.hsu.rx_result.state == PN532_ASYNC_BUSY);
    CHECK(fixture.uart.tx_active);
    CHECK(fixture.uart.tx_length == 9U);
    CHECK(fixture.uart.tx_data == fixture.tx);

    enqueue(&fixture.uart, noise, sizeof(noise));
    enqueue(&fixture.uart, ack_frame, sizeof(ack_frame));
    enqueue_firmware(&fixture.uart);
    step(&fixture);
    CHECK(fixture.uart.head == 0U); /* No RX before physical UART TC. */
    fixture.uart.tx_complete = true;
    step(&fixture);
    until_completed(&fixture, 70U);
    CHECK(fixture.observer.last.result == PN532_RESULT_OK);
    CHECK(fixture.observer.last.response_length == sizeof(expected));
    CHECK(memcmp(fixture.observer.response_copy,
                 expected, sizeof(expected)) == 0);
    CHECK(fixture.uart.head == fixture.uart.tail);
    CHECK(fixture.uart.rx_flushes == 0U);
    steps(&fixture, 2U);
    CHECK(fixture.observer.count == 1U);
}

/** @brief Frame bytes can arrive over separate polling epochs. */
static void test_partial_delivery(void)
{
    fixture_t fixture;
    pn532_command_t command;
    uint8_t response[PN532_FRAME_MAX_SIZE];
    const uint8_t payload[] = {0x03U, 0x32U, 0x01U, 0x06U, 0x07U};
    uint16_t length;

    fixture_init(&fixture, 3U);
    command = firmware_request(&fixture);
    CHECK(pn532_submit(&fixture.core, &command) == PN532_STATUS_OK);
    steps(&fixture, 2U);
    fixture.uart.tx_complete = true;
    step(&fixture);

    enqueue(&fixture.uart, ack_frame, 4U);
    steps(&fixture, 4U);
    CHECK(fixture.observer.count == 0U);
    enqueue(&fixture.uart, ack_frame + 4U, 2U);
    steps(&fixture, 2U);
    CHECK(fixture.hsu.rx_result.state == PN532_ASYNC_BUSY);

    length = encode_response(response, payload, sizeof(payload));
    enqueue(&fixture.uart, response, 3U);
    steps(&fixture, 5U);
    CHECK(fixture.observer.count == 0U);
    enqueue(&fixture.uart, response + 3U, (uint16_t)(length - 3U));
    until_completed(&fixture, 70U);
    CHECK(fixture.observer.last.result == PN532_RESULT_OK);
    CHECK(fixture.observer.last.response_length == 4U);
}

/** @brief UART RX overrun must be visible in Core and lock new submits. */
static void test_uart_overrun(void)
{
    fixture_t fixture;
    pn532_command_t command;

    fixture_init(&fixture, 4U);
    command = firmware_request(&fixture);
    CHECK(pn532_submit(&fixture.core, &command) == PN532_STATUS_OK);
    steps(&fixture, 2U);
    fixture.uart.tx_complete = true;
    step(&fixture);
    fixture.uart.overrun = true;
    until_completed(&fixture, 3U);
    CHECK(fixture.observer.last.result == PN532_RESULT_TRANSPORT_ERROR);
    CHECK(fixture.observer.last.transport_status == PN532_STATUS_OVERRUN);
    CHECK(fixture.uart.rx_flushes >= 1U);
    CHECK(pn532_submit(&fixture.core, &command) == PN532_STATUS_IO_ERROR);
}

/** @brief Cancel preserves the TX backing memory until UART confirms abort. */
static void test_deferred_tx_abort(void)
{
    fixture_t fixture;
    pn532_command_t command;
    uint8_t snapshot[PN532_FRAME_MAX_SIZE];

    fixture_init(&fixture, 4U);
    command = firmware_request(&fixture);
    CHECK(pn532_submit(&fixture.core, &command) == PN532_STATUS_OK);
    steps(&fixture, 2U);
    memcpy(snapshot, fixture.tx, fixture.uart.tx_length);
    CHECK(pn532_cancel(&fixture.core) == PN532_STATUS_OK);
    step(&fixture);
    CHECK(fixture.uart.abort_requested);
    CHECK(fixture.uart.tx_active);
    CHECK(fixture.observer.count == 0U);
    steps(&fixture, 4U);
    CHECK(fixture.observer.count == 0U);
    CHECK(memcmp(snapshot, fixture.uart.tx_data,
                 fixture.uart.tx_length) == 0);
    CHECK(pn532_submit(&fixture.core, &command) == PN532_STATUS_BUSY);

    fixture.uart.abort_complete = true;
    step(&fixture);
    CHECK(!fixture.uart.tx_active);
    step(&fixture);
    CHECK(fixture.observer.count == 1U);
    CHECK(fixture.observer.last.result == PN532_RESULT_CANCELLED);
    CHECK(pn532_submit(&fixture.core, &command) == PN532_STATUS_IO_ERROR);
}

/** @brief Recover a timed-out command over real HSU byte framing. */
static void test_timeout_then_recover(void)
{
    fixture_t fixture;
    pn532_command_t command;
    pn532_recovery_request_t recovery;
    const uint8_t stale[] = {0x11U, 0x22U, 0x33U};

    fixture_init(&fixture, 5U);
    command = firmware_request(&fixture);
    recovery = recovery_request(&fixture);
    CHECK(pn532_submit(&fixture.core, &command) == PN532_STATUS_OK);
    steps(&fixture, 2U);
    fixture.uart.tx_complete = true;
    step(&fixture);
    enqueue(&fixture.uart, ack_frame, 3U);
    step(&fixture);
    CHECK(fixture.observer.count == 0U);
    fixture.uart.tick = 26U;
    until_completed(&fixture, 3U);
    CHECK(fixture.observer.last.result == PN532_RESULT_ACK_TIMEOUT);
    CHECK(pn532_submit(&fixture.core, &command) == PN532_STATUS_IO_ERROR);
    CHECK(fixture.uart.rx_flushes >= 1U);

    enqueue(&fixture.uart, stale, sizeof(stale));
    CHECK(pn532_recover(&fixture.core, &recovery) == PN532_STATUS_OK);
    CHECK(pn532_submit(&fixture.core, &command) == PN532_STATUS_BUSY);
    step(&fixture); /* Start host ACK transmission. */
    CHECK(fixture.uart.host_acks == 1U);
    CHECK(fixture.uart.tx_active);
    fixture.uart.tx_complete = true;
    step(&fixture); /* Terminal UART TC. */
    step(&fixture); /* Idle-only RX flush; stale bytes discarded. */
    CHECK(fixture.uart.head == fixture.uart.tail);
    step(&fixture); /* Arm verification RX. */
    step(&fixture); /* Start GetFirmwareVersion TX. */
    CHECK(fixture.uart.tx_starts == 3U);
    fixture.uart.tx_complete = true;
    step(&fixture);
    enqueue(&fixture.uart, ack_frame, sizeof(ack_frame));
    enqueue_firmware(&fixture.uart);
    until_completed(&fixture, 50U);
    CHECK(fixture.observer.last.result == PN532_RESULT_OK);
    CHECK(fixture.observer.last.response == NULL);
    CHECK(fixture.observer.last.response_length == 0U);
    CHECK(fixture.observer.count == 2U);
    CHECK(pn532_submit(&fixture.core, &command) == PN532_STATUS_OK);
}

/** @brief A damaged response DCS is rejected by the full pipeline. */
static void test_corrupted_response(void)
{
    fixture_t fixture;
    pn532_command_t command;
    uint8_t frame[PN532_FRAME_MAX_SIZE];
    const uint8_t payload[] = {0x03U, 0x32U, 0x01U, 0x06U, 0x07U};
    uint16_t length;

    fixture_init(&fixture, 7U);
    command = firmware_request(&fixture);
    CHECK(pn532_submit(&fixture.core, &command) == PN532_STATUS_OK);
    steps(&fixture, 2U);
    fixture.uart.tx_complete = true;
    step(&fixture);
    enqueue(&fixture.uart, ack_frame, sizeof(ack_frame));
    length = encode_response(frame, payload, sizeof(payload));
    frame[length - 2U] ^= 0x01U;
    enqueue(&fixture.uart, frame, length);
    until_completed(&fixture, 50U);
    CHECK(fixture.observer.last.result == PN532_RESULT_FRAME_ERROR);
    CHECK(pn532_submit(&fixture.core, &command) == PN532_STATUS_IO_ERROR);
}

/** @brief Complete extended 275-byte frame through bounded HSU RX. */
static void test_extended_response(void)
{
    fixture_t fixture;
    pn532_command_t command;
    uint8_t frame[PN532_FRAME_MAX_SIZE];
    uint8_t payload[PN532_FRAME_MAX_PAYLOAD_SIZE];
    uint16_t i;
    uint16_t length;

    fixture_init(&fixture, 7U);
    command = firmware_request(&fixture);
    CHECK(pn532_submit(&fixture.core, &command) == PN532_STATUS_OK);
    steps(&fixture, 2U);
    fixture.uart.tx_complete = true;
    step(&fixture);

    payload[0] = 0x03U;
    for (i = 1U; i < PN532_FRAME_MAX_PAYLOAD_SIZE; ++i) {
        payload[i] = (uint8_t)(i ^ 0x69U);
    }
    length = encode_response(frame, payload, sizeof(payload));
    CHECK(length == PN532_FRAME_MAX_SIZE);
    enqueue(&fixture.uart, ack_frame, sizeof(ack_frame));
    enqueue(&fixture.uart, frame, length);
    until_completed(&fixture, 120U);
    CHECK(fixture.observer.last.result == PN532_RESULT_OK);
    CHECK(fixture.observer.last.response_length ==
          PN532_FRAME_MAX_PAYLOAD_SIZE - 1U);
    CHECK(memcmp(fixture.observer.response_copy, payload + 1U,
                 PN532_FRAME_MAX_PAYLOAD_SIZE - 1U) == 0);
}


/** @brief Type-A target discovery through the real Core and HSU pipeline. */
static void test_type_a_command_and_decode(void)
{
    fixture_t fixture;
    pn532_tag_type_a_list_t tags;
    const pn532_cmd_type_a_request_t request = {
        2U, 60U, completion, &fixture.observer
    };
    const uint8_t payload[] = {
        0x4BU, 0x02U,
        0x01U, 0x44U, 0x00U, 0x20U, 0x07U,
        0x04U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U,
        0x03U, 0x75U, 0x77U,
        0x02U, 0x00U, 0x04U, 0x00U, 0x04U,
        0xAAU, 0xBBU, 0xCCU, 0xDDU
    };
    uint8_t frame[PN532_FRAME_MAX_SIZE];
    uint16_t length;

    fixture_init(&fixture, 3U);
    CHECK(pn532_cmd_list_type_a(&fixture.core, &request) ==
          PN532_STATUS_OK);
    steps(&fixture, 2U);
    CHECK(fixture.uart.tx_length == 11U);
    CHECK(fixture.uart.tx_data[5] == 0xD4U);
    CHECK(fixture.uart.tx_data[6] == 0x4AU);
    CHECK(fixture.uart.tx_data[7] == 0x02U);
    CHECK(fixture.uart.tx_data[8] == 0x00U);

    fixture.uart.tx_complete = true;
    step(&fixture);
    enqueue(&fixture.uart, ack_frame, sizeof(ack_frame));
    length = encode_response(frame, payload, sizeof(payload));
    enqueue(&fixture.uart, frame, length);
    until_completed(&fixture, 100U);
    CHECK(fixture.observer.last.result == PN532_RESULT_OK);
    CHECK(pn532_tag_parse_type_a(fixture.observer.response_copy,
                                  fixture.observer.last.response_length,
                                  &tags) == PN532_TAG_OK);
    CHECK(tags.count == 2U);
    CHECK(tags.targets[0].uid_length == 7U);
    CHECK(tags.targets[0].ats_length == 3U);
    CHECK(tags.targets[1].uid_length == 4U);
    CHECK(tags.targets[1].uid[3] == 0xDDU);
    /* Decode result remains valid after the caller reuses Core RX storage. */
    memset(fixture.rx, 0, sizeof(fixture.rx));
    CHECK(tags.targets[0].uid[0] == 0x04U);
    CHECK(tags.targets[1].uid[3] == 0xDDU);
}


/** @brief InDataExchange over the complete nonblocking Core + HSU path. */
static void test_in_data_exchange(void)
{
    fixture_t fixture;
    const uint8_t read_command[] = {0x40U, 0x01U, 0x30U, 0x02U};
    const uint8_t reply[] = {0x41U, 0x00U, 0xCAU, 0xFEU};
    const pn532_command_t command = {
        read_command, sizeof(read_command), 60U,
        completion, &fixture.observer
    };
    pn532_cmd_exchange_view_t view;
    uint8_t frame[PN532_FRAME_MAX_SIZE];
    uint16_t length;

    fixture_init(&fixture, 3U);
    CHECK(pn532_cmd_in_data_exchange(&fixture.core, &command) ==
          PN532_STATUS_OK);
    steps(&fixture, 2U);
    CHECK(fixture.uart.tx_length == 12U);
    CHECK(fixture.uart.tx_data[5] == 0xD4U);
    CHECK(fixture.uart.tx_data[6] == 0x40U);
    CHECK(fixture.uart.tx_data[7] == 0x01U);
    CHECK(fixture.uart.tx_data[8] == 0x30U);
    CHECK(fixture.uart.tx_data[9] == 0x02U);
    fixture.uart.tx_complete = true;
    step(&fixture);
    enqueue(&fixture.uart, ack_frame, sizeof(ack_frame));
    length = encode_response(frame, reply, sizeof(reply));
    enqueue(&fixture.uart, frame, length);
    until_completed(&fixture, 100U);
    CHECK(fixture.observer.last.result == PN532_RESULT_OK);
    CHECK(pn532_cmd_parse_exchange(fixture.observer.response_copy,
                                    fixture.observer.last.response_length,
                                    &view) == PN532_CMD_EXCHANGE_OK);
    CHECK(view.length == 2U);
    CHECK(view.data[0] == 0xCAU && view.data[1] == 0xFEU);
    CHECK(!view.more_information && !view.nad_present);
}

/** @brief Frame success does not imply PN532 RF exchange success. */
static void test_in_data_exchange_rf_error(void)
{
    fixture_t fixture;
    const uint8_t read_command[] = {0x40U, 0x01U, 0x30U, 0x02U};
    const uint8_t reply[] = {0x41U, 0x27U};
    const pn532_command_t command = {
        read_command, sizeof(read_command), 60U,
        completion, &fixture.observer
    };
    pn532_cmd_exchange_view_t view;
    uint8_t frame[PN532_FRAME_MAX_SIZE];
    uint16_t length;

    fixture_init(&fixture, 5U);
    CHECK(pn532_cmd_in_data_exchange(&fixture.core, &command) ==
          PN532_STATUS_OK);
    steps(&fixture, 2U);
    fixture.uart.tx_complete = true;
    step(&fixture);
    enqueue(&fixture.uart, ack_frame, sizeof(ack_frame));
    length = encode_response(frame, reply, sizeof(reply));
    enqueue(&fixture.uart, frame, length);
    until_completed(&fixture, 100U);
    CHECK(fixture.observer.last.result == PN532_RESULT_OK);
    CHECK(pn532_cmd_parse_exchange(fixture.observer.response_copy,
                                    fixture.observer.last.response_length,
                                    &view) == PN532_CMD_EXCHANGE_RF_ERROR);
    CHECK(view.error_code == 0x27U);
    CHECK(view.length == 0U);
}

int main(void)
{
    test_back_to_back_frames();
    test_partial_delivery();
    test_uart_overrun();
    test_deferred_tx_abort();
    test_timeout_then_recover();
    test_corrupted_response();
    test_extended_response();
    test_type_a_command_and_decode();
    test_in_data_exchange();
    test_in_data_exchange_rf_error();
    printf("PN532 Core+HSU integration: %u checks, %u failures\n",
           checks, failures);
    return failures == 0U ? 0 : 1;
}
