/**
 * @file pn532_frame.h
 * @brief PN532 frame encoding, inspection, and validation API.
 *
 * @details
 * This module handles the host-link frame format defined by NXP
 * PN532 User Manual UM0701-02, section 6.2.1.
 *
 * It does not access hardware, wait for responses, or schedule commands.
 * It accepts canonical PN532 frames with one 0x00 preamble and one
 * 0x00 postamble. Support for configurations that omit either byte is
 * intentionally outside the initial API contract.
 *
 * No dynamic allocation is required. Encoding writes to caller-owned
 * buffers; decoding returns a read-only view into caller-owned storage.
 */

#ifndef PN532_FRAME_H
#define PN532_FRAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of PD bytes, excluding the TFI byte. */
#define PN532_FRAME_MAX_PAYLOAD_SIZE 264U

/** @brief Maximum encoded information-frame size in bytes. */
#define PN532_FRAME_MAX_SIZE 275U

/** @brief Fixed ACK/NACK frame size in bytes. */
#define PN532_FRAME_CONTROL_SIZE 6U

/** @brief Fixed PN532 application-error frame size in bytes. */
#define PN532_FRAME_ERROR_SIZE 8U

/** @brief Frame identifier for host-to-PN532 information frames. */
#define PN532_FRAME_TFI_HOST 0xD4U

/** @brief Frame identifier for PN532-to-host information frames. */
#define PN532_FRAME_TFI_DEVICE 0xD5U

/**
 * @brief Result of a frame-format operation.
 */
typedef enum {
    PN532_FRAME_OK = 0,             /**< Operation succeeded. */
    PN532_FRAME_INVALID_ARGUMENT,   /**< Null pointer or invalid argument. */
    PN532_FRAME_NEED_MORE_DATA,     /**< Input does not yet contain enough bytes. */
    PN532_FRAME_BUFFER_TOO_SMALL,   /**< Output buffer has insufficient capacity. */
    PN532_FRAME_INVALID_FORMAT,     /**< Invalid marker, TFI, or postamble. */
    PN532_FRAME_INVALID_LENGTH,     /**< Invalid or unsupported PN532 data length. */
    PN532_FRAME_CHECKSUM_ERROR      /**< LCS or DCS verification failed. */
} pn532_frame_result_t;

/**
 * @brief Category of a complete PN532 frame.
 */
typedef enum {
    PN532_FRAME_TYPE_NORMAL = 0, /**< Normal information frame. */
    PN532_FRAME_TYPE_EXTENDED,   /**< Extended information frame. */
    PN532_FRAME_TYPE_ACK,        /**< Positive acknowledgement. */
    PN532_FRAME_TYPE_NACK,       /**< Negative acknowledgement. */
    PN532_FRAME_TYPE_ERROR       /**< Fixed application-error frame (0x7F). */
} pn532_frame_type_t;

/**
 * @brief Read-only view of contiguous bytes.
 *
 * @note The caller retains ownership of the bytes referenced by data.
 */
typedef struct {
    const uint8_t *data; /**< First byte, or NULL when length is zero. */
    size_t length;       /**< Number of available bytes. */
} pn532_frame_span_t;

/**
 * @brief Caller-owned output buffer.
 *
 * @note The implementation does not retain the buffer after returning.
 */
typedef struct {
    uint8_t *data; /**< Storage provided by the caller. */
    size_t capacity; /**< Available storage in bytes. */
    size_t length; /**< Encoded byte count; output. */
} pn532_frame_buffer_t;

/**
 * @brief Message content to encode as an information frame.
 *
 * @note data begins with PD0 (the command/response code).
 *       It does not include TFI, LCS, DCS, or framing markers.
 */
typedef struct {
    uint8_t tfi;         /**< PN532_FRAME_TFI_HOST or PN532_FRAME_TFI_DEVICE. */
    const uint8_t *data; /**< PD0 followed by optional data bytes. */
    size_t length;       /**< PD byte count, from 1 through 264. */
} pn532_frame_message_t;

/**
 * @brief Zero-copy view of a decoded frame.
 *
 * @note For NORMAL/EXTENDED frames, data points to PD0 in the source
 *       buffer, and length is the number of PD bytes (excluding TFI).
 *       For ACK, NACK, and ERROR, tfi is zero, data is NULL, and
 *       length is zero. The ERROR type identifies the fixed PN532
 *       application-error frame; its fixed code is 0x7F.
 *
 * @warning The view is invalidated if the source storage is modified
 *          or its lifetime ends.
 */
typedef struct {
    pn532_frame_type_t type; /**< Decoded frame category. */
    uint8_t tfi;             /**< TFI for information frames; otherwise zero. */
    const uint8_t *data;     /**< Read-only PD0 pointer, if applicable. */
    size_t length;           /**< PD byte count, excluding TFI. */
} pn532_frame_view_t;

