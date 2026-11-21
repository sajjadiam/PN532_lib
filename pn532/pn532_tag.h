/**
 * @file pn532_tag.h
 * @brief Portable parser for PN532 InListPassiveTarget Type-A results.
 *
 * @details Receives only the response bytes AFTER D5 4B, as returned by
 * pn532_completion_t. No heap, pointer retention, or registry coupling.
 */
#ifndef PN532_TAG_H
#define PN532_TAG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PN532_TAG_MAX_TARGETS 2U
#define PN532_TAG_UID_MAX 10U

/** @brief A target observed during one Type-A polling cycle. */
typedef struct {
    uint8_t target_number;              /**< PN532 Tg: 1 or 2. */
    uint8_t sens_res[2];                /**< ATQA, as received (MSB first). */
    uint8_t sel_res;                    /**< SAK, including ISO-DEP flag. */
    uint8_t uid_length;                 /**< NFCID1: 4, 7 or 10 bytes. */
    uint8_t uid[PN532_TAG_UID_MAX];     /**< Copied, not tied to Core RX. */
    uint8_t ats_length;                 /**< 0 if not ISO-DEP. */
} pn532_tag_type_a_t;

/** @brief List of independently copied Type-A targets, at most two. */
typedef struct {
    uint8_t count;
    pn532_tag_type_a_t targets[PN532_TAG_MAX_TARGETS];
} pn532_tag_type_a_list_t;

typedef enum {
    PN532_TAG_OK = 0,
    PN532_TAG_INVALID_ARGUMENT,
    PN532_TAG_MALFORMED_RESPONSE
} pn532_tag_result_t;

/**
 * @brief Parse the payload of D5 4B for BrTy=0x00 only.
 *
 * @details Validates NbTg, Tg sequence, UID length and exact payload
 * boundaries. If SEL_RES bit 0x20 is set, PN532 appends the ATS bytes:
 * the first byte is the complete ATS length, including itself. ATS is
 * validated and skipped; only its length is copied. No Card ID is
 * authenticated by this function.
 *
 * @param[in] data Payload beginning with NbTg (not D5, not 4B).
 * @param[in] length Payload byte count.
 * @param[out] output Cleared on every call; set only on full success.
 * @return PN532_TAG_OK for 0, 1, or 2 valid target records.
 */
pn532_tag_result_t pn532_tag_parse_type_a(
    const uint8_t *data,
    uint16_t length,
    pn532_tag_type_a_list_t *output
);

#ifdef __cplusplus
}
#endif

#endif /* PN532_TAG_H */
