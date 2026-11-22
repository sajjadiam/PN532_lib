/** @file test_pn532_tag.c
 * @brief Deterministic parser and command validation tests.
 */
#include "pn532_tag.h"
#include "pn532_cmd.h"

#include <stdio.h>
#include <string.h>

static unsigned int checks;
static unsigned int failures;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); } } while (0)

static void check_bad(const uint8_t *data, uint16_t len)
{
    pn532_tag_type_a_list_t result;
    memset(&result, 0xA5, sizeof(result));
    CHECK(pn532_tag_parse_type_a(data, len, &result) ==
          PN532_TAG_MALFORMED_RESPONSE);
    CHECK(result.count == 0U);
}

static void test_empty_and_single(void)
{
    pn532_tag_type_a_list_t result;
    const uint8_t empty[] = {0x00U};
    const uint8_t single[] = {
        0x01U, 0x01U, 0x04U, 0x00U, 0x08U,
        0x04U, 0xDEU, 0xADU, 0xBEU, 0xEFU
    };

    CHECK(pn532_tag_parse_type_a(empty, sizeof(empty), &result) ==
          PN532_TAG_OK);
    CHECK(result.count == 0U);
    CHECK(pn532_tag_parse_type_a(single, sizeof(single), &result) ==
          PN532_TAG_OK);
    CHECK(result.count == 1U);
    CHECK(result.targets[0].target_number == 1U);
    CHECK(result.targets[0].uid_length == 4U);
    CHECK(result.targets[0].uid[0] == 0xDEU);
    CHECK(result.targets[0].uid[3] == 0xEFU);
    CHECK(result.targets[0].ats_length == 0U);
    CHECK(result.targets[1].uid_length == 0U);

    CHECK(pn532_tag_parse_type_a(NULL, 1U, &result) ==
          PN532_TAG_INVALID_ARGUMENT);
    CHECK(result.count == 0U);
    CHECK(pn532_tag_parse_type_a(empty, 0U, &result) ==
          PN532_TAG_INVALID_ARGUMENT);
    CHECK(pn532_tag_parse_type_a(single, sizeof(single), NULL) ==
          PN532_TAG_INVALID_ARGUMENT);
}

static void test_dual_ats(void)
{
    pn532_tag_type_a_list_t result;
    const uint8_t dual[] = {
        0x02U,
        0x01U, 0x44U, 0x00U, 0x20U, 0x07U,
        0x04U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U,
        0x04U, 0x75U, 0x77U, 0x81U,
        0x02U, 0x00U, 0x04U, 0x00U, 0x0AU,
        0x01U, 0x02U, 0x03U, 0x04U, 0x05U,
        0x06U, 0x07U, 0x08U, 0x09U, 0x0AU
    };

    CHECK(pn532_tag_parse_type_a(dual, sizeof(dual), &result) ==
          PN532_TAG_OK);
    CHECK(result.count == 2U);
    CHECK(result.targets[0].uid_length == 7U);
    CHECK(result.targets[0].uid[6] == 0x66U);
    CHECK(result.targets[0].ats_length == 4U);
    CHECK(result.targets[1].target_number == 2U);
    CHECK(result.targets[1].uid_length == 10U);
    CHECK(result.targets[1].uid[9] == 0x0AU);
    CHECK(result.targets[1].ats_length == 0U);
}

static void test_malformed(void)
{
    const uint8_t count3[] = {0x03U};
    const uint8_t surplus[] = {0x00U, 0x44U};
    const uint8_t missing[] = {0x01U, 0x01U};
    const uint8_t wrong_target[] = {
        0x01U, 0x02U, 0x04U, 0x00U, 0x08U, 0x04U,
        0x11U, 0x22U, 0x33U, 0x44U
    };
    const uint8_t invalid_uid[] = {
        0x01U, 0x01U, 0x04U, 0x00U, 0x08U, 0x05U,
        0x11U, 0x22U, 0x33U, 0x44U, 0x55U
    };
    const uint8_t missing_ats[] = {
        0x01U, 0x01U, 0x04U, 0x00U, 0x20U, 0x04U,
        0x11U, 0x22U, 0x33U, 0x44U
    };
    const uint8_t short_ats[] = {
        0x01U, 0x01U, 0x04U, 0x00U, 0x20U, 0x04U,
        0x11U, 0x22U, 0x33U, 0x44U, 0x03U, 0x78U
    };
    const uint8_t ats_too_short[] = {
        0x01U, 0x01U, 0x04U, 0x00U, 0x20U, 0x04U,
        0x11U, 0x22U, 0x33U, 0x44U, 0x01U
    };
    check_bad(count3, sizeof(count3));
    check_bad(surplus, sizeof(surplus));
    check_bad(missing, sizeof(missing));
    check_bad(wrong_target, sizeof(wrong_target));
    check_bad(invalid_uid, sizeof(invalid_uid));
    check_bad(missing_ats, sizeof(missing_ats));
    check_bad(short_ats, sizeof(short_ats));
    check_bad(ats_too_short, sizeof(ats_too_short));
}

static void dummy_completed(void *user, const pn532_completion_t *completion)
{
    (void)user;
    (void)completion;
}

