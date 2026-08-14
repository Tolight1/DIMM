# DIMM TCP Monitoring Protocol

This document records the current DIMM monitoring frame used between the DIMM computer and the upstream host. The host sends control commands; DIMM sends one monitoring frame periodically while live capture is running and the TCP connection is established.

## Network Defaults

- DIMM computer: `169.254.100.1`
- Upstream host: configure a static address in the same `169.254.0.0/16` network, for example `169.254.100.2`
- Subnet mask: `255.255.0.0`
- Default gateway: none required for the direct local link
- TCP port: `5000`
- Physical connection: the upstream host uses the spare RJ45 port on the PoE switch

The camera addresses shown in the camera utility (`169.254.103.41` and `169.254.9.4`) are camera-device addresses. They are separate from the DIMM computer address and must not be reused.

## Frame Layout

All multi-byte integers and IEEE-754 `float32` values are encoded in network byte order (big-endian). The total frame length is 93 bytes.

| Offset | Size | Field | Value / meaning |
| ---: | ---: | --- | --- |
| 0 | 4 | `SOF` | `49 96 02 D2` |
| 4 | 4 | `LEN` | `00 00 00 55`; bytes from `DST_ADDR` through `EOF`, excluding `SOF` and `LEN` |
| 8 | 6 | `DST_ADDR` | `01 03 03 02 00 00`; from table 9 |
| 14 | 6 | `SRC_ADDR` | `01 03 03 05 00 00`; DIMM monitoring endpoint |
| 20 | 1 | `MSG_TYPE` | `07`; status/measurement active report |
| 21 | 4 | `SEQ` | uint32 cycle counter, big-endian |
| 25 | 8 | `TIMESTAMP` | uint64 UTC milliseconds since 1970-01-01, big-endian |
| 33 | 52 | `DATA` | twelve big-endian `float32` values followed by one big-endian `uint32` status bitmask |
| 85 | 4 | `CRC` | CRC-32/ISO-HDLC, big-endian; input is bytes 4 through 84 |
| 89 | 4 | `EOF` | `B6 69 FD 2E` |

`LEN` is 85 (`0x55`), and does not include `SOF` or the four-byte `LEN` field itself.

## DATA Layout

The 52-byte DATA area follows the current agreed order below. Each numeric measurement occupies four bytes as an IEEE-754 `float32`; the final status field is a four-byte `uint32`. Invalid floating-point values are transmitted as IEEE-754 `NaN`.

| DATA offset | Size | Field | Source |
| ---: | ---: | --- | --- |
| 0 | 4 | temperature | temperature sensor, degrees Celsius |
| 4 | 4 | humidity | humidity sensor, relative humidity in percent |
| 8 | 4 | pressure | pressure sensor, hPa |
| 12 | 4 | `r0` | latest atmospheric measurement |
| 16 | 4 | `seeing` | latest atmospheric measurement |
| 20 | 4 | `theta0` | latest atmospheric measurement |
| 24 | 4 | `tau0` | latest atmospheric measurement |
| 28 | 4 | camera A peak brightness | camera A peak star brightness |
| 32 | 4 | camera B peak brightness | camera B peak star brightness |
| 36 | 4 | camera A exposure time | microseconds |
| 40 | 4 | camera B exposure time | microseconds |
| 44 | 4 | frame rate | shared frame rate for the two cameras, Hz |
| 48 | 4 | device status | big-endian `uint32` bitmask, see below |

### Device Status Bitmask

The status value is a snapshot for the current monitoring report. A bit is set when the corresponding abnormal condition is present and is cleared after the condition recovers. `0x00000000` means normal.

| Bit mask | Meaning |
| ---: | --- |
| `0x00000001` | camera A connection abnormal |
| `0x00000002` | camera B connection abnormal |
| `0x00000004` | trigger or pulse generator abnormal |
| `0x00000008` | camera A capture timeout or frame loss |
| `0x00000010` | camera B capture timeout or frame loss |
| `0x00000020` | camera A star not found |
| `0x00000040` | camera B star not found |
| `0x00000080` | camera A star brightness insufficient |
| `0x00000100` | camera B star brightness insufficient |
| `0x00000200` | environment sensor abnormal |
| `0x00000400` | exposure setting abnormal |
| `0x00000800` | frame rate or dual-camera consistency abnormal |
| `0x00001000` | atmospheric measurement invalid or stale |
| `0x00002000` | local measurement file cannot be written |

The current implementation uses a missing valid centroid as the “star not found” condition. A valid centroid whose peak brightness is below the configured auto-exposure low target is reported as “brightness insufficient”.

## CRC

Use the standard CRC-32/ISO-HDLC parameters:

- polynomial: `0x04C11DB7` (reflected implementation commonly uses `0xEDB88320`)
- initial value: `0xFFFFFFFF`
- input and output reflection: enabled
- final XOR: `0xFFFFFFFF`
- transmit the resulting uint32 in big-endian order

## Host Interaction

The current DIMM implementation is a TCP client. It connects to the configured host address and port, then writes monitoring frames. The host should run a TCP server on port `5000` and accept the connection. TCP is a byte stream, so the host parser must accumulate bytes and resynchronize using `SOF`, `LEN`, and `EOF`; one `read()` call is not guaranteed to contain one complete frame.

The current implementation does not yet parse host-to-DIMM command frames. Start/stop capture remains a local UI operation, while monitoring transmission is enabled during live capture after the TCP connection is established.
