/** @file pn532_cmd.c
 * @brief Nonblocking PN532 command wrappers; no protocol frame duplication.
 */
#include "pn532_cmd.h"

#include <stddef.h>
#include <stdint.h>

pn532_status_t pn532_cmd_list_type_a(
    pn532_t *device,
    const pn532_cmd_type_a_request_t *request)
{
    pn532_command_t command;

    if (request == NULL || request->max_targets < 1U ||
        request->max_targets > 2U || request->on_complete == NULL ||
        request->response_timeout_ms == 0U ||
        request->response_timeout_ms > UINT32_C(0x7FFFFFFF)) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }

    /* Core copies these stack bytes before pn532_submit() returns. */
    {
        const uint8_t data[] = {0x4AU, request->max_targets, 0x00U};
        command.data = data;
        command.length = sizeof(data);
        command.response_timeout_ms = request->response_timeout_ms;
        command.on_complete = request->on_complete;
        command.user = request->user;
        return pn532_submit(device, &command);
    }
}


pn532_status_t pn532_cmd_in_data_exchange(
    pn532_t *device,
    const pn532_command_t *command)
{
    if (command == NULL || command->data == NULL ||
        command->length < 2U ||
        command->length > PN532_FRAME_MAX_PAYLOAD_SIZE ||
        command->data[0] != PN532_CMD_IN_DATA_EXCHANGE ||
        (command->data[1] != 1U && command->data[1] != 2U)) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }
    return pn532_submit(device, command);
}

pn532_cmd_exchange_result_t pn532_cmd_parse_exchange(
    const uint8_t *response,
    uint16_t length,
    pn532_cmd_exchange_view_t *view)
{
    uint8_t status;

    if (view == NULL) {
        return PN532_CMD_EXCHANGE_INVALID_ARGUMENT;
    }
    view->error_code = 0U;
    view->more_information = false;
    view->nad_present = false;
    view->data = NULL;
    view->length = 0U;

    if (response == NULL || length == 0U ||
        length > (PN532_CMD_EXCHANGE_MAX_DATA + 1U)) {
        return PN532_CMD_EXCHANGE_MALFORMED;
    }
    status = response[0];
    view->error_code = (uint8_t)(status & 0x3FU);
    view->more_information = (status & 0x40U) != 0U;
    view->nad_present = (status & 0x80U) != 0U;
    if (view->error_code != 0U) {
        return PN532_CMD_EXCHANGE_RF_ERROR;
    }
    if (length > 1U) {
        view->data = &response[1];
        view->length = (uint16_t)(length - 1U);
    }
    return PN532_CMD_EXCHANGE_OK;
}
