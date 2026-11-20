/**
 * @file pn532_rx.h
 * @brief Transport-neutral receive-length planning for canonical PN532 frames.
 *
 * @details No I/O, polling, allocation, or resynchronization is performed.
 * Transport implementations own bus transactions, timing, and buffers.
 */
#ifndef PN532_RX_H
#define PN532_RX_H

#include "pn532_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Determine the exact number of bytes to receive next.
 *
 * @details Operates on a caller-owned prefix that begins at the PN532
 * preamble. Reading proceeds in stages: three start bytes, five normal
 * header bytes, eight extended header bytes when applicable, and the
 * remaining bytes up to the complete frame length. Header inspection
 * uses pn532_frame_probe(); completed frames must subsequently pass
 * pn532_frame_decode() to validate TFI, DCS, and postamble.
 *
 * Bus transactions and timeouts remain the transport adapter's
 * responsibility. In particular, an I2C adapter must NOT issue STOP
 * between staged reads after a ready indication: the PN532 discards
 * unread frame bytes if the transaction is terminated prematurely.
 *
 * @param[in] prefix Bytes already received, or {NULL, 0} when empty.
 * @param[in] capacity Total capacity of the caller-owned receive buffer.
 * @param[out] next_bytes Number of additional bytes to receive.
 *
 * @retval PN532_FRAME_NEED_MORE_DATA Read *next_bytes more bytes.
 * @retval PN532_FRAME_OK All bytes of one frame have been collected.
 * @retval PN532_FRAME_BUFFER_TOO_SMALL Expected frame exceeds capacity.
 * @retval PN532_FRAME_INVALID_ARGUMENT Invalid pointer or prefix length.
 * @retval PN532_FRAME_INVALID_FORMAT Invalid start or extra data.
 * @retval PN532_FRAME_INVALID_LENGTH Invalid length in frame header.
 * @retval PN532_FRAME_CHECKSUM_ERROR Invalid length checksum.
 *
 * @post *next_bytes is zero unless more bytes are required.
 * @note On HSU, the adapter must locate/synchronize the start marker
 *       before passing the prefix to this function.
 * @warning PN532_FRAME_OK does not imply a valid payload checksum.
 */
pn532_frame_result_t pn532_frame_rx_plan(
    const pn532_frame_span_t *prefix,
    size_t capacity,
    size_t *next_bytes
);

#ifdef __cplusplus
}
#endif

#endif /* PN532_RX_H */
