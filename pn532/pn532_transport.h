/**
 * @file pn532_transport.h
 * @brief Platform-independent PN532 transport interface.
 *
 * @details
 * Defines a synchronous, frame-oriented interface for PN532 HSU, I2C,
 * and SPI transport implementations.
 *
 * Transport adapters handle interface-specific I/O and return raw PN532
 * frames. The PN532 core handles command sequencing, ACK/NACK handling,
 * checksum validation, and response processing.
 *
 * Buffers are owned by the caller and must remain valid until the
 * corresponding synchronous operation returns.
 */

#ifndef PN532_TRANSPORT_H
#define PN532_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Transport operation status codes.
 */
typedef enum {
    PN532_STATUS_OK = 0,           /**< Operation completed successfully. */
    PN532_STATUS_INVALID_ARGUMENT, /**< Invalid descriptor or parameter. */
    PN532_STATUS_TIMEOUT,          /**< Operation did not finish in time. */
    PN532_STATUS_IO_ERROR,         /**< Platform communication failure. */
    PN532_STATUS_BUFFER_TOO_SMALL, /**< Receive buffer cannot hold the frame. */
    PN532_STATUS_FRAME_ERROR       /**< Frame boundary could not be recovered. */
} pn532_status_t;

/**
 * @brief Transmit-frame descriptor.
 *
 * @note The caller retains ownership of @ref data. The transport must
 *       not modify or retain the buffer after returning.
 */
typedef struct {
    const uint8_t *data; /**< Complete PN532 frame to transmit. */
    size_t length;       /**< Number of frame bytes. */
    uint32_t timeout_ms; /**< Maximum operation duration, in milliseconds. */
} pn532_tx_frame_t;

/**
 * @brief Receive-frame descriptor.
 *
 * @note The caller owns @ref data and specifies its available capacity.
 */
typedef struct {
    uint8_t *data;       /**< Destination for the complete PN532 frame. */
    size_t capacity;     /**< Capacity of @ref data, in bytes. */
    size_t length;       /**< Received frame length; output. */
    uint32_t timeout_ms; /**< Maximum operation duration, in milliseconds. */
} pn532_rx_frame_t;

/**
 * @brief Write a complete PN532 frame.
 *
 * @param[in] context Transport-specific instance context.
 * @param[in] frame   Transmit frame descriptor.
 *
 * @retval PN532_STATUS_OK Complete frame transmitted.
 * @retval PN532_STATUS_INVALID_ARGUMENT Invalid descriptor or parameters.
 * @retval PN532_STATUS_TIMEOUT Operation timed out.
 * @retval PN532_STATUS_IO_ERROR Hardware communication failure.
 *
 * @pre frame and frame->data are valid.
 * @pre frame->length and frame->timeout_ms are nonzero.
 *
 * @note Interface-specific signaling is handled by the adapter, without
 *       modifying the caller's frame buffer.
 */
typedef pn532_status_t (*pn532_write_frame_fn)(
    void *context,
    const pn532_tx_frame_t *frame
);

/**
 * @brief Read a complete PN532 frame.
 *
 * @param[in]     context Transport-specific instance context.
 * @param[in,out] frame   Receive frame descriptor.
 *
 * @retval PN532_STATUS_OK Complete frame received.
 * @retval PN532_STATUS_INVALID_ARGUMENT Invalid descriptor or parameters.
 * @retval PN532_STATUS_TIMEOUT Operation timed out.
 * @retval PN532_STATUS_IO_ERROR Hardware communication failure.
 * @retval PN532_STATUS_BUFFER_TOO_SMALL Insufficient receive capacity.
 * @retval PN532_STATUS_FRAME_ERROR Frame boundaries could not be recovered.
 *
 * @pre frame and frame->data are valid.
 * @pre frame->capacity and frame->timeout_ms are nonzero.
 *
 * @post On success, frame->length is the complete received frame length.
 * @post On failure, frame->length is zero for a valid descriptor.
 *
 * @note Interface-specific status bytes and opcodes are excluded from
 *       the returned PN532 frame.
 *
 * @warning If the receive buffer is too small, the adapter must discard
 *          the remainder or resynchronize before the next read.
 */
typedef pn532_status_t (*pn532_read_frame_fn)(
    void *context,
    pn532_rx_frame_t *frame
);

/**
 * @brief Operations implemented by a PN532 transport adapter.
 */
typedef struct {
    pn532_write_frame_fn write_frame; /**< Synchronous frame transmission. */
    pn532_read_frame_fn read_frame;   /**< Synchronous frame reception. */
} pn532_transport_ops_t;

/**
 * @brief Transport binding for a single PN532 instance.
 *
 * @note Each device may have its own context and operations table.
 *       Calls on the same instance must be serialized by the caller.
 */
typedef struct {
    const pn532_transport_ops_t *ops; /**< Adapter operations. */
    void *context;                    /**< Adapter-specific instance state. */
} pn532_transport_t;

#ifdef __cplusplus
}
#endif

#endif /* PN532_TRANSPORT_H */
