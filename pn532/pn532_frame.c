/**
 * @file pn532_frame.c
 * @brief Encoding and validation of canonical PN532 host-link frames.
 *
 * @details Implements NXP UM0701-02, section 6.2.1, without heap allocation.
 */

#include "pn532_frame.h"

#define PN532_FRAME_NORMAL_OVERHEAD 7U
#define PN532_FRAME_EXT_OVERHEAD   10U
#define PN532_FRAME_NORMAL_LIMIT  255U
#define PN532_FRAME_ERROR_CODE    0x7FU

/** @brief Return the additive inverse of a byte modulo 256. */
static uint8_t pn532_frame_negate(uint8_t value)
{
    return (uint8_t)(0U - (unsigned int)value);
}

/** @brief Check whether two checksum bytes sum to zero modulo 256. */
static int pn532_frame_sum_is_zero(uint8_t a, uint8_t b)
{
    return (uint8_t)((unsigned int)a + (unsigned int)b) == 0U;
}

/** @brief Validate the available part of the canonical 00 00 FF start. */
static pn532_frame_result_t pn532_frame_check_start(
    const pn532_frame_span_t *input)
{
    const uint8_t *data = input->data;
    const uint16_t count = input->length;

    if ((count > 0U && data[0] != 0x00U) ||
        (count > 1U && data[1] != 0x00U) ||
        (count > 2U && data[2] != 0xFFU)) {
        return PN532_FRAME_INVALID_FORMAT;
    }
    return count < 3U ? PN532_FRAME_NEED_MORE_DATA : PN532_FRAME_OK;
}

/** @brief Probe the length of a frame using the extended length header. */
static pn532_frame_result_t pn532_frame_probe_extended(
    const pn532_frame_span_t *input,
    uint16_t *frame_size)
{
    const uint8_t *data = input->data;
    uint16_t length;

    if (input->length < 8U) {
        return PN532_FRAME_NEED_MORE_DATA;
    }
    if ((uint8_t)((unsigned int)data[5] + data[6] + data[7]) != 0U) {
        return PN532_FRAME_CHECKSUM_ERROR;
    }
    length = ((uint16_t)data[5] << 8U) | (uint16_t)data[6];
    if (length < 2U || length > PN532_FRAME_MAX_PAYLOAD_SIZE + 1U) {
        return PN532_FRAME_INVALID_LENGTH;
    }
    *frame_size = (uint16_t)(length + PN532_FRAME_EXT_OVERHEAD);
    return PN532_FRAME_OK;
}

/** @brief Probe normal information, ACK/NACK, and error frames. */
static pn532_frame_result_t pn532_frame_probe_short(
    const uint8_t *data,
    uint16_t *frame_size)
{
    if (data[3] == 0x00U) {
        if (data[4] != 0xFFU) {
            return PN532_FRAME_INVALID_LENGTH;
        }
        *frame_size = PN532_FRAME_CONTROL_SIZE;
    } else if (data[3] == 0xFFU && data[4] == 0x00U) {
        *frame_size = PN532_FRAME_CONTROL_SIZE;
    } else {
        if (!pn532_frame_sum_is_zero(data[3], data[4])) {
            return PN532_FRAME_CHECKSUM_ERROR;
        }
        if (data[3] < 2U && data[3] != 1U) {
            return PN532_FRAME_INVALID_LENGTH;
        }
        /* Normal LEN=255/LCS=01 is not an extended header. */
        *frame_size = data[3] == 1U
                        ? PN532_FRAME_ERROR_SIZE
                        : (uint16_t)(data[3] + PN532_FRAME_NORMAL_OVERHEAD);
    }
    return PN532_FRAME_OK;
}

pn532_frame_result_t pn532_frame_probe(
    const pn532_frame_span_t *prefix,
    uint16_t *frame_size)
{
    pn532_frame_result_t result;

    if (frame_size != NULL) {
        *frame_size = 0U;
    }
    if (prefix == NULL || frame_size == NULL ||
        (prefix->length != 0U && prefix->data == NULL)) {
        return PN532_FRAME_INVALID_ARGUMENT;
    }
    result = pn532_frame_check_start(prefix);
    if (result != PN532_FRAME_OK) {
        return result;
    }
    if (prefix->length < 5U) {
        return PN532_FRAME_NEED_MORE_DATA;
    }
    if (prefix->data[3] == 0xFFU && prefix->data[4] == 0xFFU) {
        return pn532_frame_probe_extended(prefix, frame_size);
    }
    return pn532_frame_probe_short(prefix->data, frame_size);
}

/** @brief Emit the PN532 header and return the TFI offset. */
static uint16_t pn532_frame_write_header(uint8_t *data, uint16_t length)
{
    data[0] = 0x00U;
    data[1] = 0x00U;
    data[2] = 0xFFU;
    if (length <= PN532_FRAME_NORMAL_LIMIT) {
        data[3] = (uint8_t)length;
        data[4] = pn532_frame_negate(data[3]);
        return 5U;
    }
    data[3] = 0xFFU;
    data[4] = 0xFFU;
    data[5] = (uint8_t)(length >> 8U);
    data[6] = (uint8_t)length;
    data[7] = pn532_frame_negate((uint8_t)(data[5] + data[6]));
    return 8U;
}

