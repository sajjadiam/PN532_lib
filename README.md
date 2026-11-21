# PN532 Library

A portable, modular Embedded C library for the **NXP PN532 NFC controller**.

## Overview

PN532_lib aims to provide a hardware-independent interface for PN532-based embedded systems. The design separates physical I/O, PN532 frame encoding, and future command/NFC APIs.

### Design goals

- **Portability:** Pure C11 core, without dependence on any MCU vendor SDK or RTOS.
- **Modularity:** Separate frame encoding, receive planning, transport, and command-processing layers.
- **Nonblocking:** An event-driven HSU interface is the target for use in larger real-time systems.
- **Reliability:** Explicit errors, deadlines, frame validation, and controlled recovery.
- **Ownership:** Independent device instances, caller-owned buffers, and no dynamic allocation.

## Support status

| Feature | Status |
|---|---|
| Normal / extended frames, ACK / NACK, parsing | Implemented and host-tested |
| Receive-length planning | Implemented and host-tested |
| HSU/UART nonblocking transport | Implemented; host-tested, hardware validation pending |
| Command-level asynchronous core | Planned |
| Optional RST / IRQ board integration | Contract specified; implementation pending |
| I2C and SPI | Deferred to future releases |

Only the nonblocking HSU transport is maintained. The command-level Core is not yet implemented; do not treat this as a complete NFC application library.

## Architecture and contracts

See [Async HSU architecture](docs/architecture.md) for the intended transport semantics, bounded processing, buffer ownership, cancel/recovery rules, and optional reset/IRQ wiring.

## Testing

Use CMake and CTest to build and run the host-side frame/transport tests. Hardware behavior remains to be verified on a real PN532 module.
