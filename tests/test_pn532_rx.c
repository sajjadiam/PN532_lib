/**
 * @file test_pn532_rx.c
 * @brief Transport-neutral RX planning regression tests.
 */
#include "pn532_rx.h"
#include <stdio.h>

static unsigned int checks;
static unsigned int failures;

#define CHECK(expression) do { \
    ++checks; \
    if (!(expression)) { \
        ++failures; \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
    } \
} while (0)

/** @brief Feed successive canonical header/payload sections to planner. */
static void test_sequence(
    const uint8_t *data,
    size_t length,
    const size_t *chunks,
    size_t chunk_count)
{
    pn532_frame_span_t prefix = {data, 0U};
    size_t next = 0U;
    size_t i;

    for (i = 0U; i < chunk_count; ++i) {
        CHECK(pn532_frame_rx_plan(&prefix, length, &next) ==
              PN532_FRAME_NEED_MORE_DATA);
        CHECK(next == chunks[i]);
        prefix.length += next;
    }
    CHECK(prefix.length == length);
    CHECK(pn532_frame_rx_plan(&prefix, length, &next) == PN532_FRAME_OK);
    CHECK(next == 0U);
}

static void test_complete_frames(void)
{
    const uint8_t ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    const uint8_t nack[] = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};
    const uint8_t error[] = {0x00, 0x00, 0xFF, 0x01, 0xFF, 0x7F, 0x81, 0x00};
    const uint8_t normal[] = {0x00, 0x00, 0xFF, 0x02, 0xFE,
                               0xD4, 0x02, 0x2A, 0x00};
    const uint8_t extended[275] = {
        0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x01, 0x09, 0xF6
    };
    const size_t control_steps[] = {3U, 2U, 1U};
    const size_t error_steps[] = {3U, 2U, 3U};
    const size_t normal_steps[] = {3U, 2U, 4U};
    const size_t extended_steps[] = {3U, 2U, 3U, 267U};

    test_sequence(ack, sizeof(ack), control_steps, 3U);
    test_sequence(nack, sizeof(nack), control_steps, 3U);
    test_sequence(error, sizeof(error), error_steps, 3U);
    test_sequence(normal, sizeof(normal), normal_steps, 3U);
    /* The planner checks the length header, not payload validity. */
    test_sequence(extended, sizeof(extended), extended_steps, 4U);
}

static void test_limits_and_bad_headers(void)
{
    uint8_t bytes[10] = {0x00, 0x00, 0xFF, 0x02, 0xFE,
                         0xD4, 0x02, 0x2A, 0x00, 0x00};
    pn532_frame_span_t prefix = {bytes, 0U};
    size_t next = 77U;

    CHECK(pn532_frame_rx_plan(NULL, 275U, &next) ==
          PN532_FRAME_INVALID_ARGUMENT && next == 0U);
    CHECK(pn532_frame_rx_plan(&prefix, 275U, NULL) ==
          PN532_FRAME_INVALID_ARGUMENT);
    CHECK(pn532_frame_rx_plan(&prefix, 2U, &next) ==
          PN532_FRAME_BUFFER_TOO_SMALL && next == 0U);

    prefix.length = 1U;
    prefix.data = NULL;
    CHECK(pn532_frame_rx_plan(&prefix, 275U, &next) ==
          PN532_FRAME_INVALID_ARGUMENT);
    prefix.data = bytes;
    prefix.length = 5U;
    CHECK(pn532_frame_rx_plan(&prefix, 8U, &next) ==
          PN532_FRAME_BUFFER_TOO_SMALL);
    prefix.length = 10U;
    CHECK(pn532_frame_rx_plan(&prefix, 275U, &next) ==
          PN532_FRAME_INVALID_FORMAT);
    prefix.length = 3U;
    bytes[2] = 0xFEU;
    CHECK(pn532_frame_rx_plan(&prefix, 275U, &next) ==
          PN532_FRAME_INVALID_FORMAT);
    bytes[2] = 0xFFU;
    prefix.length = 5U;
    bytes[4] = 0x01U;
    CHECK(pn532_frame_rx_plan(&prefix, 275U, &next) ==
          PN532_FRAME_CHECKSUM_ERROR);
    bytes[4] = 0xFEU;
    prefix.length = 9U;
    CHECK(pn532_frame_rx_plan(&prefix, 8U, &next) ==
          PN532_FRAME_INVALID_ARGUMENT);

    /* Normal LEN=255 is distinct from extended FF FF. */
    bytes[3] = 0xFFU;
    bytes[4] = 0x01U;
    prefix.length = 5U;
    CHECK(pn532_frame_rx_plan(&prefix, 262U, &next) ==
          PN532_FRAME_NEED_MORE_DATA && next == 257U);

    bytes[4] = 0xFFU;
    CHECK(pn532_frame_rx_plan(&prefix, 6U, &next) ==
          PN532_FRAME_BUFFER_TOO_SMALL);
    bytes[5] = 0x01U;
    bytes[6] = 0x0AU;
    bytes[7] = 0xF5U;
    prefix.length = 8U;
    CHECK(pn532_frame_rx_plan(&prefix, 275U, &next) ==
          PN532_FRAME_INVALID_LENGTH);
    bytes[7] ^= 1U;
    CHECK(pn532_frame_rx_plan(&prefix, 275U, &next) ==
          PN532_FRAME_CHECKSUM_ERROR);
}

int main(void)
{
    test_complete_frames();
    test_limits_and_bad_headers();
    printf("PN532 RX planner: %u checks, %u failures\n", checks, failures);
    return failures == 0U ? 0 : 1;
}
