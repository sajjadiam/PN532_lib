/**
 * @file pn532_tag.c
 * @brief Length-checked PN532 InListPassiveTarget Type-A response decoder.
 */
#include "pn532_tag.h"

#include <stddef.h>
#include <string.h>

pn532_tag_result_t pn532_tag_parse_type_a(
    const uint8_t *data,
    uint16_t length,
    pn532_tag_type_a_list_t *output)
{
    pn532_tag_type_a_list_t parsed = {0};
    uint16_t pos = 1U;
    uint8_t i;

    if (output == NULL) return PN532_TAG_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (data == NULL || length == 0U) return PN532_TAG_INVALID_ARGUMENT;

    parsed.count = data[0];
    if (parsed.count > PN532_TAG_MAX_TARGETS) {
        return PN532_TAG_MALFORMED_RESPONSE;
    }
    for (i = 0U; i < parsed.count; ++i) {
        pn532_tag_type_a_t *tag = &parsed.targets[i];
        uint8_t uid_size;

        /* Tg + SENS_RES[2] + SEL_RES + NFCIDLength. */
        if ((uint16_t)(length - pos) < 5U) {
            return PN532_TAG_MALFORMED_RESPONSE;
        }
        tag->target_number = data[pos++];
        tag->sens_res[0] = data[pos++];
        tag->sens_res[1] = data[pos++];
        tag->sel_res = data[pos++];
        uid_size = data[pos++];
        if (tag->target_number != (uint8_t)(i + 1U) ||
            (uid_size != 4U && uid_size != 7U && uid_size != 10U) ||
            (uint16_t)(length - pos) < uid_size) {
            return PN532_TAG_MALFORMED_RESPONSE;
        }

        tag->uid_length = uid_size;
        memcpy(tag->uid, &data[pos], uid_size);
        pos = (uint16_t)(pos + uid_size);

        if ((tag->sel_res & 0x20U) != 0U) {
            uint8_t ats_length;
            if (pos >= length) return PN532_TAG_MALFORMED_RESPONSE;
            ats_length = data[pos];
            /* ATS length includes its own TL byte; T0 is mandatory. */
            if (ats_length < 2U ||
                (uint16_t)(length - pos) < ats_length) {
                return PN532_TAG_MALFORMED_RESPONSE;
            }
            tag->ats_length = ats_length;
            pos = (uint16_t)(pos + ats_length);
        }
    }
    if (pos != length) return PN532_TAG_MALFORMED_RESPONSE;
    *output = parsed;
    return PN532_TAG_OK;
}
