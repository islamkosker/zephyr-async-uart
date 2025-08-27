#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Zephyr Async UART Testbench (TX/RX) - Python
--------------------------------------------
- Frames data as: [SYNC=0xAA][LEN][DATA...][CRC16-CCITT (big-endian)]
- CRC is computed over LEN + DATA, with init=0xFFFF, poly=0x1021 (no final XOR)
- Supports large transfers using a 7-byte segmentation header inside DATA:
    typ(1), xid(1), total(2BE), offset(2BE), clen(1)
- Receives and parses incoming frames, verifies CRC, and reassembles segments.

Requires: pyserial  (pip install pyserial)

Usage examples:
  python zephyr_uart_testbench.py --port COM6 --baud 115200 --rx-only
  python zephyr_uart_testbench.py --port /dev/ttyUSB0 --send "Hello UART!"
  python zephyr_uart_testbench.py --port /dev/ttyUSB0 --send-hex "01 02 AA FF"
  python zephyr_uart_testbench.py --port /dev/ttyUSB0 --send-file sample.bin --xid 3
  python zephyr_uart_testbench.py --port /dev/ttyUSB0 --send-hex "00 01 ... 70B" --buffer-mode
"""

import argparse
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Optional, Dict, Tuple, List

try:
    import serial  # pyserial
except Exception as e:  # pragma: no cover
    print("pyserial is required. Install with: pip install pyserial", file=sys.stderr)
    raise

# ---- Protocol defaults (keep in sync with uart_cfg.h) ----
SYNC_BYTE = 0xAA
UART_MAX_PACKET_SIZE = 64          # len(DATA) upper bound
SEG_HDR_SIZE = 7                   # typ(1), xid(1), total(2), offset(2), clen(1)
SEG_TYP_DATA = 0x01
CRC_INIT = 0xFFFF                  # CRC16-CCITT initial value

# Derived
PAYLOAD_MAX = UART_MAX_PACKET_SIZE - SEG_HDR_SIZE

# ---- CRC16-CCITT (False) ----
def crc16_ccitt_step(crc: int, b: int) -> int:
    """Single-byte CCITT step. Poly 0x1021, init 0xFFFF, no final xor, no reflection."""
    crc ^= (b << 8) & 0xFFFF
    for _ in range(8):
        if crc & 0x8000:
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF
        else:
            crc = (crc << 1) & 0xFFFF
    return crc

def crc16_ccitt(data: bytes, init: int = CRC_INIT) -> int:
    crc = init
    for b in data:
        crc = crc16_ccitt_step(crc, b)
    return crc & 0xFFFF

# ---- Segmentation header helpers ----
def seg_hdr_write(typ: int, xid: int, total: int, offset: int, clen: int) -> bytes:
    assert 0 <= typ <= 0xFF
    assert 0 <= xid <= 0xFF
    assert 0 <= total <= 0xFFFF
    assert 0 <= offset <= 0xFFFF
    assert 0 <= clen <= 0xFF
    return bytes([typ, xid, (total >> 8) & 0xFF, total & 0xFF, (offset >> 8) & 0xFF, offset & 0xFF, clen])

def seg_hdr_read(buf: bytes) -> Tuple[int,int,int,int,int]:
    """Returns (typ, xid, total, offset, clen). Expects at least 7 bytes."""
    typ = buf[0]
    xid = buf[1]
    total = (buf[2] << 8) | buf[3]
    offset = (buf[4] << 8) | buf[5]
    clen = buf[6]
    return typ, xid, total, offset, clen

# ---- Frame builder/parser ----
def build_frame(payload: bytes) -> bytes:
    """Construct a single frame: SYNC, LEN, DATA=payload, CRC (big-endian)."""
    if not (0 < len(payload) <= UART_MAX_PACKET_SIZE):
        raise ValueError(f"payload length must be 1..{UART_MAX_PACKET_SIZE}, got {len(payload)}")
    # CRC covers LEN + DATA (per Zephyr framer.c logic)
    crc = crc16_ccitt(bytes([len(payload)]) + payload, init=CRC_INIT)
    return bytes([SYNC_BYTE, len(payload)]) + payload + bytes([(crc >> 8) & 0xFF, crc & 0xFF])

def build_large_frames(data: bytes, xid: int = 1) -> List[bytes]:
    """Split 'data' into multiple frames using 7-byte segment header inside DATA."""
    frames = []
    total = len(data)
    off = 0
    while off < total:
        chunk = min(PAYLOAD_MAX, total - off)
        hdr = seg_hdr_write(SEG_TYP_DATA, xid, total, off, chunk)
        payload = hdr + data[off:off+chunk]
        frames.append(build_frame(payload))
        off += chunk
    return frames

def build_buffer_frames(data: bytes) -> List[bytes]:
    """Split 'data' into multiple frames WITHOUT segmentation header (raw slicing)."""
    frames = []
    off = 0
    total = len(data)
    while off < total:
        chunk = min(UART_MAX_PACKET_SIZE, total - off)
        payload = data[off:off+chunk]
        frames.append(build_frame(payload))
        off += chunk
    return frames

@dataclass
class ParsedFrame:
    data: bytes  # DATA (without SYNC, LEN, CRC)
    raw: bytes   # full raw frame

# Stream parser (same state order as the Zephyr framer)
class StreamParser:
    ST_SYNC, ST_LEN, ST_DATA, ST_CRC_H, ST_CRC_L = range(5)

    def __init__(self, on_frame):
        self.state = self.ST_SYNC
        self.len = 0
        self.data_buf = bytearray()
        self.crc_calc = CRC_INIT
        self.crc_hi_tmp = 0
        self.on_frame = on_frame

    def reset(self):
        self.state = self.ST_SYNC
        self.len = 0
        self.data_buf.clear()
        self.crc_calc = CRC_INIT
        self.crc_hi_tmp = 0

    def feed(self, chunk: bytes):
        for b in chunk:
            self._push_byte(b)

    def _push_byte(self, b: int):
        if self.state == self.ST_SYNC:
            if b == SYNC_BYTE:
                self.state = self.ST_LEN
                self.crc_calc = CRC_INIT
                self.data_buf.clear()
            return

        if self.state == self.ST_LEN:
            if b == 0 or b > UART_MAX_PACKET_SIZE:
                self.reset()
                return
            self.len = b
            self.crc_calc = crc16_ccitt_step(self.crc_calc, b)  # include LEN
            self.state = self.ST_DATA
            return

        if self.state == self.ST_DATA:
            self.data_buf.append(b)
            self.crc_calc = crc16_ccitt_step(self.crc_calc, b)
            if len(self.data_buf) == self.len:
                self.state = self.ST_CRC_H
            return

        if self.state == self.ST_CRC_H:
            self.crc_hi_tmp = b
            self.state = self.ST_CRC_L
            return

        if self.state == self.ST_CRC_L:
            recv_crc = ((self.crc_hi_tmp << 8) | b) & 0xFFFF
            if recv_crc == self.crc_calc:
                raw = bytes([SYNC_BYTE, self.len]) + bytes(self.data_buf) + bytes([self.crc_hi_tmp, b])
                try:
                    self.on_frame(ParsedFrame(data=bytes(self.data_buf), raw=raw))
                except Exception as e:
                    print(f"[parser] on_frame error: {e}", file=sys.stderr)
            self.reset()
            return

# ---- Reassembly for segmented payloads ----
@dataclass
class Reassembly:
    total: int
    buf: bytearray = field(default_factory=bytearray)
    received: int = 0

class SegmentReassembler:
    """
    Detects 7-byte segment headers inside the frame DATA and reassembles
    multi-frame transfers keyed by (xid).
    """
    def __init__(self):
        self.active: Dict[int, Reassembly] = {}

    def try_handle(self, data: bytes) -> Optional[Tuple[int, bytes, bool]]:
        """
        If data looks like a segmented payload, accumulate and return (xid, full_bytes, done).
        If not segmented, return None.
        """
        if len(data) < SEG_HDR_SIZE:
            return None
        typ, xid, total, offset, clen = seg_hdr_read(data[:SEG_HDR_SIZE])
        if typ != SEG_TYP_DATA:
            return None
        if clen != len(data) - SEG_HDR_SIZE:
            return None
        if offset + clen > total:
            return None

        R = self.active.get(xid)
        if R is None:
            R = Reassembly(total=total, buf=bytearray(total), received=0)
            self.active[xid] = R

        R.buf[offset:offset+clen] = data[SEG_HDR_SIZE:SEG_HDR_SIZE+clen]
        R.received += clen

        done = (R.received >= R.total)
        if done:
            payload = bytes(R.buf[:R.total])
            del self.active[xid]
            return (xid, payload, True)
        return (xid, bytes(R.buf), False)

# ---- Hex helpers ----
def hexdump(b: bytes) -> str:
    return ' '.join(f'{x:02X}' for x in b)

def parse_hex_bytes(s: str) -> bytes:
    toks = s.replace(",", " ").split()
    out = bytearray()
    for t in toks:
        t = t.strip()
        if t.lower().startswith("0x"):
            v = int(t, 16)
        else:
            v = int(t, 16)
        if not (0 <= v <= 0xFF):
            raise ValueError(f"hex byte out of range: {t}")
        out.append(v)
    return bytes(out)

# ---- Worker threads ----
class RXWorker(threading.Thread):
    def __init__(self, ser: serial.Serial, reasm: SegmentReassembler, verbose: bool = True):
        super().__init__(daemon=True)
        self.ser = ser
        self.reasm = reasm
        self.verbose = verbose
        self.parser = StreamParser(on_frame=self.on_frame)
        self._stop = threading.Event()

    def on_frame(self, pf: ParsedFrame):
        if self.verbose:
            print(f"[RX] Frame: LEN={len(pf.data)}  CRC OK  RAW={hexdump(pf.raw)}")
        seg = self.reasm.try_handle(pf.data)
        if seg is None:
            try:
                txt = pf.data.decode('utf-8')
                print(f"[RX] DATA (text): {txt!r}")
            except Exception:
                print(f"[RX] DATA (hex):  {hexdump(pf.data)}")
            return
        xid, payload, done = seg
        if done:
            print(f"[RX] Segmented complete: xid={xid}  total={len(payload)} bytes")
            try:
                txt = payload.decode('utf-8')
                print(f"[RX] REASSM (text): {txt[:200]!r}")
            except Exception:
                print(f"[RX] REASSM (hex):  {hexdump(payload[:64])} ...")
        else:
            print(f"[RX] Segment part: xid={xid}  received≈{len(payload)} / ? (waiting more)")

    def run(self):
        while not self._stop.is_set():
            try:
                n = self.ser.in_waiting
                chunk = self.ser.read(n or 1)
                if chunk:
                    self.parser.feed(chunk)
            except serial.SerialException as e:
                print(f"[RX] Serial error: {e}", file=sys.stderr)
                break
            except Exception as e:
                print(f"[RX] Error: {e}", file=sys.stderr)
        print("[RX] Stopped.")

    def stop(self):
        self._stop.set()

# ---- TX helpers ----
def tx_send_frame(ser: serial.Serial, payload: bytes, delay: float = 0.0, verbose: bool = True):
    frame = build_frame(payload)
    if verbose:
        print(f"[TX] {len(payload)}B -> Frame {len(frame)}B: {hexdump(frame)}")
    ser.write(frame)
    ser.flush()
    if delay > 0:
        time.sleep(delay)

def tx_send_large(ser: serial.Serial, data: bytes, xid: int = 1, per_frame_delay: float = 0.01, verbose: bool = True):
    frames = build_large_frames(data, xid=xid)
    for i, f in enumerate(frames):
        if verbose:
            print(f"[TX] Part {i+1}/{len(frames)}  Frame {len(f)}B: {hexdump(f)}")
        ser.write(f)
        ser.flush()
        if per_frame_delay > 0:
            time.sleep(per_frame_delay)

def tx_send_buffer(ser: serial.Serial, data: bytes, per_frame_delay: float = 0.01, verbose: bool = True):
    """Send long data by raw slicing into <=64B frames (no segmentation header)."""
    frames = build_buffer_frames(data)
    for i, f in enumerate(frames):
        if verbose:
            print(f"[TX] Slice {i+1}/{len(frames)}  Frame {len(f)}B: {hexdump(f)}")
        ser.write(f)
        ser.flush()
        if per_frame_delay > 0:
            time.sleep(per_frame_delay)

# ---- CLI ----
def parse_args(argv=None):
    ap = argparse.ArgumentParser(description="Zephyr Async UART Testbench (TX/RX)")
    ap.add_argument("--port", required=True, help="Serial port (e.g., COM6, /dev/ttyUSB0)")
    ap.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    ap.add_argument("--rx-only", action="store_true", help="Only listen, don't transmit")
    ap.add_argument("--send", help="Send a UTF-8 string as a single frame")
    ap.add_argument("--send-hex", help="Send hex bytes as a single frame (e.g., '01 02 AA')")
    ap.add_argument("--send-file", help="Send file contents using segmentation (multiple frames)")
    ap.add_argument("--xid", type=int, default=1, help="Segment transfer ID (default: 1)")
    ap.add_argument("--repeat", type=int, default=1, help="Repeat count for --send/--send-hex (default: 1)")
    ap.add_argument("--per-frame-delay", type=float, default=0.01, help="Delay between frames in seconds (default: 0.01)")
    ap.add_argument("--buffer-mode", action="store_true", help="If payload exceeds 64B, slice into multiple frames WITHOUT segmentation header")
    ap.add_argument("--quiet", action="store_true", help="Less verbose output")
    ap.add_argument("--exit-after-send", action="store_true", help="Exit after sending instead of staying in RX loop")
    return ap.parse_args(argv)

def main(argv=None):
    args = parse_args(argv)
    verbose = not args.quiet

    # Open serial
    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.05, write_timeout=1.0)
    except Exception as e:
        print(f"[ERR] Could not open {args.port} at {args.baud}: {e}", file=sys.stderr)
        return 2

    reasm = SegmentReassembler()
    rx = RXWorker(ser, reasm, verbose=verbose)
    rx.start()

    try:
        if not args.rx_only:
            # Handle file first (segmented)
            if args.send_file:
                try:
                    with open(args.send_file, "rb") as f:
                        data = f.read()
                except Exception as e:
                    print(f"[ERR] Could not read file: {e}", file=sys.stderr)
                    return 3
                if verbose:
                    print(f"[TX] Sending file {args.send_file} ({len(data)} bytes) xid={args.xid}")
                tx_send_large(ser, data, xid=args.xid, per_frame_delay=args.per_frame_delay, verbose=verbose)

            # Handle single-frame sends (string or hex)
            if args.send or args.send_hex:
                if args.send:
                    payload = args.send.encode("utf-8")
                else:
                    try:
                        payload = parse_hex_bytes(args.send_hex)
                    except Exception as e:
                        print(f"[ERR] Invalid --send-hex: {e}", file=sys.stderr)
                        return 4

                for i in range(args.repeat):
                    if len(payload) <= UART_MAX_PACKET_SIZE:
                        tx_send_frame(ser, payload, delay=args.per_frame_delay, verbose=verbose)
                    else:
                        if args.buffer_mode:
                            if verbose:
                                print(f"[TX] Payload {len(payload)}B > {UART_MAX_PACKET_SIZE}. Using buffer-mode slicing.")
                            tx_send_buffer(ser, payload, per_frame_delay=args.per_frame_delay, verbose=verbose)
                        else:
                            if verbose:
                                print(f"[TX] Payload {len(payload)}B > {UART_MAX_PACKET_SIZE}. Using segmented transfer xid={args.xid}.")
                            tx_send_large(ser, payload, xid=args.xid, per_frame_delay=args.per_frame_delay, verbose=verbose)

            if args.exit_after_send and not args.rx_only:
                return 0

        # Stay in RX loop until Ctrl+C
        if verbose:
            print("[INFO] RX running. Press Ctrl+C to stop.")
        while True:
            time.sleep(0.2)

    except KeyboardInterrupt:
        pass
    finally:
        rx.stop()
        rx.join(timeout=1.0)
        try:
            ser.close()
        except Exception:
            pass

    return 0

if __name__ == "__main__":
    sys.exit(main())
