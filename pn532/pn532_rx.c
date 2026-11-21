/**
 * @file pn532_rx.c
 * @brief Length-based receive planner shared by transport adapters.
 */
#include "pn532_rx.h"

/** @brief Determine the next header boundary required by the frame probe. */
static uint16_t pn532_rx_header_target(uint16_t received) {
    if (received < 3U) return 3U;
    if (received < 5U) return 5U;
    return 8U;
}

pn532_frame_result_t pn532_frame_rx_plan(const pn532_frame_span_t *prefix, uint16_t capacity, uint16_t *next_bytes) {
    uint16_t target = 0U;

    if (next_bytes) {
        *next_bytes = 0U;
    }
    if (!prefix || !next_bytes || (prefix->length > 0U && !prefix->data) || prefix->length > capacity) {
        return PN532_FRAME_INVALID_ARGUMENT;
    }

    pn532_frame_result_t result = pn532_frame_probe(prefix, &target);
    if (result != PN532_FRAME_OK && result != PN532_FRAME_NEED_MORE_DATA) return result;
    if (result == PN532_FRAME_NEED_MORE_DATA) target = pn532_rx_header_target(prefix->length);
    if (target > capacity) return PN532_FRAME_BUFFER_TOO_SMALL;
    if (prefix->length > target) return PN532_FRAME_INVALID_FORMAT;
    if (prefix->length == target) return PN532_FRAME_OK;

    *next_bytes = (uint16_t)(target - prefix->length);
    return PN532_FRAME_NEED_MORE_DATA;
}
