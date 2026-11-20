/**
 * @file test_pn532_frame.c
 * @brief Host-side unit tests for the PN532 frame implementation.
 */
#include "pn532_frame.h"
#include <stdio.h>
#include <string.h>

static unsigned int checks = 0U;
static unsigned int failures = 0U;

#define CHECK(expr) do { \
    ++checks; \
    if (!(expr)) { \
        ++failures; \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)

static void test_control_and_error(void)
{
    const uint8_t ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    const uint8_t nack[] = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};
    const uint8_t err[] = {0x00, 0x00, 0xFF, 0x01, 0xFF, 0x7F, 0x81, 0x00};
    uint8_t bytes[8] = {0};
    pn532_frame_buffer_t out = {bytes, sizeof(bytes), 99U};
    pn532_frame_span_t in = {bytes, 0U};
    pn532_frame_view_t view;
    size_t expected = 0U;

    CHECK(pn532_frame_encode_control(PN532_FRAME_TYPE_ACK, &out) == PN532_FRAME_OK);
    CHECK(out.length == sizeof(ack) && memcmp(bytes, ack, sizeof(ack)) == 0);
    in.length = out.length;
    CHECK(pn532_frame_probe(&in, &expected) == PN532_FRAME_OK && expected == 6U);
    CHECK(pn532_frame_decode(&in, &view) == PN532_FRAME_OK);
    CHECK(view.type == PN532_FRAME_TYPE_ACK && view.data == NULL);
    CHECK(pn532_frame_encode_control(PN532_FRAME_TYPE_NACK, &out) == PN532_FRAME_OK);
    CHECK(out.length == sizeof(nack) && memcmp(bytes, nack, sizeof(nack)) == 0);
    in.length = out.length;
    CHECK(pn532_frame_decode(&in, &view) == PN532_FRAME_OK);
    CHECK(view.type == PN532_FRAME_TYPE_NACK);

    memcpy(bytes, err, sizeof(err));
    in.length = sizeof(err);
    CHECK(pn532_frame_decode(&in, &view) == PN532_FRAME_OK);
    CHECK(view.type == PN532_FRAME_TYPE_ERROR && view.length == 0U);
    bytes[6] ^= 1U;
    CHECK(pn532_frame_decode(&in, &view) == PN532_FRAME_CHECKSUM_ERROR);
    bytes[5] = 0x80U;
    bytes[6] = 0x80U;
    CHECK(pn532_frame_decode(&in, &view) == PN532_FRAME_INVALID_FORMAT);
}

static void test_reference_frames(void)
{
    const uint8_t command[] = {0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00};
    const uint8_t response[] = {0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5, 0x03, 0x32,
                                0x01, 0x06, 0x07, 0xE8, 0x00};
    uint8_t bytes[PN532_FRAME_MAX_SIZE];
    const uint8_t command_code = 0x02U;
    pn532_frame_message_t msg = {PN532_FRAME_TFI_HOST, &command_code, 1U};
    pn532_frame_buffer_t out = {bytes, sizeof(bytes), 0U};
    pn532_frame_span_t in = {response, sizeof(response)};
    pn532_frame_view_t view;

    CHECK(pn532_frame_encode(&msg, &out) == PN532_FRAME_OK);
    CHECK(out.length == sizeof(command) && memcmp(bytes, command, sizeof(command)) == 0);
    CHECK(pn532_frame_decode(&in, &view) == PN532_FRAME_OK);
    CHECK(view.type == PN532_FRAME_TYPE_NORMAL);
    CHECK(view.tfi == PN532_FRAME_TFI_DEVICE && view.length == 5U);
    CHECK(view.data == &response[6] && view.data[0] == 0x03U);
}

static void test_all_lengths(void)
{
    uint8_t payload[PN532_FRAME_MAX_PAYLOAD_SIZE];
    uint8_t bytes[PN532_FRAME_MAX_SIZE + 1U];
    pn532_frame_message_t msg = {PN532_FRAME_TFI_HOST, payload, 1U};
    pn532_frame_buffer_t out = {bytes, PN532_FRAME_MAX_SIZE, 0U};
    pn532_frame_span_t in = {bytes, 0U};
    pn532_frame_view_t view;
    size_t expected = 0U;
    size_t i;

    for (i = 0U; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)i;
    }
    for (i = 1U; i <= sizeof(payload); ++i) {
        msg.length = i;
        bytes[PN532_FRAME_MAX_SIZE] = 0xA5U;
        CHECK(pn532_frame_encode(&msg, &out) == PN532_FRAME_OK);
        CHECK(out.length == (i <= 254U ? i + 8U : i + 11U));
        CHECK(bytes[PN532_FRAME_MAX_SIZE] == 0xA5U);
        in.length = out.length;
        CHECK(pn532_frame_probe(&in, &expected) == PN532_FRAME_OK);
        CHECK(expected == out.length);
        CHECK(pn532_frame_decode(&in, &view) == PN532_FRAME_OK);
        CHECK(view.type == (i <= 254U ? PN532_FRAME_TYPE_NORMAL :
                                        PN532_FRAME_TYPE_EXTENDED));
        CHECK(view.tfi == PN532_FRAME_TFI_HOST && view.length == i);
        CHECK(memcmp(view.data, payload, i) == 0);
    }
    msg.length = 254U;
    CHECK(pn532_frame_encode(&msg, &out) == PN532_FRAME_OK);
    CHECK(bytes[3] == 0xFFU && bytes[4] == 0x01U);
    CHECK(out.length == 262U);
    msg.length = 255U;
    CHECK(pn532_frame_encode(&msg, &out) == PN532_FRAME_OK);
    CHECK(bytes[3] == 0xFFU && bytes[4] == 0xFFU);
    CHECK(bytes[5] == 0x01U && bytes[6] == 0x00U && bytes[7] == 0xFFU);
    msg.length = 264U;
    CHECK(pn532_frame_encode(&msg, &out) == PN532_FRAME_OK);
    CHECK(out.length == 275U);
    msg.length = 265U;
    CHECK(pn532_frame_encode(&msg, &out) == PN532_FRAME_INVALID_LENGTH);
    CHECK(out.length == 0U);
}

