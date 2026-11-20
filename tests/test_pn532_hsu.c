/**
 * @file test_pn532_hsu.c
 * @brief Host-side fake-UART regression tests for the PN532 HSU adapter.
 */
#include "pn532_hsu.h"
#include "pn532_frame.h"
#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures;     fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); } } while (0)

typedef struct {
    uint8_t rx[1024];
    size_t rx_length;
    size_t rx_offset;
    uint8_t tx[300];
    size_t tx_length;
    uint32_t clock_ms;
    uint32_t last_delay;
    unsigned reads;
    unsigned writes;
    size_t max_read;
    pn532_status_t read_fault;
    pn532_status_t write_fault;
} fake_port_t;

static pn532_status_t fake_read(void *opaque, pn532_hsu_rx_t *request)
{
    fake_port_t *port = opaque;
    ++port->reads;
    port->max_read = request->length > port->max_read ? request->length : port->max_read;
    if (port->read_fault != PN532_STATUS_OK) return port->read_fault;
    if (request->length > port->rx_length - port->rx_offset) return PN532_STATUS_TIMEOUT;
    if (request->timeout_ms < 1U) return PN532_STATUS_TIMEOUT;
    memcpy(request->data, port->rx + port->rx_offset, request->length);
    port->rx_offset += request->length;
    ++port->clock_ms;
    return PN532_STATUS_OK;
}

static pn532_status_t fake_write(void *opaque, const pn532_hsu_tx_t *request)
{
    fake_port_t *port = opaque;
    ++port->writes;
    if (port->write_fault != PN532_STATUS_OK) return port->write_fault;
    if (request->length > sizeof(port->tx)) return PN532_STATUS_IO_ERROR;
    memcpy(port->tx, request->data, request->length);
    port->tx_length = request->length;
    ++port->clock_ms;
    return PN532_STATUS_OK;
}
static uint32_t fake_now(void *opaque) { return ((fake_port_t *)opaque)->clock_ms; }
static void fake_delay(void *opaque, uint32_t delay_ms)
{
    fake_port_t *port = opaque;
    port->last_delay = delay_ms;
    port->clock_ms += delay_ms;
}
static const pn532_hsu_io_t io = {fake_read, fake_write, fake_now, fake_delay};

static void append(fake_port_t *port, const uint8_t *data, size_t count)
{
    memcpy(port->rx + port->rx_length, data, count);
    port->rx_length += count;
}

static void test_write(void)
{
    fake_port_t port = {0};
    pn532_hsu_t hsu;
    const uint8_t data[] = {0, 0, 0xFF, 0, 0xFF, 0};
    const pn532_tx_frame_t tx = {data, sizeof(data), 20U};
    pn532_transport_t transport = {&pn532_hsu_transport_ops, &hsu};
    CHECK(pn532_hsu_init(&hsu, &io, &port) == PN532_STATUS_OK);
    CHECK(transport.ops->write_frame(transport.context, &tx) == PN532_STATUS_OK);
    CHECK(port.tx_length == sizeof(data) && memcmp(data, port.tx, sizeof(data)) == 0);
    {
        const pn532_hsu_wakeup_t wake = {10U, 3U};
        CHECK(pn532_hsu_wakeup(&hsu, &wake) == PN532_STATUS_OK);
        CHECK(port.tx_length == 1U && port.tx[0] == 0x55U);
        CHECK(port.last_delay == 3U);
    }
    port.write_fault = PN532_STATUS_TIMEOUT;
    CHECK(transport.ops->write_frame(transport.context, &tx) == PN532_STATUS_TIMEOUT);
    port.write_fault = PN532_STATUS_FRAME_ERROR;
    CHECK(transport.ops->write_frame(transport.context, &tx) == PN532_STATUS_IO_ERROR);
}

static void test_stream(void)
{
    fake_port_t port = {0};
    pn532_hsu_t hsu;
    const uint8_t noise[] = {0x91, 0, 0x40, 0, 0, 0x12, 0xFD};
    const uint8_t ack[] = {0, 0, 0xFF, 0, 0xFF, 0};
    const uint8_t response[] = {0, 0, 0xFF, 2, 0xFE, 0xD5, 3, 0x28, 0};
    uint8_t frame[PN532_FRAME_MAX_SIZE] = {0};
    pn532_rx_frame_t rx = {frame, sizeof(frame), 99U, 60U};
    pn532_transport_t transport = {&pn532_hsu_transport_ops, &hsu};
    pn532_hsu_init(&hsu, &io, &port);
    append(&port, noise, sizeof(noise));
    append(&port, ack, sizeof(ack));
    append(&port, response, sizeof(response));
    CHECK(transport.ops->read_frame(transport.context, &rx) == PN532_STATUS_OK);
    CHECK(rx.length == sizeof(ack) && memcmp(ack, frame, sizeof(ack)) == 0);
    CHECK(port.rx_offset == sizeof(noise) + sizeof(ack));
    CHECK(transport.ops->read_frame(transport.context, &rx) == PN532_STATUS_OK);
    CHECK(rx.length == sizeof(response) && memcmp(response, frame, sizeof(response)) == 0);
    CHECK(port.rx_offset == port.rx_length);
    CHECK(port.max_read == 4U);
}

