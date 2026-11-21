/**
 * @file test_pn532_hsu.c
 * @brief Fake-UART tests for nonblocking HSU and optional TX lifecycle.
 */
#include "pn532_hsu.h"
#include "pn532_board.h"
#include <stdio.h>
#include <string.h>

static unsigned int checks;
static unsigned int failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

typedef struct {
    uint8_t rx[1024];
    size_t len;
    size_t pos;
    uint32_t tick;
    bool tx_busy;
    bool abort_requested;
    bool tx_complete;
    bool io_error;
    size_t tx_len;
    unsigned reads;
    unsigned flushes;
} mock_uart_t;

static pn532_status_t mock_tx_start(
    void *ctx, const uint8_t *data, size_t len)
{
    mock_uart_t *m = ctx;
    if (data == NULL || m->tx_busy) return PN532_STATUS_BUSY;
    m->tx_len = len;
    m->tx_busy = true;
    m->tx_complete = false;
    return PN532_STATUS_OK;
}
static pn532_async_result_t mock_tx_poll(void *ctx)
{
    mock_uart_t *m = ctx;
    pn532_async_result_t result = {PN532_ASYNC_BUSY, PN532_STATUS_OK};
    if (m->tx_complete || m->abort_requested) {
        m->tx_busy = false;
        result.state = PN532_ASYNC_DONE;
    }
    return result;
}
static pn532_status_t mock_tx_abort(void *ctx)
{
    mock_uart_t *m = ctx;
    m->abort_requested = true;
    return PN532_STATUS_OK;
}
static pn532_hsu_rx_result_t mock_rx_read(
    void *ctx, uint8_t *buffer, size_t capacity)
{
    mock_uart_t *m = ctx;
    pn532_hsu_rx_result_t result = {PN532_STATUS_OK, 0U};
    ++m->reads;
    if (m->io_error) {
        result.status = PN532_STATUS_OVERRUN;
    } else if (capacity > 0U && m->pos < m->len) {
        *buffer = m->rx[m->pos++];
        result.count = 1U;
    }
    return result;
}
static pn532_status_t mock_rx_flush(void *ctx)
{
    mock_uart_t *m = ctx;
    ++m->flushes;
    m->pos = m->len;
    m->io_error = false;
    return PN532_STATUS_OK;
}
static uint32_t mock_now(void *ctx) { return ((mock_uart_t *)ctx)->tick; }
static const pn532_hsu_io_t mock_io = {
    mock_tx_start, mock_tx_poll, mock_tx_abort,
    mock_rx_read, mock_rx_flush, mock_now
};

static void append(mock_uart_t *m, const uint8_t *data, size_t n)
{
    memcpy(m->rx + m->len, data, n);
    m->len += n;
}
static void test_rx_budget_and_frames(void)
{
    mock_uart_t m = {0};
    pn532_hsu_t hsu;
    const uint8_t noise[] = {0xAAU, 0x11U};
    const uint8_t ack[] = {0, 0, 0xFF, 0, 0xFF, 0};
    const uint8_t response[] = {0, 0, 0xFF, 2, 0xFE, 0xD5, 3, 0x28, 0};
    uint8_t buffer[PN532_FRAME_MAX_SIZE] = {0};
    pn532_rx_frame_t frame = {buffer, sizeof(buffer), 0U, 200U};
    pn532_transport_t t = {&pn532_hsu_transport_ops, &hsu};
    pn532_async_result_t r;
    unsigned int loops = 0;

    CHECK(pn532_hsu_init(&hsu, &mock_io, &m, 3U) == PN532_STATUS_OK);
    CHECK(t.ops->start_rx(t.context, &frame) == PN532_STATUS_OK);
    append(&m, noise, sizeof(noise));
    append(&m, ack, sizeof(ack));
    append(&m, response, sizeof(response));
    r = t.ops->poll_rx(t.context);
    CHECK(r.state == PN532_ASYNC_BUSY && m.pos == 3U);
    while (r.state == PN532_ASYNC_BUSY && loops++ < 20U) {
        const size_t old = m.pos;
        r = t.ops->poll_rx(t.context);
        CHECK(m.pos - old <= 3U);
    }
    CHECK(r.state == PN532_ASYNC_DONE);
    CHECK(frame.length == sizeof(ack));
    CHECK(memcmp(buffer, ack, sizeof(ack)) == 0);
    CHECK(t.ops->start_rx(t.context, &frame) == PN532_STATUS_OK);
    loops = 0U;
    r = t.ops->poll_rx(t.context);
    while (r.state == PN532_ASYNC_BUSY && loops++ < 20U) {
        r = t.ops->poll_rx(t.context);
    }
    CHECK(r.state == PN532_ASYNC_DONE);
    CHECK(frame.length == sizeof(response));
    CHECK(memcmp(buffer, response, sizeof(response)) == 0);
}

