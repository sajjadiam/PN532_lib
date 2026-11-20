# PN532 Library

A portable and modular Embedded C library for the **NXP PN532 NFC controller**.

## Overview

PN532_lib aims to provide a hardware-independent software interface for integrating the PN532 NFC controller into microcontroller-based and embedded systems.

The library is designed around a layered architecture that separates hardware communication, PN532 command processing, and high-level NFC operations.

### Design Goals

- **Portability:** Independent of specific microcontrollers, SDKs, and hardware abstraction layers.
- **Modularity:** Clear separation between transport drivers, protocol handling, and NFC functionality.
- **Flexibility:** Designed to accommodate UART (HSU), I²C, and SPI host interfaces.
- **Reliability:** Explicit error handling, communication timeouts, and frame validation.
- **Maintainability:** A clean, documented, and testable C codebase.

### Project Status

Currently in the design phase. Features and supported interfaces will be implemented and verified incrementally.
