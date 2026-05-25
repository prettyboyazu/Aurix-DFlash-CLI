"""
TC3xx CAN Bootstrap Loader (CAN-BSL) Protocol Implementation
=============================================================
Target:   Infineon AURIX TC3xx (TC33x / TC36x / TC37x / TC39x)
Interface: PCAN-USB via python-can
Reference: TC3xx Family User's Manual Part 1, Section 3.1.3 "Bootstrap Loaders"

Protocol Overview (Classical CAN, 11-bit standard ID):
  Phase 1 - Baud Rate Initialization:
    Host → MCU : CAN ID=0x555, DLC=8, data=[0x55,0x55,ACKID,0x55,0x55,0x55,0x55,0x55]
    MCU  → Host: CAN ID=ACKID, DLC=0 (ACK / echo)

  Phase 2 - Command / Response:
    Host → MCU : Command frame  (CAN ID=0x555)
    MCU  → Host: Response frame (CAN ID=ACKID)

NOTE: All UM-reference values are marked with  # UM §3.1.3
      Verify them against the actual PDF before running on hardware.
"""

from __future__ import annotations

import struct
import time
import logging
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional

import can  # pip install python-can

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Protocol Constants  (verify against TC3xx UM §3.1.3)
# ---------------------------------------------------------------------------

# CAN ID used by Host → MCU for all frames
BSL_CMD_ID: int = 0x555          # UM §3.1.3 – fixed command CAN ID

# CAN ID used by MCU → Host (must match ACKID in init frame)
BSL_ACK_ID: int = 0x554          # configurable; pick any valid 11-bit ID != 0x555

# Initialization frame: two 0x55 sync bytes + ACKID byte + padding
BSL_INIT_SYNC: bytes = bytes([0x55, 0x55])   # UM §3.1.3 – baud-rate pattern

# Timing
BSL_INIT_RETRIES:    int   = 5
BSL_INIT_INTERVAL_S: float = 0.050    # 50 ms between init frames
BSL_CMD_TIMEOUT_S:   float = 2.0      # command response timeout
BSL_ERASE_TIMEOUT_S: float = 30.0     # erase can take longer
BSL_WRITE_CHUNK:     int   = 256      # bytes per write transaction (PFlash page = 32, DFlash = 8)


# ---------------------------------------------------------------------------
# Command Opcodes  (verify against TC3xx UM §3.1.3 Table – "Command Frames")
# ---------------------------------------------------------------------------
class Cmd(IntEnum):
    # NOTE: All opcode values below are PLACEHOLDER.
    # Read TC3xx UM §3.1.3, replace with actual values.
    GET_DEVICE_ID   = 0x00   # TODO: UM §3.1.3
    ERASE_SECTOR    = 0x01   # TODO: UM §3.1.3
    WRITE_DATA      = 0x02   # TODO: UM §3.1.3
    VERIFY_DATA     = 0x03   # TODO: UM §3.1.3
    EXECUTE         = 0x04   # TODO: UM §3.1.3
    GET_STATUS      = 0x05   # TODO: UM §3.1.3


# ---------------------------------------------------------------------------
# Response / Status Codes  (verify against TC3xx UM §3.1.3)
# ---------------------------------------------------------------------------
class Status(IntEnum):
    ACK    = 0x00   # TODO: UM §3.1.3
    NACK   = 0x01   # TODO: UM §3.1.3
    BUSY   = 0x02   # TODO: UM §3.1.3
    ERROR  = 0xFF   # generic error placeholder


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------
@dataclass
class DeviceInfo:
    """Filled by GET_DEVICE_ID response."""
    jtag_id:  int = 0
    flash_kb: int = 0
    raw:      bytes = b""

    def __str__(self) -> str:
        return (f"JTAG-ID=0x{self.jtag_id:08X}  "
                f"Flash={self.flash_kb} KB")


# ---------------------------------------------------------------------------
# Exceptions
# ---------------------------------------------------------------------------
class BslError(Exception):
    """Generic BSL protocol error."""

