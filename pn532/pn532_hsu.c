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

/** @brief Map unsupported platform statuses to an I/O error. */
static pn532_status_t hsu_io_status(pn532_status_t status)
{
    if (status == PN532_STATUS_OK || status == PN532_STATUS_TIMEOUT ||
        status == PN532_STATUS_IO_ERROR) {
        return status;
    }
    return PN532_STATUS_IO_ERROR;
}

/** @brief Derive a remaining timeout, safely accounting for timer wrap. */
static uint32_t hsu_remaining(const pn532_hsu_t *hsu,
                              uint32_t started, uint32_t timeout_ms)
{
    const uint32_t elapsed = hsu->io->now_ms(hsu->context) - started;
    return elapsed >= timeout_ms ? 0U : timeout_ms - elapsed;
}

/** @brief Read exactly count bytes under one overall operation deadline. */
static pn532_status_t hsu_read(const pn532_hsu_t *hsu,
                               uint8_t *data, size_t count,
                               uint32_t started, uint32_t timeout_ms)
{
    pn532_hsu_rx_t request = {data, count, hsu_remaining(hsu, started, timeout_ms)};
    if (request.timeout_ms == 0U) {
        return PN532_STATUS_TIMEOUT;
    }
    return hsu_io_status(hsu->io->read_exact(hsu->context, &request));
}

/** @brief Skip garbage and locate the canonical 00 00 FF start marker. */
static pn532_status_t hsu_find_start(const pn532_hsu_t *hsu,
                                     uint8_t *header, uint32_t started,
                                     uint32_t timeout_ms)
{
    uint8_t a = 0xFFU;
    uint8_t b = 0xFFU;
    uint8_t current = 0U;
    pn532_status_t result;

    for (;;) {
        result = hsu_read(hsu, &current, 1U, started, timeout_ms);
        if (result != PN532_STATUS_OK) {
            return result;
        }
        if (a == 0U && b == 0U && current == 0xFFU) {
            header[0] = 0U;
            header[1] = 0U;
            header[2] = 0xFFU;
            return PN532_STATUS_OK;
        }
        a = b;
        b = current;
    }
}

/** @brief Discard known surplus bytes to realign subsequent read calls. */
static pn532_status_t hsu_discard(const pn532_hsu_t *hsu,
                                  size_t bytes, uint32_t started,
                                  uint32_t timeout_ms)
{
    uint8_t scratch[PN532_HSU_DISCARD_SIZE];
    pn532_status_t result;

    while (bytes > 0U) {
        const size_t count = bytes < sizeof(scratch) ? bytes : sizeof(scratch);
        result = hsu_read(hsu, scratch, count, started, timeout_ms);
        if (result != PN532_STATUS_OK) {
            return result;
        }
        bytes -= count;
    }
    return PN532_STATUS_OK;
}

/** @brief Collect prefix bytes and determine the full expected frame size. */
static pn532_status_t hsu_read_header(const pn532_hsu_t *hsu,
                                      uint8_t *header, size_t *used,
                                      size_t *expected, uint32_t started,
                                      uint32_t timeout_ms)
{
    pn532_frame_result_t status;
    pn532_frame_span_t prefix = {header, 3U};
    size_t next;
    pn532_status_t io_result;

    *used = 3U;
    for (;;) {
        status = pn532_frame_rx_plan(&prefix, PN532_FRAME_MAX_SIZE, &next);
        if (status == PN532_FRAME_OK) {
            *expected = prefix.length;
            return PN532_STATUS_OK;
        }
        if (status != PN532_FRAME_NEED_MORE_DATA) {
            return PN532_STATUS_FRAME_ERROR;
        }
        if (prefix.length + next > PN532_HSU_HEADER_SIZE) {
            *used = prefix.length;
            *expected = prefix.length + next;
            return PN532_STATUS_OK;
        }
        io_result = hsu_read(hsu, header + prefix.length, next,
                             started, timeout_ms);
        if (io_result != PN532_STATUS_OK) {
            return io_result;
        }
        prefix.length += next;
        *used = prefix.length;
    }
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

/** @brief Read the header, then read or drain the remaining frame bytes. */
static pn532_status_t hsu_collect_frame(const pn532_hsu_t *hsu,
                                        pn532_rx_frame_t *frame,
                                        uint32_t started)
{
    uint8_t header[PN532_HSU_HEADER_SIZE];
    size_t used = 0U;
    size_t expected = 0U;
    pn532_status_t result;

    result = hsu_find_start(hsu, header, started, frame->timeout_ms);
    if (result != PN532_STATUS_OK) {
        return result;
    }
    result = hsu_read_header(hsu, header, &used, &expected,
                             started, frame->timeout_ms);
    if (result != PN532_STATUS_OK) {
        return result;
    }
    if (expected > frame->capacity) {
        result = hsu_discard(hsu, expected - used, started, frame->timeout_ms);
        return result == PN532_STATUS_OK ? PN532_STATUS_BUFFER_TOO_SMALL : result;
    }
    memcpy(frame->data, header, used);
    if (expected > used) {
        result = hsu_read(hsu, frame->data + used, expected - used,
                          started, frame->timeout_ms);
        if (result != PN532_STATUS_OK) {
            return result;
        }
    }
    frame->length = expected;
    return PN532_STATUS_OK;
}

/** @brief Read one complete PN532 frame under a single overall deadline. */
static pn532_status_t hsu_read_frame(void *context, pn532_rx_frame_t *frame)
{
    pn532_hsu_t *hsu = (pn532_hsu_t *)context;

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
    return hsu_collect_frame(hsu, frame, hsu->io->now_ms(hsu->context));
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
