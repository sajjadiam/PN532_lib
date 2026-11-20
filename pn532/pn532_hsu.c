/**
 * @file pn532_hsu.c
 * @brief PN532 HSU transport with bounded, staged reads and sync recovery.
 */
#include "pn532_hsu.h"
#include "pn532_rx.h"

#include <string.h>

#define PN532_HSU_HEADER_SIZE 8U
#define PN532_HSU_DISCARD_SIZE 32U
#define PN532_HSU_MAX_TIMEOUT_MS UINT32_C(0x7FFFFFFF)

/**
 * @brief One receive operation's immutable timeout and UART context.
 *
 * @note Shared by RX helpers to avoid repeating the same arguments.
 */
typedef struct {
    const pn532_hsu_t *hsu;
    uint32_t started_ms;
    uint32_t timeout_ms;
} hsu_rx_context_t;

/** @brief Partially received header and the expected full frame length. */
typedef struct {
    uint8_t data[PN532_HSU_HEADER_SIZE];
    size_t used;
    size_t expected;
} hsu_frame_header_t;

/** @brief Map unsupported platform statuses to an I/O error. */
static pn532_status_t hsu_io_status(pn532_status_t status)
{
    if (status == PN532_STATUS_OK || status == PN532_STATUS_TIMEOUT ||
        status == PN532_STATUS_IO_ERROR) {
        return status;
    }
    return PN532_STATUS_IO_ERROR;
}

/** @brief Remaining time for this receive operation, including timer wrap. */
static uint32_t hsu_remaining(const hsu_rx_context_t *rx)
{
    const uint32_t elapsed = rx->hsu->io->now_ms(rx->hsu->context)
                             - rx->started_ms;

    return elapsed >= rx->timeout_ms ? 0U : rx->timeout_ms - elapsed;
}

/** @brief Read an exact number of bytes within the shared deadline. */
static pn532_status_t hsu_read(
    const hsu_rx_context_t *rx,
    uint8_t *data,
    size_t count)
{
    pn532_hsu_rx_t request = {data, count, hsu_remaining(rx)};

    if (request.timeout_ms == 0U) {
        return PN532_STATUS_TIMEOUT;
    }
    return hsu_io_status(rx->hsu->io->read_exact(
        rx->hsu->context, &request));
}

/** @brief Find 00 00 FF without waiting past the operation deadline. */
static pn532_status_t hsu_find_start(
    const hsu_rx_context_t *rx,
    hsu_frame_header_t *header)
{
    uint8_t a = 0xFFU;
    uint8_t b = 0xFFU;
    uint8_t current;
    pn532_status_t result;

    while (hsu_remaining(rx) != 0U) {
        result = hsu_read(rx, &current, 1U);
        if (result != PN532_STATUS_OK) {
            return result;
        }
        if (a == 0U && b == 0U && current == 0xFFU) {
            header->data[0] = 0U;
            header->data[1] = 0U;
            header->data[2] = 0xFFU;
            header->used = 3U;
            return PN532_STATUS_OK;
        }
        a = b;
        b = current;
    }
    return PN532_STATUS_TIMEOUT;
}

/** @brief Consume a known number of unread bytes after buffer overflow. */
static pn532_status_t hsu_discard(
    const hsu_rx_context_t *rx,
    size_t bytes)
{
    uint8_t scratch[PN532_HSU_DISCARD_SIZE];

    while (bytes != 0U) {
        const size_t chunk = bytes < sizeof(scratch) ? bytes : sizeof(scratch);
        const pn532_status_t result = hsu_read(rx, scratch, chunk);

        if (result != PN532_STATUS_OK) {
            return result;
        }
        bytes -= chunk;
    }
    return PN532_STATUS_OK;
}

/** @brief Receive enough header bytes to determine the entire frame size. */
static pn532_status_t hsu_read_header(
    const hsu_rx_context_t *rx,
    hsu_frame_header_t *header)
{
    pn532_frame_span_t prefix = {header->data, header->used};
    pn532_frame_result_t status;
    size_t next;

    /* Prefix strictly increases on every successful read (max. 8 bytes). */
    while (prefix.length <= sizeof(header->data)) {
        status = pn532_frame_rx_plan(&prefix, PN532_FRAME_MAX_SIZE, &next);
        if (status == PN532_FRAME_OK) {
            header->expected = prefix.length;
            return PN532_STATUS_OK;
        }
        if (status != PN532_FRAME_NEED_MORE_DATA) {
            return PN532_STATUS_FRAME_ERROR;
        }
        if (next > sizeof(header->data) - prefix.length) {
            header->expected = prefix.length + next;
            return PN532_STATUS_OK;
        }

        const pn532_status_t result = hsu_read(
            rx, header->data + prefix.length, next);
        if (result != PN532_STATUS_OK) {
            return result;
        }
        prefix.length += next;
        header->used = prefix.length;
    }
    return PN532_STATUS_FRAME_ERROR;
}