class BslTimeout(BslError):
    """No response received within timeout."""

class BslNack(BslError):
    """MCU returned NACK."""


# ---------------------------------------------------------------------------
# Low-level CAN transport
# ---------------------------------------------------------------------------
class _CanTransport:
    """Thin wrapper around python-can Bus for BSL traffic."""

    def __init__(self, interface: str, channel: str, bitrate: int):
        self._bus = can.interface.Bus(
            interface=interface,
            channel=channel,
            bitrate=bitrate,
        )
        log.debug("CAN bus opened: %s %s @ %d bps", interface, channel, bitrate)

    def send(self, can_id: int, data: bytes) -> None:
        msg = can.Message(
            arbitration_id=can_id,
            data=data,
            is_extended_id=False,
        )
        self._bus.send(msg)
        log.debug("TX  ID=0x%03X  DLC=%d  %s", can_id, len(data), data.hex(" "))

    def recv(self, can_id: int, timeout_s: float) -> bytes:
        """
        Wait for a frame from `can_id`.
        Ignores frames with other IDs (e.g., own echo from PCAN loopback).
        """
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            msg = self._bus.recv(timeout=max(0.0, remaining))
            if msg is None:
                break
            if msg.arbitration_id == can_id:
                log.debug("RX  ID=0x%03X  DLC=%d  %s",
                          msg.arbitration_id, len(msg.data), bytes(msg.data).hex(" "))
                return bytes(msg.data)
        raise BslTimeout(f"No response from ID=0x{can_id:03X} within {timeout_s}s")

    def flush(self) -> None:
        """Drain pending frames (e.g., loopback echo)."""
        while self._bus.recv(timeout=0.01) is not None:
            pass

    def close(self) -> None:
        self._bus.shutdown()