static void test_extended_and_discard(void)
{
    fake_port_t port = {0};
    pn532_hsu_t hsu;
    uint8_t extended[275] = {0};
    const uint8_t ack[] = {0, 0, 0xFF, 0, 0xFF, 0};
    uint8_t received[275] = {0};
    pn532_rx_frame_t rx = {received, sizeof(received), 0U, 80U};
    pn532_transport_t transport = {&pn532_hsu_transport_ops, &hsu};
    pn532_hsu_init(&hsu, &io, &port);
    extended[2] = 0xFF;
    extended[3] = 0xFF;
    extended[4] = 0xFF;
    extended[5] = 1;
    extended[6] = 9;
    extended[7] = 0xF6;
    extended[8] = 0xD5;
    append(&port, extended, sizeof(extended));
    CHECK(transport.ops->read_frame(transport.context, &rx) == PN532_STATUS_OK);
    CHECK(rx.length == sizeof(extended) && port.max_read == 267U);
    CHECK(memcmp(received, extended, sizeof(extended)) == 0);

    memset(&port, 0, sizeof(port));
    append(&port, extended, sizeof(extended));
    append(&port, ack, sizeof(ack));
    rx.capacity = 12U;
    CHECK(transport.ops->read_frame(transport.context, &rx) == PN532_STATUS_BUFFER_TOO_SMALL);
    CHECK(rx.length == 0U && port.rx_offset == sizeof(extended));
    rx.capacity = sizeof(received);
    CHECK(transport.ops->read_frame(transport.context, &rx) == PN532_STATUS_OK);
    CHECK(rx.length == sizeof(ack) && memcmp(received, ack, sizeof(ack)) == 0);
}

static void test_bad_input_and_deadline(void)
{
    fake_port_t port = {0};
    pn532_hsu_t hsu;
    const uint8_t malformed[] = {0, 0, 0xFF, 2, 0xFD};
    const uint8_t ack[] = {0, 0, 0xFF, 0, 0xFF, 0};
    uint8_t received[275] = {0};
    pn532_rx_frame_t rx = {received, sizeof(received), 88U, 60U};
    pn532_transport_t transport = {&pn532_hsu_transport_ops, &hsu};
    CHECK(pn532_hsu_init(NULL, &io, &port) == PN532_STATUS_INVALID_ARGUMENT);
    CHECK(pn532_hsu_init(&hsu, NULL, &port) == PN532_STATUS_INVALID_ARGUMENT);
    CHECK(pn532_hsu_init(&hsu, &io, &port) == PN532_STATUS_OK);
    CHECK(transport.ops->read_frame(transport.context, NULL) == PN532_STATUS_INVALID_ARGUMENT);
    CHECK(transport.ops->read_frame(transport.context, &rx) == PN532_STATUS_TIMEOUT);
    CHECK(rx.length == 0U);
    append(&port, malformed, sizeof(malformed));
    CHECK(transport.ops->read_frame(transport.context, &rx) == PN532_STATUS_FRAME_ERROR);
    memset(&port, 0, sizeof(port));
    append(&port, ack, sizeof(ack));
    rx.capacity = 5U;
    CHECK(transport.ops->read_frame(transport.context, &rx) == PN532_STATUS_BUFFER_TOO_SMALL);
    CHECK(port.rx_offset == 0U);
    rx.capacity = sizeof(received);
    rx.timeout_ms = 2U;
    CHECK(transport.ops->read_frame(transport.context, &rx) == PN532_STATUS_TIMEOUT);
    CHECK(rx.length == 0U);
    memset(&port, 0, sizeof(port));
    port.clock_ms = UINT32_MAX - 3U;
    append(&port, ack, sizeof(ack));
    rx.timeout_ms = 20U;
    CHECK(transport.ops->read_frame(transport.context, &rx) == PN532_STATUS_OK);
    CHECK(rx.length == sizeof(ack));
}

int main(void)
{
    test_write();
    test_stream();
    test_extended_and_discard();
    test_bad_input_and_deadline();
    printf("PN532 HSU: %u checks, %u failures\n", checks, failures);
    return failures == 0U ? 0 : 1;
}
