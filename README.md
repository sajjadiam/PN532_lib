# PN532 Library

A portable, modular Embedded C library for the **NXP PN532 NFC controller**.

## Overview

PN532_lib provides a hardware-independent interface for PN532-based embedded systems. It separates physical I/O, frame encoding, asynchronous command processing and NFC tag discovery. Multiple readers are supported by independent Core instances managed by the application.

### Design goals

- **Portability:** Pure C11 core, without dependence on any MCU vendor SDK or RTOS.
- **Modularity:** Separate frame encoding, receive planning, transport, and command-processing layers.
- **Nonblocking:** Cooperative HSU transport and command state machines.
- **Reliability:** Explicit errors, deadlines, frame validation, and controlled recovery.
- **Ownership:** Independent device instances, caller-owned buffers, and no dynamic allocation.

## Support status

| Feature | Status |
|---|---|
| Normal / extended frames, ACK / NACK, parsing | Implemented and host-tested |
| Receive-length planning | Implemented and host-tested |
| HSU/UART nonblocking transport | Implemented; host-tested, hardware validation pending |
| Command-level asynchronous Core and protocol Recovery | Implemented; host-tested |
| Nonblocking ISO14443-A discovery and target parsing | Implemented; host-tested, hardware validation pending |
| InDataExchange command and RF-status decoding | Implemented; host-tested, hardware validation pending |
| Optional RST / IRQ board integration | Contract specified; implementation pending |
| I2C and SPI | Deferred to future releases |

The command Core, Type-A discovery/parsing, and single-frame InDataExchange are host-tested; real hardware behavior and card-memory operations still require implementation and validation. User registries, server synchronization, gate logic and multi-reader policy belong to the application, not this PN532 library.

## Architecture and contracts

See [Async HSU architecture](docs/architecture.md) for transport semantics, buffer ownership and recovery rules.

`pn532_cmd_in_data_exchange()` accepts a preformatted `pn532_command_t` (`0x40`, target number `1` or `2`, up to 262 data bytes). Its completion returns `Status + DataIn` after the Core strips response code `0x41`. Call `pn532_cmd_parse_exchange()` to inspect PN532 RF errors and MI/NAD flags. Chained payload assembly, specific card commands, and authentication remain application/card-protocol responsibilities.

## Testing

Use CMake and CTest to build and run the host-side frame, transport, Core, Core+HSU integration and tag parsing tests. Hardware behavior remains to be verified on a real PN532 module.
