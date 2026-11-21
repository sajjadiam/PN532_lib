/**
 * @file pn532_board.h
 * @brief Optional platform-independent PN532 reset and IRQ contract.
 *
 * @details No GPIO signal is mandatory for standard HSU communication.
 * The board must verify actual module wiring before enabling reset/IRQ.
 */
#ifndef PN532_BOARD_H
#define PN532_BOARD_H

#include "pn532_transport.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Optional board control hooks.
 *
 * @details Callback pointers may be NULL. The caller selects which
 * features are available for each device instance. None may block.
 *
 * reset_set requests the *logical* reset state. The board adapter
 * maps asserted=true to the correct electrical polarity. On a
 * verified direct connection to PN532 RSTPD_N, asserted is active low.
 * Do not confuse RSTPD_N (input) with RSTOUT_N (output).
 *
 * irq_take must atomically read and clear a latched device IRQ event,
 * if supported. It is called from task context, not an ISR; the
 * platform IRQ handler only latches the event. IRQ is advisory:
 * it cannot replace UART RX events and is not required for HSU.
 *
 * The caller owns the context and keeps it valid for the device lifetime.
 */
typedef struct {
    pn532_status_t (*reset_set)(
        void *context, bool asserted); /**< Optional logical reset output. */
    bool (*irq_take)(
        void *context); /**< Optional atomic take-and-clear IRQ event. */
    void *context;       /**< Shared board GPIO context. */
} pn532_board_io_t;

#ifdef __cplusplus
}
#endif

#endif /* PN532_BOARD_H */