/** @brief Emit TFI, PD bytes, DCS, and postamble. */
static void pn532_frame_write_data(
    uint8_t *data,
    uint16_t offset,
    const pn532_frame_message_t *message)
{
    uint8_t checksum = message->tfi;
    uint16_t index;

    data[offset] = message->tfi;
    for (index = 0U; index < message->length; ++index) {
        const uint8_t value = message->data[index];
        data[offset + index + 1U] = value;
        checksum = (uint8_t)((unsigned int)checksum + value);
    }
    data[offset + message->length + 1U] = pn532_frame_negate(checksum);
    data[offset + message->length + 2U] = 0x00U;
}

pn532_frame_result_t pn532_frame_encode(
    const pn532_frame_message_t *message,
    pn532_frame_buffer_t *output)
{
    uint16_t length;
    uint16_t overhead;
    uint16_t offset;

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
    length = (uint16_t)(message->length + 1U);
    overhead = length <= PN532_FRAME_NORMAL_LIMIT
                   ? PN532_FRAME_NORMAL_OVERHEAD : PN532_FRAME_EXT_OVERHEAD;
    if (output->capacity < length + overhead) {
        return PN532_FRAME_BUFFER_TOO_SMALL;
    }
    offset = pn532_frame_write_header(output->data, length);
    pn532_frame_write_data(output->data, offset, message);
    output->length = (uint16_t)(length + overhead);
    return PN532_FRAME_OK;
}

pn532_frame_result_t pn532_frame_encode_control(
    pn532_frame_type_t type,
    pn532_frame_buffer_t *output)
{
    uint8_t *data;

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
    data = output->data;
    data[0] = 0x00U;
    data[1] = 0x00U;
    data[2] = 0xFFU;
    data[3] = type == PN532_FRAME_TYPE_ACK ? 0x00U : 0xFFU;
    data[4] = type == PN532_FRAME_TYPE_ACK ? 0xFFU : 0x00U;
    data[5] = 0x00U;
    output->length = PN532_FRAME_CONTROL_SIZE;
    return PN532_FRAME_OK;
}

/** @brief Decode the fixed application-error frame (no TFI). */
static pn532_frame_result_t pn532_frame_decode_error(
    const uint8_t *data,
    pn532_frame_view_t *view)
{
    if (!pn532_frame_sum_is_zero(data[5], data[6])) {
        return PN532_FRAME_CHECKSUM_ERROR;
    }
    if (data[5] != PN532_FRAME_ERROR_CODE) {
        return PN532_FRAME_INVALID_FORMAT;
    }
    view->type = PN532_FRAME_TYPE_ERROR;
    return PN532_FRAME_OK;
}

/** @brief Decode normal/extended information fields without copying data. */
static pn532_frame_result_t pn532_frame_decode_data(
    const uint8_t *data,
    pn532_frame_view_t *view)
{
    const int extended = data[3] == 0xFFU && data[4] == 0xFFU;
    const uint16_t offset = extended ? 8U : 5U;
    const uint16_t length = extended
                            ? ((uint16_t)data[5] << 8U) | (uint16_t)data[6]
                            : (uint16_t)data[3];
    uint8_t checksum = 0U;
    uint16_t index;

    if (data[offset] != PN532_FRAME_TFI_HOST &&
        data[offset] != PN532_FRAME_TFI_DEVICE) {
        return PN532_FRAME_INVALID_FORMAT;
    }
    for (index = 0U; index < length; ++index) {
        checksum = (uint8_t)((unsigned int)checksum + data[offset + index]);
    }
    if (!pn532_frame_sum_is_zero(checksum, data[offset + length])) {
        return PN532_FRAME_CHECKSUM_ERROR;
    }
    view->type = extended ? PN532_FRAME_TYPE_EXTENDED : PN532_FRAME_TYPE_NORMAL;
    view->tfi = data[offset];
    view->data = data + offset + 1U;
    view->length = (uint16_t)(length - 1U);
    return PN532_FRAME_OK;
}

pn532_frame_result_t pn532_frame_decode(
    const pn532_frame_span_t *input,
    pn532_frame_view_t *view)
{
    pn532_frame_result_t result;
    uint16_t expected;
    const uint8_t *data;

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
    if (input->length > expected || input->data[expected - 1U] != 0x00U) {
        return PN532_FRAME_INVALID_FORMAT;
    }
    data = input->data;
    if (expected == PN532_FRAME_CONTROL_SIZE) {
        view->type = data[3] == 0x00U
                         ? PN532_FRAME_TYPE_ACK : PN532_FRAME_TYPE_NACK;
        return PN532_FRAME_OK;
    }
    if (expected == PN532_FRAME_ERROR_SIZE && data[3] == 0x01U) {
        return pn532_frame_decode_error(data, view);
    }
    return pn532_frame_decode_data(data, view);
}