static void test_tx_timeout_and_cancel(void)
{
    mock_uart_t m = {0};
    pn532_hsu_t hsu;
    const uint8_t ack[] = {0, 0, 0xFF, 0, 0xFF, 0};
    pn532_tx_frame_t frame = {ack, sizeof(ack), 10U};
    pn532_transport_t t = {&pn532_hsu_transport_ops, &hsu};
    pn532_async_result_t r;

    pn532_hsu_init(&hsu, &mock_io, &m, 16U);
    CHECK(t.ops->start_tx(t.context, &frame) == PN532_STATUS_OK);
    CHECK(t.ops->start_tx(t.context, &frame) == PN532_STATUS_BUSY);
    r = t.ops->poll_tx(t.context);
    CHECK(r.state == PN532_ASYNC_BUSY);
    m.tick = 10U;
    r = t.ops->poll_tx(t.context);
    CHECK(r.state == PN532_ASYNC_BUSY && m.abort_requested);
    r = t.ops->poll_tx(t.context);
    CHECK(r.state == PN532_ASYNC_FAILED &&
          r.status == PN532_STATUS_TIMEOUT);
    m.abort_requested = false;
    CHECK(t.ops->start_tx(t.context, &frame) == PN532_STATUS_OK);
    CHECK(t.ops->abort(t.context) == PN532_STATUS_OK);
    r = t.ops->poll_tx(t.context);
    CHECK(r.state == PN532_ASYNC_FAILED &&
          r.status == PN532_STATUS_CANCELLED);
}

static void test_rx_errors(void)
{
    mock_uart_t m = {0};
    pn532_hsu_t hsu;
    uint8_t buffer[275] = {0};
    pn532_rx_frame_t frame = {buffer, sizeof(buffer), 42U, 5U};
    pn532_transport_t t = {&pn532_hsu_transport_ops, &hsu};
    pn532_async_result_t r;

    pn532_hsu_init(&hsu, &mock_io, &m, 8U);
    CHECK(t.ops->start_rx(t.context, &frame) == PN532_STATUS_OK);
    m.io_error = true;
    r = t.ops->poll_rx(t.context);
    CHECK(r.state == PN532_ASYNC_FAILED && r.status == PN532_STATUS_OVERRUN);
    CHECK(frame.length == 0U && m.flushes == 1U);
    CHECK(t.ops->start_rx(t.context, &frame) == PN532_STATUS_OK);
    m.tick = 6U;
    r = t.ops->poll_rx(t.context);
    CHECK(r.state == PN532_ASYNC_FAILED && r.status == PN532_STATUS_TIMEOUT);
    CHECK(t.ops->start_rx(t.context, &frame) == PN532_STATUS_OK);
    CHECK(t.ops->abort(t.context) == PN532_STATUS_OK);
    r = t.ops->poll_rx(t.context);
    CHECK(r.state == PN532_ASYNC_FAILED &&
          r.status == PN532_STATUS_CANCELLED);
}

int main(void)
{
    test_rx_budget_and_frames();
    test_tx_timeout_and_cancel();
    test_rx_errors();
    printf("PN532 HSU: %u checks, %u failures\n", checks, failures);
    return failures == 0U ? 0 : 1;
}