static void test_command_argument_validation(void)
{
    pn532_cmd_type_a_request_t request = {0U, 10U, dummy_completed, NULL};
    CHECK(pn532_cmd_list_type_a(NULL, NULL) ==
          PN532_STATUS_INVALID_ARGUMENT);
    CHECK(pn532_cmd_list_type_a(NULL, &request) ==
          PN532_STATUS_INVALID_ARGUMENT);
    request.max_targets = 3U;
    CHECK(pn532_cmd_list_type_a(NULL, &request) ==
          PN532_STATUS_INVALID_ARGUMENT);
    request.max_targets = 2U;
    request.response_timeout_ms = 0U;
    CHECK(pn532_cmd_list_type_a(NULL, &request) ==
          PN532_STATUS_INVALID_ARGUMENT);
    request.response_timeout_ms = 11U;
    request.on_complete = NULL;
    CHECK(pn532_cmd_list_type_a(NULL, &request) ==
          PN532_STATUS_INVALID_ARGUMENT);
    request.on_complete = dummy_completed;
    CHECK(pn532_cmd_list_type_a(NULL, &request) ==
          PN532_STATUS_INVALID_ARGUMENT);
}


static void test_exchange_reply(void)
{
    pn532_cmd_exchange_view_t view;
    const uint8_t success[] = {0x00U, 0x90U, 0x00U};
    const uint8_t chained[] = {0xC0U, 0x11U};
    const uint8_t card_error[] = {0x27U};
    const uint8_t no_payload[] = {0x00U};
    uint8_t oversized[PN532_CMD_EXCHANGE_MAX_DATA + 2U] = {0};

    CHECK(pn532_cmd_parse_exchange(success, sizeof(success), &view) ==
          PN532_CMD_EXCHANGE_OK);
    CHECK(view.error_code == 0U);
    CHECK(!view.more_information && !view.nad_present);
    CHECK(view.length == 2U && view.data == &success[1]);
    CHECK(view.data[0] == 0x90U && view.data[1] == 0x00U);
    CHECK(pn532_cmd_parse_exchange(chained, sizeof(chained), &view) ==
          PN532_CMD_EXCHANGE_OK);
    CHECK(view.more_information && view.nad_present);
    CHECK(view.length == 1U && view.data[0] == 0x11U);
    CHECK(pn532_cmd_parse_exchange(card_error, sizeof(card_error), &view) ==
          PN532_CMD_EXCHANGE_RF_ERROR);
    CHECK(view.error_code == 0x27U);
    CHECK(view.data == NULL && view.length == 0U);
    CHECK(pn532_cmd_parse_exchange(no_payload, sizeof(no_payload), &view) ==
          PN532_CMD_EXCHANGE_OK);
    CHECK(view.data == NULL && view.length == 0U);
    CHECK(pn532_cmd_parse_exchange(NULL, 0U, &view) ==
          PN532_CMD_EXCHANGE_MALFORMED);
    CHECK(view.data == NULL && view.error_code == 0U);
    CHECK(pn532_cmd_parse_exchange(oversized, sizeof(oversized), &view) ==
          PN532_CMD_EXCHANGE_MALFORMED);
    CHECK(pn532_cmd_parse_exchange(success, sizeof(success), NULL) ==
          PN532_CMD_EXCHANGE_INVALID_ARGUMENT);
}

static void test_exchange_arguments(void)
{
    const uint8_t good[] = {0x40U, 0x01U, 0x30U, 0x02U};
    const uint8_t bad_tg[] = {0x40U, 0x03U};
    const uint8_t chained_tg[] = {0x40U, 0x41U};
    const uint8_t bad_code[] = {0x41U, 0x01U};
    const pn532_command_t valid = {
        good, sizeof(good), 100U, dummy_completed, NULL
    };
    pn532_command_t cmd = valid;

    CHECK(pn532_cmd_in_data_exchange(NULL, NULL) ==
          PN532_STATUS_INVALID_ARGUMENT);
    CHECK(pn532_cmd_in_data_exchange(NULL, &valid) ==
          PN532_STATUS_INVALID_ARGUMENT);
    cmd.data = bad_tg;
    cmd.length = sizeof(bad_tg);
    CHECK(pn532_cmd_in_data_exchange(NULL, &cmd) ==
          PN532_STATUS_INVALID_ARGUMENT);
    cmd.data = chained_tg;
    CHECK(pn532_cmd_in_data_exchange(NULL, &cmd) ==
          PN532_STATUS_INVALID_ARGUMENT);
    cmd.data = bad_code;
    CHECK(pn532_cmd_in_data_exchange(NULL, &cmd) ==
          PN532_STATUS_INVALID_ARGUMENT);
    cmd.data = good;
    cmd.length = 1U;
    CHECK(pn532_cmd_in_data_exchange(NULL, &cmd) ==
          PN532_STATUS_INVALID_ARGUMENT);
    cmd.length = PN532_FRAME_MAX_PAYLOAD_SIZE + 1U;
    CHECK(pn532_cmd_in_data_exchange(NULL, &cmd) ==
          PN532_STATUS_INVALID_ARGUMENT);
}

int main(void)
{
    test_empty_and_single();
    test_dual_ats();
    test_malformed();
    test_command_argument_validation();
    test_exchange_reply();
    test_exchange_arguments();
    printf("PN532 Type-A tag: %u checks, %u failures\n", checks, failures);
    return failures == 0U ? 0 : 1;
}