static void test_partial_and_corrupt(void)
{
    uint8_t bytes[PN532_FRAME_MAX_SIZE + 1U];
    const uint8_t payload[] = {0x03, 0x32, 0x01, 0x06, 0x07};
    pn532_frame_message_t msg = {PN532_FRAME_TFI_DEVICE, payload, sizeof(payload)};
    pn532_frame_buffer_t out = {bytes, PN532_FRAME_MAX_SIZE, 0U};
    pn532_frame_span_t in = {bytes, 0U};
    pn532_frame_view_t view;
    size_t expected = 0U;
    size_t i;

    CHECK(pn532_frame_encode(&msg, &out) == PN532_FRAME_OK);
    for (i = 0U; i < out.length; ++i) {
        in.length = i;
        CHECK(pn532_frame_decode(&in, &view) == PN532_FRAME_NEED_MORE_DATA);
        CHECK(view.data == NULL && view.length == 0U);
        CHECK(pn532_frame_probe(&in, &expected) ==
              (i < 5U ? PN532_FRAME_NEED_MORE_DATA : PN532_FRAME_OK));
        CHECK(expected == (i < 5U ? 0U : out.length));
    }
    in.length = out.length;
    bytes[2] ^= 1U;
    CHECK(pn532_frame_decode(&in, &view) == PN532_FRAME_INVALID_FORMAT);
    bytes[2] ^= 1U;
    bytes[4] ^= 1U;
    CHECK(pn532_frame_decode(&in, &view) == PN532_FRAME_CHECKSUM_ERROR);
    bytes[4] ^= 1U;
    bytes[5] = 0U;
    CHECK(pn532_frame_decode(&in, &view) == PN532_FRAME_INVALID_FORMAT);
    bytes[5] = PN532_FRAME_TFI_DEVICE;
    bytes[7] ^= 1U;
    CHECK(pn532_frame_decode(&in, &view) == PN532_FRAME_CHECKSUM_ERROR);
    bytes[7] ^= 1U;
    bytes[out.length - 1U] = 0xFFU;
    CHECK(pn532_frame_decode(&in, &view) == PN532_FRAME_INVALID_FORMAT);
    bytes[out.length - 1U] = 0U;
    in.length++;
    CHECK(pn532_frame_decode(&in, &view) == PN532_FRAME_INVALID_FORMAT);

    /* Invalid extended LEN and extended LCS. */
    bytes[3] = 0xFFU; bytes[4] = 0xFFU;
    bytes[5] = 0x01U; bytes[6] = 0x0AU; bytes[7] = 0xF5U;
    in.length = 8U;
    CHECK(pn532_frame_probe(&in, &expected) == PN532_FRAME_INVALID_LENGTH);
    bytes[7] ^= 1U;
    CHECK(pn532_frame_probe(&in, &expected) == PN532_FRAME_CHECKSUM_ERROR);
}

static void test_invalid_arguments(void)
{
    uint8_t bytes[16] = {0};
    const uint8_t code = 2U;
    pn532_frame_buffer_t out = {bytes, 8U, 7U};
    pn532_frame_message_t msg = {PN532_FRAME_TFI_HOST, &code, 1U};
    pn532_frame_span_t in = {NULL, 0U};
    pn532_frame_view_t view;
    size_t expected = 99U;

    CHECK(pn532_frame_encode(NULL, &out) == PN532_FRAME_INVALID_ARGUMENT);
    CHECK(out.length == 0U);
    CHECK(pn532_frame_encode(&msg, NULL) == PN532_FRAME_INVALID_ARGUMENT);
    CHECK(pn532_frame_encode(&msg, &out) == PN532_FRAME_BUFFER_TOO_SMALL);
    CHECK(out.length == 0U);
    out.capacity = 5U;
    CHECK(pn532_frame_encode_control(PN532_FRAME_TYPE_ACK, &out) ==
          PN532_FRAME_BUFFER_TOO_SMALL);
    CHECK(pn532_frame_encode_control(PN532_FRAME_TYPE_ERROR, &out) ==
          PN532_FRAME_INVALID_ARGUMENT);
    msg.tfi = 0xFFU;
    CHECK(pn532_frame_encode(&msg, &out) == PN532_FRAME_INVALID_ARGUMENT);
    CHECK(pn532_frame_probe(&in, &expected) == PN532_FRAME_NEED_MORE_DATA);
    CHECK(expected == 0U);
    in.length = 1U;
    CHECK(pn532_frame_probe(&in, &expected) == PN532_FRAME_INVALID_ARGUMENT);
    CHECK(pn532_frame_probe(NULL, &expected) == PN532_FRAME_INVALID_ARGUMENT);
    CHECK(pn532_frame_decode(&in, &view) == PN532_FRAME_INVALID_ARGUMENT);
    CHECK(view.data == NULL && view.length == 0U);
    CHECK(pn532_frame_decode(NULL, &view) == PN532_FRAME_INVALID_ARGUMENT);
}

int main(void)
{
    test_control_and_error();
    test_reference_frames();
    test_all_lengths();
    test_partial_and_corrupt();
    test_invalid_arguments();
    printf("PN532 frame tests: %u checks, %u failures\n", checks, failures);
    return failures == 0U ? 0 : 1;
}