/**
 * @brief Encode an information frame using the shortest valid format.
 *
 * @details
 * Uses the normal format when (1 + message->length) is at most 255
 * bytes (the extra byte is TFI); otherwise uses the extended format.
 * Includes one-byte preamble and postamble, LCS, and DCS.
 *
 * @param[in] message Information-frame content.
 * @param[in,out] output Caller-owned destination and its capacity.
 *
 * @retval PN532_FRAME_OK Frame encoded.
 * @retval PN532_FRAME_INVALID_ARGUMENT Invalid pointer or TFI.
 * @retval PN532_FRAME_INVALID_LENGTH Payload is not 1..264 bytes.
 * @retval PN532_FRAME_BUFFER_TOO_SMALL Insufficient output capacity.
 *
 * @post If output is valid, output->length is the encoded size on
 *       success, or zero on failure.
 *
 * @note Input and output storage must not overlap.
 */
pn532_frame_result_t pn532_frame_encode(
    const pn532_frame_message_t *message,
    pn532_frame_buffer_t *output
);

/**
 * @brief Encode a six-byte ACK or NACK control frame.
 *
 * @param[in] type PN532_FRAME_TYPE_ACK or PN532_FRAME_TYPE_NACK.
 * @param[in,out] output Caller-owned destination and its capacity.
 *
 * @retval PN532_FRAME_OK Frame encoded.
 * @retval PN532_FRAME_INVALID_ARGUMENT Invalid type or pointer.
 * @retval PN532_FRAME_BUFFER_TOO_SMALL Insufficient output capacity.
 *
 * @post If output is valid, output->length is six on success,
 *       or zero on failure.
 */
pn532_frame_result_t pn532_frame_encode_control(
    pn532_frame_type_t type,
    pn532_frame_buffer_t *output
);

/**
 * @brief Determine the required size of a canonical PN532 frame.
 *
 * @details
 * Examines the frame prefix, beginning at its 0x00 preamble.
 * The function checks framing markers and the available length
 * checksum (LCS). It does not check the data checksum (DCS).
 *
 * This operation supports incremental receive: once enough header
 * bytes have arrived, the total frame size becomes available even
 * when the payload has not yet arrived.
 *
 * @param[in] prefix Available leading frame bytes; data may be NULL
 *                   only when length is zero.
 * @param[out] frame_size Complete expected frame size, in bytes.
 *
 * @retval PN532_FRAME_OK Expected total size determined.
 * @retval PN532_FRAME_NEED_MORE_DATA Insufficient header bytes.
 * @retval PN532_FRAME_INVALID_ARGUMENT Invalid pointers.
 * @retval PN532_FRAME_INVALID_FORMAT Invalid framing markers.
 * @retval PN532_FRAME_INVALID_LENGTH Unsupported packet length.
 * @retval PN532_FRAME_CHECKSUM_ERROR Invalid header checksum.
 *
 * @post On success, *frame_size is the complete canonical frame size.
 *       On any other result, *frame_size is zero.
 *
 * @note The input must start at the canonical preamble. Locating
 *       a frame within a noisy HSU byte stream is the transport
 *       adapter's responsibility.
 */
pn532_frame_result_t pn532_frame_probe(
    const pn532_frame_span_t *prefix,
    size_t *frame_size
);

/**
 * @brief Decode and validate one complete canonical PN532 frame.
 *
 * @details
 * Recognizes normal, extended, ACK, NACK, and fixed application-error
 * frames. Checks the framing markers, exact length, TFI, LCS, DCS,
 * and postamble where applicable.
 *
 * @param[in] input Exact frame bytes, including preamble and postamble.
 * @param[out] view Read-only decoded frame view.
 *
 * @retval PN532_FRAME_OK Frame is valid.
 * @retval PN532_FRAME_INVALID_ARGUMENT Invalid pointer.
 * @retval PN532_FRAME_NEED_MORE_DATA Input is shorter than a frame.
 * @retval PN532_FRAME_INVALID_FORMAT Bad markers, TFI, or extra bytes.
 * @retval PN532_FRAME_INVALID_LENGTH Unsupported packet length.
 * @retval PN532_FRAME_CHECKSUM_ERROR Invalid LCS or DCS.
 *
 * @post On success, view references the input payload without copying.
 *       On failure, view is cleared if the pointer is valid.
 *
 * @warning The input buffer must outlive any use of the returned view.
 */
pn532_frame_result_t pn532_frame_decode(
    const pn532_frame_span_t *input,
    pn532_frame_view_t *view
);

#ifdef __cplusplus
}
#endif

#endif /* PN532_FRAME_H */
