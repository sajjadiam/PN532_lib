/**
 * @file pn532_frame.c
 * @brief Encoding and validation of canonical PN532 host-link frames.
 *
 * @details Based on NXP UM0701-02, section 6.2.1. Uses no heap allocation.
 */

#include "pn532_frame.h"

#define PN532_FRAME_PREAMBLE       0x00U
#define PN532_FRAME_START_0       0x00U
#define PN532_FRAME_START_1       0xFFU
#define PN532_FRAME_POSTAMBLE      0x00U
#define PN532_FRAME_EXT_MARKER     0xFFU
#define PN532_FRAME_ERROR_CODE     0x7FU
#define PN532_FRAME_NORMAL_BASE   7U
#define PN532_FRAME_EXT_BASE     10U
#define PN532_FRAME_NORMAL_LIMIT 255U

/** @brief Return additive inverse modulo 256 without signed overflow. */
static uint8_t pn532_frame_complement(uint8_t value)
{
    return (uint8_t)(0U - (unsigned int)value);
}

/** @brief Return true if the two bytes sum to zero modulo 256. */
static int pn532_frame_sum_is_zero(uint8_t a, uint8_t b)
{
    return (uint8_t)((unsigned int)a + (unsigned int)b) == 0U;
}

/** @brief Detect the number of bytes in a canonical frame from its header. */
pn532_frame_result_t pn532_frame_probe(
    const pn532_frame_span_t *prefix,
    size_t *frame_size)
{
    const uint8_t *p;
    size_t length;
    size_t data_length;

    if (frame_size != NULL) {
        *frame_size = 0U;
    }
    if (prefix == NULL || frame_size == NULL ||
        (prefix->length != 0U && prefix->data == NULL)) {
        return PN532_FRAME_INVALID_ARGUMENT;
    }

    p = prefix->data;
    length = prefix->length;
    if (length < 3U) {
        if ((length >= 1U && p[0] != PN532_FRAME_PREAMBLE) ||
            (length >= 2U && p[1] != PN532_FRAME_START_0)) {
            return PN532_FRAME_INVALID_FORMAT;
        }
        return PN532_FRAME_NEED_MORE_DATA;
    }
    if (p[0] != PN532_FRAME_PREAMBLE || p[1] != PN532_FRAME_START_0 ||
        p[2] != PN532_FRAME_START_1) {
        return PN532_FRAME_INVALID_FORMAT;
    }
    if (length < 5U) {
        return PN532_FRAME_NEED_MORE_DATA;
    }

    if (p[3] == 0x00U) {
        if (p[4] != 0xFFU) {
            return PN532_FRAME_INVALID_LENGTH;
        }
        *frame_size = PN532_FRAME_CONTROL_SIZE;
        return PN532_FRAME_OK;
    }

    if (p[3] == 0xFFU && p[4] == 0x00U) {
        *frame_size = PN532_FRAME_CONTROL_SIZE;
        return PN532_FRAME_OK;
    }

    if (p[3] == PN532_FRAME_EXT_MARKER &&
        p[4] == PN532_FRAME_EXT_MARKER) {
        if (length < 8U) {
            return PN532_FRAME_NEED_MORE_DATA;
        }
        if ((uint8_t)((unsigned int)p[5] + (unsigned int)p[6] +
                      (unsigned int)p[7]) != 0U) {
            return PN532_FRAME_CHECKSUM_ERROR;
        }
        data_length = ((size_t)p[5] << 8U) | (size_t)p[6];
        if (data_length < 2U ||
            data_length > PN532_FRAME_MAX_PAYLOAD_SIZE + 1U) {
            return PN532_FRAME_INVALID_LENGTH;
        }
        *frame_size = data_length + PN532_FRAME_EXT_BASE;
        return PN532_FRAME_OK;
    }

    if (!pn532_frame_sum_is_zero(p[3], p[4])) {
        return PN532_FRAME_CHECKSUM_ERROR;
    }
    if (p[3] == 0x01U) {
        *frame_size = PN532_FRAME_ERROR_SIZE;
        return PN532_FRAME_OK;
    }
    if (p[3] < 2U) {
        return PN532_FRAME_INVALID_LENGTH;
    }

    /* LEN == 0xFF, LCS == 0x01 is a valid normal frame of 255 bytes. */
    *frame_size = (size_t)p[3] + PN532_FRAME_NORMAL_BASE;
    return PN532_FRAME_OK;
}