# ---------------------------------------------------------------------------
# BSL Protocol Layer
# ---------------------------------------------------------------------------
class TC3xxCanBsl:
    """
    Host-side implementation of the AURIX TC3xx CAN Bootstrap Loader protocol.

    Usage:
        bsl = TC3xxCanBsl(interface='pcan', channel='PCAN_USBBUS1', bitrate=500_000)
        bsl.connect()
        bsl.erase(addr=0xAF000000, num_sectors=32)
        bsl.flash_hex("firmware.hex")
        bsl.execute(0xA0000000)
        bsl.close()
    """

    def __init__(
        self,
        interface: str = "pcan",
        channel:   str = "PCAN_USBBUS1",
        bitrate:   int = 500_000,
        ack_id:    int = BSL_ACK_ID,
    ):
        self._transport = _CanTransport(interface, channel, bitrate)
        self._ack_id    = ack_id
        self._connected = False

    # ------------------------------------------------------------------
    # Phase 1: Baud Rate Initialization
    # ------------------------------------------------------------------
    def connect(self) -> DeviceInfo:
        """
        Send initialization frames until MCU acknowledges.
        Returns DeviceInfo on success.

        Init frame format (TC3xx UM §3.1.3):
          ID  = 0x555
          DLC = 8
          DB0 = 0x55  (baud-rate sync pattern)
          DB1 = 0x55
          DB2 = ACKID (low byte of the CAN ID the MCU shall use for responses)
          DB3 = 0x55  (padding / additional sync)
          DB4 = 0x55
          DB5 = 0x55
          DB6 = 0x55
          DB7 = 0x55

        NOTE: The exact byte layout of DB2..DB7 must be verified against UM §3.1.3.
              The ACKID encoding (1 byte vs 2 bytes) must also be confirmed.
        """
        ack_id_byte = self._ack_id & 0xFF  # UM §3.1.3 – confirm ACKID encoding

        init_frame = bytes([
            0x55,           # DB0 – sync                      UM §3.1.3
            0x55,           # DB1 – sync                      UM §3.1.3
            ack_id_byte,    # DB2 – ACKID (MCU response ID)   UM §3.1.3
            0x55,           # DB3 – padding / sync            UM §3.1.3
            0x55,           # DB4 – padding                   UM §3.1.3
            0x55,           # DB5 – padding                   UM §3.1.3
            0x55,           # DB6 – padding                   UM §3.1.3
            0x55,           # DB7 – padding                   UM §3.1.3
        ])

        self._transport.flush()

        for attempt in range(BSL_INIT_RETRIES):
            log.info("BSL init attempt %d/%d …", attempt + 1, BSL_INIT_RETRIES)
            self._transport.send(BSL_CMD_ID, init_frame)
            try:
                # MCU ACKs with DLC=0 (empty frame) on ACKID  – UM §3.1.3
                resp = self._transport.recv(self._ack_id, timeout_s=0.5)
                if len(resp) == 0:          # DLC=0 → ACK confirmed     UM §3.1.3
                    log.info("BSL baud-rate sync confirmed")
                    break
            except BslTimeout:
                time.sleep(BSL_INIT_INTERVAL_S)
        else:
            raise BslError("BSL initialization failed after all retries")

        self._connected = True
        return self._get_device_id()

    # ------------------------------------------------------------------
    # Phase 2: Commands
    # ------------------------------------------------------------------
    def _send_command(
        self,
        cmd: Cmd,
        addr: int = 0,
        length: int = 0,
        extra: bytes = b"",
        timeout_s: float = BSL_CMD_TIMEOUT_S,
    ) -> bytes:
        """
        Send a generic command frame and return the MCU response payload.

        Command frame format (TC3xx UM §3.1.3 – verify byte layout):
          ID  = BSL_CMD_ID (0x555)
          DLC = 8
          DB0 = command opcode                      (UM §3.1.3)
          DB1 = address[23:16]  (big-endian MSB)   (UM §3.1.3)
          DB2 = address[15:8]                       (UM §3.1.3)
          DB3 = address[7:0]                        (UM §3.1.3)
          DB4 = length[15:8]                        (UM §3.1.3)
          DB5 = length[7:0]                         (UM §3.1.3)
          DB6 = extra[0]  (command-specific)        (UM §3.1.3)
          DB7 = extra[1]  (command-specific)        (UM §3.1.3)

        NOTE: The above layout is an educated assumption.
              Verify the EXACT field positions and endianness from UM §3.1.3.
        """
        addr_bytes   = addr.to_bytes(4, "big")   # UM §3.1.3 – confirm endianness
        length_bytes = length.to_bytes(2, "big")  # UM §3.1.3
        extra_pad    = (extra + b"\x00\x00")[:2]

        frame = bytes([
            cmd,
            addr_bytes[1],   # addr [23:16]
            addr_bytes[2],   # addr [15:8]
            addr_bytes[3],   # addr [7:0]
            length_bytes[0],
            length_bytes[1],
            extra_pad[0],
            extra_pad[1],
        ])
        self._transport.send(BSL_CMD_ID, frame)
        return self._transport.recv(self._ack_id, timeout_s)

    def _check_ack(self, resp: bytes, context: str) -> None:
        """Raise BslNack if response indicates failure."""
        if not resp:
            raise BslTimeout(f"{context}: empty response")
        status = resp[0]  # UM §3.1.3 – confirm status byte position
        if status == Status.NACK:
            raise BslNack(f"{context}: MCU returned NACK (0x{status:02X})")
        if status == Status.ERROR:
            raise BslError(f"{context}: MCU returned ERROR (0x{status:02X})")

    # ------------------------------------------------------------------
    def _get_device_id(self) -> DeviceInfo:
        """Query device identification."""
        resp = self._send_command(Cmd.GET_DEVICE_ID)
        if len(resp) < 4:
            raise BslError(f"GET_DEVICE_ID: short response ({len(resp)} bytes)")

        # Response layout – UM §3.1.3 (placeholder):
        # DB0 = status
        # DB1-DB4 = JTAG ID (32-bit, big-endian)
        info = DeviceInfo()
        if len(resp) >= 5:
            info.jtag_id = struct.unpack_from(">I", resp, 1)[0]  # UM §3.1.3
        info.raw = resp
        log.info("Device: %s", info)
        return info

    # ------------------------------------------------------------------
    def erase(self, addr: int, num_sectors: int) -> None:
        """
        Erase `num_sectors` Flash sectors starting at `addr`.

        DFlash: sector = 4 KB  → addr must be 4 KB aligned
        PFlash: sector = 16 KB → addr must be 16 KB aligned
        """
        if not self._connected:
            raise BslError("Not connected – call connect() first")

        log.info("Erase  addr=0x%08X  sectors=%d", addr, num_sectors)
        resp = self._send_command(
            Cmd.ERASE_SECTOR,
            addr=addr,
            length=num_sectors,
            timeout_s=BSL_ERASE_TIMEOUT_S,
        )
        self._check_ack(resp, "ERASE_SECTOR")
        log.info("Erase complete")

    # ------------------------------------------------------------------
    def write(self, addr: int, data: bytes) -> None:
        """
        Write arbitrary-length `data` to Flash starting at `addr`.

        Data is split into BSL_WRITE_CHUNK-byte transactions.
        Each transaction:
          1. Send WRITE_DATA command frame (opcode + addr + length)
          2. Send data frames (8 bytes each)  – UM §3.1.3 data frame format
          3. Wait for ACK
        """
        if not self._connected:
            raise BslError("Not connected – call connect() first")

        total = len(data)
        offset = 0
        while offset < total:
            chunk = data[offset: offset + BSL_WRITE_CHUNK]
            cur_addr = addr + offset
            log.info("Write  addr=0x%08X  len=%d", cur_addr, len(chunk))

            # Step 1: Send write command
            resp = self._send_command(
                Cmd.WRITE_DATA,
                addr=cur_addr,
                length=len(chunk),
            )
            self._check_ack(resp, f"WRITE_DATA header @0x{cur_addr:08X}")

            # Step 2: Send data in 8-byte CAN frames
            self._send_data_frames(chunk)

            # Step 3: Wait for final ACK after all data frames
            final = self._transport.recv(self._ack_id, BSL_CMD_TIMEOUT_S)
            self._check_ack(final, f"WRITE_DATA finalize @0x{cur_addr:08X}")

            offset += len(chunk)

        log.info("Write complete (%d bytes total)", total)

    def _send_data_frames(self, data: bytes) -> None:
        """
        Transmit `data` as a sequence of 8-byte CAN frames.
        Frame format: ID=BSL_CMD_ID, DLC=8, padded with 0xFF if necessary.
        (UM §3.1.3 – confirm whether data frames use same ID or different)
        """
        for i in range(0, len(data), 8):
            chunk = data[i: i + 8]
            padded = chunk.ljust(8, b"\xFF")  # UM §3.1.3 – confirm padding value
            self._transport.send(BSL_CMD_ID, padded)

    # ------------------------------------------------------------------
    def verify(self, addr: int, length: int, expected_crc: int) -> bool:
        """
        Request CRC-32 verification over [addr, addr+length).
        Returns True if MCU-computed CRC matches `expected_crc`.
        """
        if not self._connected:
            raise BslError("Not connected – call connect() first")

        log.info("Verify addr=0x%08X  len=0x%X  crc=0x%08X",
                 addr, length, expected_crc)
        resp = self._send_command(
            Cmd.VERIFY_DATA,
            addr=addr,
            length=length,
            extra=expected_crc.to_bytes(4, "big")[:2],  # UM §3.1.3 – CRC passing method
        )
        self._check_ack(resp, "VERIFY_DATA")
        # Extract MCU CRC from response – UM §3.1.3
        mcu_crc = struct.unpack_from(">I", resp, 1)[0] if len(resp) >= 5 else 0
        match = (mcu_crc == expected_crc)
        log.info("Verify %s  (MCU=0x%08X  expected=0x%08X)",
                 "OK" if match else "FAIL", mcu_crc, expected_crc)
        return match

    # ------------------------------------------------------------------
    def execute(self, addr: int = 0xA0000000) -> None:
        """
        Jump to `addr` – transfers execution to user application.
        After this call, BSL communication ends.
        """
        if not self._connected:
            raise BslError("Not connected – call connect() first")

        log.info("Execute  addr=0x%08X", addr)
        self._send_command(Cmd.EXECUTE, addr=addr)
        # No meaningful response expected after execute
        self._connected = False
        log.info("Execute command sent")

    # ------------------------------------------------------------------
    # High-level: Flash an Intel HEX file
    # ------------------------------------------------------------------
    def flash_hex(
        self,
        hex_path: str,
        do_erase: bool = True,
        do_verify: bool = True,
    ) -> None:
        """
        Convenience method: erase + write + verify from an Intel HEX file.
        Requires `pip install intelhex`.
        """
        try:
            from intelhex import IntelHex
        except ImportError:
            raise ImportError("Install intelhex: pip install intelhex")

        ih = IntelHex()
        ih.loadhex(hex_path)
        log.info("Loaded HEX: %s  segments=%d", hex_path, len(ih.segments()))

        for start, stop in ih.segments():
            length = stop - start
            data = bytes(ih.tobinarray(start=start, size=length))

            if do_erase:
                # Derive sector count from address range
                # DFlash (0xAF……): sector = 4 KB = 0x1000
                # PFlash (0xA0……): sector = 16 KB = 0x4000
                if start >= 0xAF000000:
                    sector_size = 0x1000
                else:
                    sector_size = 0x4000
                aligned_start = start & ~(sector_size - 1)
                num_sectors = (stop - aligned_start + sector_size - 1) // sector_size
                self.erase(aligned_start, num_sectors)

            self.write(start, data)

            if do_verify:
                crc = _crc32(data)
                ok = self.verify(start, length, crc)
                if not ok:
                    raise BslError(
                        f"Verification failed @ 0x{start:08X}–0x{stop:08X}"
                    )

        log.info("flash_hex complete: %s", hex_path)

    # ------------------------------------------------------------------
    def close(self) -> None:
        """Release CAN bus."""
        self._transport.close()
        log.info("CAN bus closed")

    # ------------------------------------------------------------------
    # Context manager support
    # ------------------------------------------------------------------
    def __enter__(self) -> "TC3xxCanBsl":
        return self

    def __exit__(self, *_) -> None:
        self.close()