/** @brief Transmit one raw canonical PN532 frame without retries. */
static pn532_status_t hsu_write_frame(void *context, const pn532_tx_frame_t *frame)
{
    pn532_hsu_t *hsu = (pn532_hsu_t *)context;
    pn532_hsu_tx_t request;

    if (hsu == NULL || hsu->io == NULL || hsu->io->write_exact == NULL ||
        frame == NULL || frame->data == NULL ||
        frame->length < PN532_FRAME_CONTROL_SIZE ||
        frame->length > PN532_FRAME_MAX_SIZE ||
        frame->timeout_ms == 0U || frame->timeout_ms > PN532_HSU_MAX_TIMEOUT_MS) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }
    request.data = frame->data;
    request.length = frame->length;
    request.timeout_ms = frame->timeout_ms;
    return hsu_io_status(hsu->io->write_exact(hsu->context, &request));
}

/** @brief Collect one frame, discarding an oversized frame when possible. */
static pn532_status_t hsu_collect_frame(
    const hsu_rx_context_t *rx,
    pn532_rx_frame_t *frame)
{
    hsu_frame_header_t header = {{0}, 0U, 0U};
    pn532_status_t result;

    result = hsu_find_start(rx, &header);
    if (result != PN532_STATUS_OK) {
        return result;
    }
    result = hsu_read_header(rx, &header);
    if (result != PN532_STATUS_OK) {
        return result;
    }
    if (header.expected > frame->capacity) {
        result = hsu_discard(rx, header.expected - header.used);
        return result == PN532_STATUS_OK ? PN532_STATUS_BUFFER_TOO_SMALL : result;
    }
    memcpy(frame->data, header.data, header.used);
    if (header.expected > header.used) {
        result = hsu_read(
            rx, frame->data + header.used, header.expected - header.used);
        if (result != PN532_STATUS_OK) {
            return result;
        }
    }
    frame->length = header.expected;
    return PN532_STATUS_OK;
}

/** @brief Receive exactly one PN532 frame with an overall timeout. */
static pn532_status_t hsu_read_frame(void *context, pn532_rx_frame_t *frame)
{
    pn532_hsu_t *hsu = (pn532_hsu_t *)context;
    hsu_rx_context_t rx;

    if (frame != NULL) {
        frame->length = 0U;
    }
    if (hsu == NULL || hsu->io == NULL || hsu->io->read_exact == NULL ||
        hsu->io->now_ms == NULL || frame == NULL || frame->data == NULL ||
        frame->capacity == 0U || frame->timeout_ms == 0U ||
        frame->timeout_ms > PN532_HSU_MAX_TIMEOUT_MS) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }
    if (frame->capacity < PN532_FRAME_CONTROL_SIZE) {
        return PN532_STATUS_BUFFER_TOO_SMALL;
    }

    rx.hsu = hsu;
    rx.started_ms = hsu->io->now_ms(hsu->context);
    rx.timeout_ms = frame->timeout_ms;
    return hsu_collect_frame(&rx, frame);
}

pn532_status_t pn532_hsu_init(pn532_hsu_t *hsu,
                              const pn532_hsu_io_t *io, void *context)
{
    if (hsu == NULL || io == NULL || io->read_exact == NULL ||
        io->write_exact == NULL || io->now_ms == NULL) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }
    hsu->io = io;
    hsu->context = context;
    return PN532_STATUS_OK;
}

pn532_status_t pn532_hsu_wakeup(
    pn532_hsu_t *hsu, const pn532_hsu_wakeup_t *wake)
{
    static const uint8_t dummy = 0x55U;
    pn532_hsu_tx_t request;
    pn532_status_t result;

    if (hsu == NULL || hsu->io == NULL || hsu->io->write_exact == NULL ||
        hsu->io->delay_ms == NULL || wake == NULL ||
        wake->tx_timeout_ms == 0U || wake->settle_ms == 0U ||
        wake->tx_timeout_ms > PN532_HSU_MAX_TIMEOUT_MS) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }
    request.data = &dummy;
    request.length = 1U;
    request.timeout_ms = wake->tx_timeout_ms;
    result = hsu_io_status(hsu->io->write_exact(hsu->context, &request));
    if (result == PN532_STATUS_OK) {
        hsu->io->delay_ms(hsu->context, wake->settle_ms);
    }
    return result;
}

const pn532_transport_ops_t pn532_hsu_transport_ops = {
    hsu_write_frame,
    hsu_read_frame
};
