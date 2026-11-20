/**
 * @file pn532_rx.c
 * @brief Length-based receive planner shared by transport adapters.
 */
#include "pn532_rx.h"

/** @brief Determine the next header boundary required by the frame probe. */
static size_t pn532_rx_header_target(size_t received)
{
    if (received < 3U) {
        return 3U;
    }
    if (received < 5U) {
        return 5U;
    }
    return 8U;
}

pn532_frame_result_t pn532_frame_rx_plan(
    const pn532_frame_span_t *prefix,
    size_t capacity,
    size_t *next_bytes)
{
    pn532_frame_result_t result;
    size_t target = 0U;

    if (next_bytes != NULL) {
        *next_bytes = 0U;
    }
    if (prefix == NULL || next_bytes == NULL ||
        (prefix->length > 0U && prefix->data == NULL) ||
        prefix->length > capacity) {
        return PN532_FRAME_INVALID_ARGUMENT;
    }

    result = pn532_frame_probe(prefix, &target);
    if (result != PN532_FRAME_OK &&
        result != PN532_FRAME_NEED_MORE_DATA) {
        return result;
    }
    if (result == PN532_FRAME_NEED_MORE_DATA) {
        target = pn532_rx_header_target(prefix->length);
    }
    if (target > capacity) {
        return PN532_FRAME_BUFFER_TOO_SMALL;
    }
    if (prefix->length > target) {
        return PN532_FRAME_INVALID_FORMAT;
    }
    if (prefix->length == target) {
        return PN532_FRAME_OK;
    }

    *next_bytes = target - prefix->length;
    return PN532_FRAME_NEED_MORE_DATA;
}