# ---------------------------------------------------------------------------
# Utility: CRC-32 (IEEE 802.3 polynomial, same as binascii.crc32)
# ---------------------------------------------------------------------------
def _crc32(data: bytes) -> int:
    import binascii
    return binascii.crc32(data) & 0xFFFFFFFF


# ---------------------------------------------------------------------------
# Quick smoke-test / CLI entry point
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    import argparse

    logging.basicConfig(
        level=logging.DEBUG,
        format="%(asctime)s %(levelname)-7s %(message)s",
    )

    parser = argparse.ArgumentParser(description="TC3xx CAN-BSL host tool")
    parser.add_argument("--channel",  default="PCAN_USBBUS1")
    parser.add_argument("--bitrate",  type=int, default=500_000)
    parser.add_argument("--hex",      help="Intel HEX file to flash")
    parser.add_argument("--erase",    action="store_true",
                        help="Erase only (requires --addr and --sectors)")
    parser.add_argument("--addr",     type=lambda x: int(x, 0),
                        default=0xAF000000)
    parser.add_argument("--sectors",  type=int, default=1)
    parser.add_argument("--no-verify", dest="verify", action="store_false")
    parser.add_argument("--execute",  action="store_true",
                        help="Execute after flash (jump to 0xA0000000)")
    args = parser.parse_args()

    with TC3xxCanBsl(
        interface="pcan",
        channel=args.channel,
        bitrate=args.bitrate,
    ) as bsl:
        info = bsl.connect()
        print(f"Connected: {info}")

        if args.erase and not args.hex:
            bsl.erase(args.addr, args.sectors)

        if args.hex:
            bsl.flash_hex(args.hex, do_erase=True, do_verify=args.verify)

        if args.execute:
            bsl.execute(0xA0000000)

    print("Done.")