pn532_frame_result_t pn532_frame_encode(
    const pn532_frame_message_t *message,
    pn532_frame_buffer_t *output)
{
    uint8_t *p;
    uint8_t checksum;
    size_t data_length;
    size_t frame_length;
    size_t data_offset;
    size_t i;

    if (output != NULL) {
        output->length = 0U;
    }
    if (message == NULL || output == NULL || message->data == NULL ||
        output->data == NULL ||
        (message->tfi != PN532_FRAME_TFI_HOST &&
         message->tfi != PN532_FRAME_TFI_DEVICE)) {
        return PN532_FRAME_INVALID_ARGUMENT;
    }
    if (message->length == 0U ||
        message->length > PN532_FRAME_MAX_PAYLOAD_SIZE) {
        return PN532_FRAME_INVALID_LENGTH;
    }

    data_length = message->length + 1U;
    frame_length = data_length <= PN532_FRAME_NORMAL_LIMIT
                     ? data_length + PN532_FRAME_NORMAL_BASE
                     : data_length + PN532_FRAME_EXT_BASE;
    if (output->capacity < frame_length) {
        return PN532_FRAME_BUFFER_TOO_SMALL;
    }

    p = output->data;
    p[0] = PN532_FRAME_PREAMBLE;
    p[1] = PN532_FRAME_START_0;
    p[2] = PN532_FRAME_START_1;
    if (data_length <= PN532_FRAME_NORMAL_LIMIT) {
        p[3] = (uint8_t)data_length;
        p[4] = pn532_frame_complement(p[3]);
        data_offset = 5U;
    } else {
        p[3] = PN532_FRAME_EXT_MARKER;
        p[4] = PN532_FRAME_EXT_MARKER;
        p[5] = (uint8_t)(data_length >> 8U);
        p[6] = (uint8_t)data_length;
        p[7] = pn532_frame_complement((uint8_t)((unsigned int)p[5] +
                                                 (unsigned int)p[6]));
        data_offset = 8U;
    }

    p[data_offset] = message->tfi;
    checksum = message->tfi;
    for (i = 0U; i < message->length; ++i) {
        const uint8_t value = message->data[i];
        p[data_offset + 1U + i] = value;
        checksum = (uint8_t)((unsigned int)checksum + (unsigned int)value);
    }
    p[data_offset + data_length] = pn532_frame_complement(checksum);
    p[data_offset + data_length + 1U] = PN532_FRAME_POSTAMBLE;
    output->length = frame_length;
    return PN532_FRAME_OK;
}

pn532_frame_result_t pn532_frame_encode_control(
    pn532_frame_type_t type,
    pn532_frame_buffer_t *output)
{
    uint8_t *p;

    if (output != NULL) {
        output->length = 0U;
    }
    if (output == NULL || output->data == NULL ||
        (type != PN532_FRAME_TYPE_ACK && type != PN532_FRAME_TYPE_NACK)) {
        return PN532_FRAME_INVALID_ARGUMENT;
    }
    if (output->capacity < PN532_FRAME_CONTROL_SIZE) {
        return PN532_FRAME_BUFFER_TOO_SMALL;
    }
    p = output->data;
    p[0] = 0x00U;
    p[1] = 0x00U;
    p[2] = 0xFFU;
    p[3] = (type == PN532_FRAME_TYPE_ACK) ? 0x00U : 0xFFU;
    p[4] = (type == PN532_FRAME_TYPE_ACK) ? 0xFFU : 0x00U;
    p[5] = 0x00U;
    output->length = PN532_FRAME_CONTROL_SIZE;
    return PN532_FRAME_OK;
}

pn532_frame_result_t pn532_frame_decode(
    const pn532_frame_span_t *input,
    pn532_frame_view_t *view)
{
    pn532_frame_result_t result;
    size_t expected;
    size_t data_length;
    size_t data_offset;
    size_t i;
    uint8_t checksum;
    const uint8_t *p;

    if (view != NULL) {
        view->type = PN532_FRAME_TYPE_NORMAL;
        view->tfi = 0U;
        view->data = NULL;
        view->length = 0U;
    }
    if (input == NULL || view == NULL ||
        (input->length != 0U && input->data == NULL)) {
        return PN532_FRAME_INVALID_ARGUMENT;
    }

    result = pn532_frame_probe(input, &expected);
    if (result != PN532_FRAME_OK) {
        return result;
    }
    if (input->length < expected) {
        return PN532_FRAME_NEED_MORE_DATA;
    }
    if (input->length > expected) {
        return PN532_FRAME_INVALID_FORMAT;
    }
    p = input->data;
    if (p[expected - 1U] != PN532_FRAME_POSTAMBLE) {
        return PN532_FRAME_INVALID_FORMAT;
    }
    if (expected == PN532_FRAME_CONTROL_SIZE) {
        view->type = p[3] == 0U ? PN532_FRAME_TYPE_ACK : PN532_FRAME_TYPE_NACK;
        return PN532_FRAME_OK;
    }
    if (expected == PN532_FRAME_ERROR_SIZE && p[3] == 0x01U) {
        if (!pn532_frame_sum_is_zero(p[5], p[6])) {
            return PN532_FRAME_CHECKSUM_ERROR;
        }
        if (p[5] != PN532_FRAME_ERROR_CODE) {
            return PN532_FRAME_INVALID_FORMAT;
        }
        view->type = PN532_FRAME_TYPE_ERROR;
        return PN532_FRAME_OK;
    }

    if (p[3] == PN532_FRAME_EXT_MARKER &&
        p[4] == PN532_FRAME_EXT_MARKER) {
        data_offset = 8U;
        data_length = ((size_t)p[5] << 8U) | (size_t)p[6];
    } else {
        data_offset = 5U;
        data_length = (size_t)p[3];
    }
    if (p[data_offset] != PN532_FRAME_TFI_HOST &&
        p[data_offset] != PN532_FRAME_TFI_DEVICE) {
        return PN532_FRAME_INVALID_FORMAT;
    }

    checksum = 0U;
    for (i = 0U; i < data_length; ++i) {
        checksum = (uint8_t)((unsigned int)checksum +
                             (unsigned int)p[data_offset + i]);
    }
    if (!pn532_frame_sum_is_zero(checksum, p[data_offset + data_length])) {
        return PN532_FRAME_CHECKSUM_ERROR;
    }

    view->type = data_offset == 8U
                   ? PN532_FRAME_TYPE_EXTENDED
                   : PN532_FRAME_TYPE_NORMAL;
    view->tfi = p[data_offset];
    view->data = &p[data_offset + 1U];
    view->length = data_length - 1U;
    return PN532_FRAME_OK;
}
