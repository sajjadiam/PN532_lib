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
    const uint8_t command_data[] = {0x4AU, 0x02U, 0x00U};
    pn532_command_t command;

    if (request == NULL || request->max_targets < 1U ||
        request->max_targets > 2U || request->on_complete == NULL ||
        request->response_timeout_ms == 0U ||
        request->response_timeout_ms > UINT32_C(0x7FFFFFFF)) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }

    command_data[0]; /* Command bytes are copied by pn532_submit(). */
    command.data = command_data;
    command.length = sizeof(command_data);
    command.response_timeout_ms = request->response_timeout_ms;
    command.on_complete = request->on_complete;
    command.user = request->user;

    /*
     * The first call accepts a transient 3-byte stack command: pn532_submit()
     * encodes it into the Core's caller-owned TX storage synchronously.
     */
    {
        const uint8_t dynamic_data[] = {0x4AU, request->max_targets, 0x00U};
        command.data = dynamic_data;
        return pn532_submit(device, &command);
    }
}
