# DIMM Monitoring Frame Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the legacy AA55/XOR host protocol with the documented 73-byte DIMM TCP monitoring frame.

**Architecture:** Keep `CommManager` responsible for the TCP client connection and delegate byte-level frame construction to a small `CommProtocol` module. DIMM will actively report one monitoring frame per second while a live capture and TCP connection are both active. The frame timestamp is the TIMESTAMP field; DATA contains eight big-endian float32 values: temperature, humidity, pressure, r0, seeing, theta0, tau0, and a reserved zero.

**Tech Stack:** Qt 6 Core/Network, C++17, CMake, Python unittest static checks, CRC-32/ISO-HDLC.

## Global Constraints

- Use SOF `49 96 02 D2` and EOF `B6 69 FD 2E`.
- Use `DST_ADDR` `01 03 03 02 00 00` and `SRC_ADDR` `01 03 03 05 00 00` from table 9.
- Encode all multi-byte integers and DATA float32 values big-endian.
- LEN counts DST_ADDR through EOF and is `65` for the fixed 32-byte DATA frame.
- CRC-32/ISO-HDLC covers LEN through DATA, excluding SOF, CRC, and EOF.
- Preserve the current TCP client role: DIMM connects to the upper computer's TCP server.
- Do not modify camera, serial sensor, pulse-generator, or focuser transport protocols.

## File Map

- Create `src/CommProtocol.h` and `src/CommProtocol.cpp` for constants, CRC-32, big-endian packing, and the 73-byte frame builder.
- Modify `src/CommManager.h` and `src/CommManager.cpp` to send the new monitoring frame and remove legacy command/XOR framing.
- Modify `src/DIMM.cpp`, `src/DIMM.h`, `src/DIMM.Results.cpp`, and `src/DIMM.CommCamera.cpp` to start/stop autonomous reporting with live capture and send the eight selected fields.
- Modify `src/SettingsDialog.cpp` to describe the documented protocol.
- Modify `CMakeLists.txt` to explicitly include the new protocol module and add a small protocol test target.
- Create `tests/comm_protocol_test.cpp` and `tests/test_comm_protocol_static.py`.
- Update `docs/host_communication_protocol_current.md` to match the documented frame.

## Verification

- Run the protocol C++ test and confirm a 73-byte frame, fixed addresses, big-endian values, and CRC validation.
- Run the Python static test suite for the changed communication sources.
- Build the main `DIMM` target with the current source split and CMake source list.

