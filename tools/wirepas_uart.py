#!/usr/bin/env python3
"""
wirepas_uart.py  —  Wirepas Dual MCU API (WAPS) UART tool

Protocol: WAPS over UART/SLIP
  SLIP:  END=0xC0, ESC=0xDB; 0xC0→0xDB 0xDC, 0xDB→0xDB 0xDD
  Frame: [func(1)][fid(1)][plen(1)][payload(N)][crc_lsb][crc_msb]  SLIP-wrapped
  CRC:   CRC16-CCITT  poly=0x1021  init=0x0000  over [func+fid+plen+payload]

Usage:
  python wirepas_uart.py -p PORT [-b BAUD] [-t TIMEOUT] <command>

Commands:
  listen
  send unicast|multicast|broadcast [--dst ADDR] [--src-ep N] [--dst-ep N] [--hex] PAYLOAD
  appconfig read
  appconfig write [--seq N] [--interval S] [--hex] DATA
  cdd list
  cdd get TYPE
  cdd set TYPE [--hex] DATA
  otap status
  otap clear
  otap upload FILE [--seq N]
  otap target [--seq N] [--crc N] [--action N] [--param N]
"""

import argparse
import os
import struct
import sys
import threading
import time
from typing import Callable, Optional

try:
    import serial
except ImportError:
    print("Missing dependency: pip install pyserial")
    sys.exit(1)


# ─── ANSI colours ──────────────────────────────────────────────────────────────
C0    = "\033[0m"
CBOLD = "\033[1m"
CCYN  = "\033[36m"
CGRN  = "\033[32m"
CYEL  = "\033[33m"
CMAG  = "\033[35m"
CRED  = "\033[31m"
CDIM  = "\033[90m"


# ─── WAPS function codes ───────────────────────────────────────────────────────
class FC:
    INDICATION_POLL_REQ  = 0x04
    INDICATION_POLL_CNF  = 0x84
    STACK_START_REQ      = 0x05
    STACK_START_CNF      = 0x85
    STACK_STOP_REQ       = 0x06
    STACK_STOP_CNF       = 0x86
    STACK_STATE_IND      = 0x07
    STACK_STATE_RSP      = 0x87
    CSAP_ATTR_READ_REQ   = 0x0E
    CSAP_ATTR_READ_CNF   = 0x8E
    CSAP_ATTR_WRITE_REQ  = 0x0D
    CSAP_ATTR_WRITE_CNF  = 0x8D
    TX_REQ               = 0x01
    TX_CNF               = 0x81
    TX_IND               = 0x02
    RX_IND               = 0x03
    RX_RSP               = 0x83
    RX_FRAG_IND          = 0x10   # fragmented data RX (dualmcu forces frag mode)
    RX_FRAG_RSP          = 0x90
    APP_CONFIG_WRITE_REQ = 0x3A
    APP_CONFIG_WRITE_CNF = 0xBA
    APP_CONFIG_READ_REQ  = 0x3B
    APP_CONFIG_READ_CNF  = 0xBB
    APP_CONFIG_RX_IND    = 0x3F
    APP_CONFIG_RX_RSP    = 0xBF
    CDD_SET_REQ          = 0x29
    CDD_SET_CNF          = 0xA9
    CDD_GET_REQ          = 0x30
    CDD_GET_CNF          = 0xB0
    CDD_IND              = 0x31
    CDD_RSP              = 0xB1
    CDD_LIST_REQ         = 0x32
    CDD_LIST_CNF         = 0xB2
    SCRATCH_START_REQ    = 0x17
    SCRATCH_START_CNF    = 0x97
    SCRATCH_BLOCK_REQ    = 0x18
    SCRATCH_BLOCK_CNF    = 0x98
    SCRATCH_STATUS_REQ   = 0x19
    SCRATCH_STATUS_CNF   = 0x99
    SCRATCH_BOOTABLE_REQ = 0x1A
    SCRATCH_BOOTABLE_CNF = 0x9A
    SCRATCH_CLEAR_REQ    = 0x1B
    SCRATCH_CLEAR_CNF    = 0x9B
    SCRATCH_TARGET_WRITE_REQ = 0x26
    SCRATCH_TARGET_WRITE_CNF = 0xA6
    SCRATCH_TARGET_READ_REQ  = 0x27
    SCRATCH_TARGET_READ_CNF  = 0xA7


# ─── CSAP attribute IDs (csap_frames.h) ───────────────────────────────────────
class CSAP:
    NODE_ID          = 1   # uint32  node address
    NETWORK_ADDR     = 2   # uint24  (3 bytes, little-endian)
    NETWORK_CHANNEL  = 3   # uint8
    NODE_ROLE        = 4   # uint8   see NodeRole
    FIRMWARE_MAJOR   = 9
    FIRMWARE_MINOR   = 10
    FIRMWARE_MAINT   = 11
    FIRMWARE_DEV     = 12
    WAPS_VERSION     = 8

class NodeRole:
    SINK_LE      = 0x01
    HEADNODE_LE  = 0x02
    SUBNODE_LE   = 0x03
    SINK_LL      = 0x11
    HEADNODE_LL  = 0x12
    SUBNODE_LL   = 0x13

_ROLE_NAMES = {v: k for k, v in vars(NodeRole).items() if not k.startswith('_')}

# ─── MSAP stack state flags (msap_frames.h) ───────────────────────────────────
STACK_STOPPED            = 1 << 1
STACK_NODE_ID_NOT_SET    = 1 << 2
STACK_NET_ADDR_NOT_SET   = 1 << 3
STACK_CHANNEL_NOT_SET    = 1 << 4
STACK_ROLE_NOT_SET       = 1 << 5

# ─── Addresses ─────────────────────────────────────────────────────────────────
ADDR_BROADCAST = 0xFFFFFFFF
ADDR_MCAST_BIT = 0x80000000


# ─── SLIP ──────────────────────────────────────────────────────────────────────
SLIP_END     = 0xC0
SLIP_ESC     = 0xDB
SLIP_ESC_END = 0xDC
SLIP_ESC_ESC = 0xDD


def slip_encode(data: bytes) -> bytes:
    out = bytearray([SLIP_END])
    for b in data:
        if b == SLIP_END:
            out += bytes([SLIP_ESC, SLIP_ESC_END])
        elif b == SLIP_ESC:
            out += bytes([SLIP_ESC, SLIP_ESC_ESC])
        else:
            out.append(b)
    out.append(SLIP_END)
    return bytes(out)


def slip_decode(data: bytes) -> bytes:
    out = bytearray()
    escaped = False
    for b in data:
        if escaped:
            if b == SLIP_ESC_END:
                out.append(SLIP_END)
            elif b == SLIP_ESC_ESC:
                out.append(SLIP_ESC)
            escaped = False
        elif b == SLIP_ESC:
            escaped = True
        else:
            out.append(b)
    return bytes(out)


# ─── CRC16-CCITT ───────────────────────────────────────────────────────────────
# Firmware uses init=0xFFFF (see util/crc.h: Crc_initValue() = 0xffff)
def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
    return crc


# ─── Frame build / parse ───────────────────────────────────────────────────────
_fid_counter = 0


def _next_fid() -> int:
    global _fid_counter
    _fid_counter = (_fid_counter + 1) & 0xFF
    return _fid_counter


def build_frame(func: int, payload: bytes, fid: Optional[int] = None) -> bytes:
    if fid is None:
        fid = _next_fid()
    hdr = bytes([func, fid, len(payload)])
    crc = crc16(hdr + payload)
    raw = hdr + payload + bytes([crc & 0xFF, crc >> 8])
    return slip_encode(raw)


def parse_frame(raw: bytes) -> Optional[dict]:
    if len(raw) < 5:
        return None
    func, fid, plen = raw[0], raw[1], raw[2]
    if len(raw) < 3 + plen + 2:
        return None
    payload  = raw[3:3 + plen]
    crc_recv = raw[3 + plen] | (raw[3 + plen + 1] << 8)
    if crc16(raw[:3 + plen]) != crc_recv:
        return None
    return {"func": func, "fid": fid, "payload": payload}


# ─── WapsConn ─────────────────────────────────────────────────────────────────
# The dualmcu firmware uses UART auto-power (waps_uart.c) on battery-powered nodes:
# the UART is off between frames and wakes up on the first 0xC0 (SLIP_END) byte.
# Sending extra 0xC0 preamble bytes gives the UART time to wake up before the
# frame header arrives; without them the first bytes are lost → CRC error → timeout.
_UART_WAKEUP_PREAMBLE = bytes([SLIP_END] * 6)


class WapsConn:
    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 3.0):
        self._port     = port
        self._baudrate = baudrate
        self._timeout  = timeout
        self._ser: Optional[serial.Serial] = None
        self._buf      = bytearray()
        self._lock     = threading.Lock()
        self._req_lock = threading.Lock()  # serialise all outgoing requests
        self._pending: dict = {}  # fid → {"event": Event, "result": dict|None}
        self._callbacks: list = []
        self._running  = False

    def open(self):
        self._ser = serial.Serial(self._port, self._baudrate, timeout=0.05)
        self._running = True
        threading.Thread(target=self._rx_loop, daemon=True).start()

    def close(self):
        self._running = False
        # Wake any in-flight request waiters so they return promptly
        with self._lock:
            for entry in self._pending.values():
                entry["event"].set()
        ser, self._ser = self._ser, None
        if ser:
            try:
                ser.close()
            except Exception:
                pass

    def add_callback(self, cb: Callable):
        self._callbacks.append(cb)

    # RX thread: accumulates bytes, drains complete SLIP frames
    def _rx_loop(self):
        while self._running:
            try:
                data = self._ser.read(256)
            except Exception:
                break
            if data:
                self._buf.extend(data)
                self._drain()

    def _drain(self):
        while True:
            # Each SLIP_END terminates one frame; everything before it is the content
            try:
                idx = self._buf.index(SLIP_END)
            except ValueError:
                return
            content = bytes(self._buf[:idx])
            del self._buf[:idx + 1]
            if not content:
                continue  # leading or consecutive END delimiter → skip
            raw   = slip_decode(content)
            frame = parse_frame(raw)
            if frame:
                self._dispatch(frame)

    def _dispatch(self, frame: dict):
        func, fid = frame["func"], frame["fid"]
        # Confirmations (func ≥ 0x80) with a matching pending fid → wake caller
        if func >= 0x80:
            with self._lock:
                entry = self._pending.get(fid)
            if entry is not None:
                entry["result"] = frame
                entry["event"].set()
                return
        # Indications (and unmatched confirmations) → user callbacks
        for cb in self._callbacks:
            try:
                cb(frame)
            except Exception as exc:
                print(f"{CRED}[callback error] {exc}{C0}")

    def _write(self, frame: bytes) -> bool:
        """Send frame preceded by UART wakeup preamble. Returns False if closed."""
        ser = self._ser
        if ser is None or not getattr(ser, "is_open", False):
            return False
        try:
            ser.write(_UART_WAKEUP_PREAMBLE + frame)
            return True
        except serial.SerialException:
            return False

    def request(self, func: int, payload: bytes = b"",
                timeout: Optional[float] = None) -> Optional[dict]:
        """Send a request and block until the matching confirmation arrives.
        Serialised by _req_lock so concurrent callers never interleave frames.
        timeout: override the connection-level timeout (seconds)."""
        t = timeout if timeout is not None else self._timeout
        with self._req_lock:
            if not self._running:
                return None
            fid   = _next_fid()
            entry = {"event": threading.Event(), "result": None}
            with self._lock:
                self._pending[fid] = entry
            if not self._write(build_frame(func, payload, fid=fid)):
                with self._lock:
                    self._pending.pop(fid, None)
                return None
            ok = entry["event"].wait(t)
            with self._lock:
                self._pending.pop(fid, None)
            return entry["result"] if ok else None

    def respond(self, func_rsp: int, fid: int, payload: bytes = b""):
        """Send a response (RSP) to acknowledge an indication."""
        self._write(build_frame(func_rsp, payload, fid=fid))


# ─── DSAP TX ──────────────────────────────────────────────────────────────────
_apdu_id_ctr = 0

_DSAP_TX_ERRORS = {
    0: "OK",            1: "STACK_STOPPED",  2: "INV_QOS",
    3: "INV_OPTS",      4: "OUT_OF_MEMORY",  5: "UNKNOWN_DST",
    6: "INV_LEN",       7: "IND_FULL",       8: "INV_PDU_ID",
    9: "RESV_EP",      10: "ACCESS_DENIED",
}


def cmd_send(conn: WapsConn, dst: int, dst_ep: int, payload: bytes,
             src_ep: int = 1, qos: int = 0, tx_opts: int = 0) -> bool:
    global _apdu_id_ctr
    _apdu_id_ctr = (_apdu_id_ctr + 1) & 0xFFFF
    # dsap_data_tx_req_t: apdu_id(H) src_ep(B) dst_addr(I) dst_ep(B) qos(B) tx_opts(B) apdu_len(B)
    pkt  = struct.pack('<HBIBBBB', _apdu_id_ctr, src_ep, dst, dst_ep,
                       qos, tx_opts, len(payload))
    pkt += payload
    cnf = conn.request(FC.TX_REQ, pkt)
    if cnf is None:
        print(f"{CRED}TX: timeout{C0}")
        return False
    p = cnf["payload"]
    if len(p) < 4:
        print(f"{CRED}TX: short confirmation{C0}")
        return False
    # dsap_data_tx_cnf_t: apdu_id(H) result(B) buff_cap(B)
    _, result, buff_cap = struct.unpack_from('<HBB', p)
    err = _DSAP_TX_ERRORS.get(result, f"ERR({result})")
    col = CGRN if result == 0 else CRED
    print(f"TX: {col}{err}{C0}  buf_cap={buff_cap}")
    return result == 0


# ─── Remote API ───────────────────────────────────────────────────────────────
# Wirepas 2.4 GHz mesh: request src_ep=255→dst_ep=240, response src_ep=240→dst_ep=255
REMOTE_API_REQ_SRC_EP = 255
REMOTE_API_REQ_DST_EP = 240
REMOTE_API_RSP_SRC_EP = 240
REMOTE_API_RSP_DST_EP = 255

# Response type byte for MSAP Scratchpad Status
_REMOTE_API_SCRATCH_STATUS_REQ  = 0x19
_REMOTE_API_SCRATCH_STATUS_RSP  = 0x99

_REMOTE_SCRATCH_ACTIONS = {
    0: "NO_OTAP", 1: "PROPAGATE_ONLY", 2: "PROPAGATE_AND_PROCESS",
    3: "PROPAGATE_AND_PROCESS_WITH_DELAY", 4: "LEGACY",
}


def cmd_remote_scratchpad_status_req(conn: WapsConn, target_addr: int,
                                     log_cb: Optional[Callable] = None) -> bool:
    """Send Remote API MSAP Scratchpad Status request to a remote node.
    The response arrives asynchronously as an RX packet (src_ep=240 dst_ep=255).
    Requires the stack to be started on the SINK."""
    def log(msg):
        if log_cb: log_cb(msg)
        else: print(msg)
    payload = bytes([_REMOTE_API_SCRATCH_STATUS_REQ, 0x00])  # TLV no value
    ok = cmd_send(conn, dst=target_addr, dst_ep=REMOTE_API_REQ_DST_EP,
                  src_ep=REMOTE_API_REQ_SRC_EP, payload=payload)
    log(f"Remote scratchpad status req → 0x{target_addr:08x}: {'sent' if ok else 'FAIL'}")
    return ok


def parse_remote_scratchpad_status(apdu: bytes) -> Optional[dict]:
    """Parse a Remote API MSAP Scratchpad Status response (type=0x99).
    Returns a dict or None if the APDU is not a valid scratchpad status response.
    Response format (v5.1, 47 data bytes after the 2-byte TLV header):
      [0x99][len] stored(4+2+1+1+1) proc(4+2+1+4) fw(4) app(4+2+1+4+4) target(1+1+2+2+2)
    """
    if len(apdu) < 4 or apdu[0] != _REMOTE_API_SCRATCH_STATUS_RSP:
        return None
    data_len = apdu[1]
    if data_len not in (24, 39, 47) or len(apdu) < 2 + data_len:
        print(f"[parse_remote_scratchpad_status] unexpected data_len={data_len} "
              f"apdu_len={len(apdu)}  hex={apdu[:8].hex()}")
        return None
    d = apdu[2:]
    # Part 1 — 24 bytes (all firmware versions)
    (stored_bytes, stored_crc, stored_seq, stored_type, stored_status,
     proc_bytes, proc_crc, proc_seq, area_id,
     major, minor, maint, devel) = struct.unpack_from('<IHBBBIHBIBBBB', d, 0)
    result: dict = dict(
        stored_bytes=stored_bytes, stored_crc=stored_crc,
        stored_seq=stored_seq, stored_type=stored_type, stored_status=stored_status,
        proc_bytes=proc_bytes, proc_crc=proc_crc, proc_seq=proc_seq,
        area_id=area_id, fw=(major, minor, maint, devel),
    )
    if data_len >= 39:
        # Part 2 — app area info (since v4.0)
        (app_bytes, app_crc, app_seq, app_area_id,
         app_major, app_minor, app_maint, app_devel) = struct.unpack_from('<IHBIBBBB', d, 24)
        result.update(app_bytes=app_bytes, app_crc=app_crc, app_seq=app_seq,
                      app_area_id=app_area_id,
                      app_fw=(app_major, app_minor, app_maint, app_devel))
    if data_len >= 47:
        # Part 3 — OTAP target info (since v5.1)
        action, target_seq, target_crc, delay_min, remaining_min = \
            struct.unpack_from('<BBHHH', d, 39)
        result.update(action=action, action_name=_REMOTE_SCRATCH_ACTIONS.get(action, str(action)),
                      target_seq=target_seq, target_crc=target_crc,
                      delay_min=delay_min, remaining_min=remaining_min)
    return result


# ─── AppConfig (legacy MSAP-APP_CONFIG) ────────────────────────────────────────
def cmd_appconfig_read(conn: WapsConn):
    cnf = conn.request(FC.APP_CONFIG_READ_REQ)
    if cnf is None:
        print(f"{CRED}AppConfig read: timeout{C0}")
        return
    p = cnf["payload"]
    if len(p) < 4:
        print(f"{CRED}AppConfig read: short response{C0}")
        return
    # msap_int_read_cnf_t: result(B) seq(B) interval(H) config[80]
    result, seq, interval = struct.unpack_from('<BBH', p)
    if result != 0:
        print(f"{CRED}AppConfig read: result={result}{C0}")
        return
    config = p[4:]
    print(f"{CYEL}AppConfig: seq={seq}  interval={interval}s  len={len(config)}{C0}")
    _hexdump(config, indent=2)


def cmd_appconfig_write(conn: WapsConn, seq: int, interval: int, data: bytes):
    if len(data) > 80:
        print(f"{CRED}AppConfig: max 80 bytes{C0}")
        return
    # msap_int_write_req_t: seq(B) interval(H) config[80]
    pkt = struct.pack('<BH', seq, interval) + data + bytes(80 - len(data))
    cnf = conn.request(FC.APP_CONFIG_WRITE_REQ, pkt)
    if cnf is None:
        print(f"{CRED}AppConfig write: timeout{C0}")
        return
    _print_result("AppConfig write", cnf["payload"][0] if cnf["payload"] else 0xFF)


# ─── CDD (MSAP-CONFIG_DATA_ITEM) ──────────────────────────────────────────────
# npd_endpoint (uint16) maps to the TLV type used by shared_appconfig

def cmd_cdd_list(conn: WapsConn):
    # msap_config_data_list_items_req_t: command(B)=0
    cnf = conn.request(FC.CDD_LIST_REQ, struct.pack('<B', 0))
    if cnf is None:
        print(f"{CRED}CDD list: timeout{C0}")
        return
    p = cnf["payload"]
    if len(p) < 2:
        print(f"{CRED}CDD list: short response{C0}")
        return
    # msap_config_data_list_items_cnf_t: result(B) amount(B) endpoints[amount×H]
    result, count = p[0], p[1]
    if result != 0:
        print(f"{CRED}CDD list: result={result}{C0}")
        return
    print(f"{CYEL}CDD items: {count}{C0}")
    for i in range(min(count, 16)):
        ep = struct.unpack_from('<H', p, 2 + i * 2)[0]
        print(f"  type=0x{ep:04x} ({ep})")


def cmd_cdd_get(conn: WapsConn, type_id: int):
    # msap_config_data_item_get_req_t: npd_endpoint(H)
    cnf = conn.request(FC.CDD_GET_REQ, struct.pack('<H', type_id))
    if cnf is None:
        print(f"{CRED}CDD get: timeout{C0}")
        return
    p = cnf["payload"]
    if len(p) < 2:
        print(f"{CRED}CDD get: short response{C0}")
        return
    # msap_config_data_item_get_cnf_t: result(B) npd_payload_len(B) npd_payload[N]
    result, plen = p[0], p[1]
    if result != 0:
        print(f"{CRED}CDD get 0x{type_id:04x}: result={result}{C0}")
        return
    data = p[2:2 + plen]
    print(f"{CYEL}CDD 0x{type_id:04x} ({type_id})  len={plen}:{C0}")
    _hexdump(data, indent=2)


def cmd_cdd_set(conn: WapsConn, type_id: int, data: bytes):
    if len(data) > 80:
        print(f"{CRED}CDD: max 80 bytes{C0}")
        return
    # msap_config_data_item_set_req_t: npd_endpoint(H) npd_payload_len(B) npd_payload[N]
    pkt = struct.pack('<HB', type_id, len(data)) + data
    cnf = conn.request(FC.CDD_SET_REQ, pkt)
    if cnf is None:
        print(f"{CRED}CDD set: timeout{C0}")
        return
    _print_result(f"CDD set 0x{type_id:04x}", cnf["payload"][0] if cnf["payload"] else 0xFF)


# ─── OTAP (MSAP-SCRATCHPAD) ───────────────────────────────────────────────────
SCRATCH_BLOCK_MAX = 112

_SCRATCH_START_ERRORS = {
    0: "OK", 1: "INVALID_STATE", 2: "INVALID_NUM_BYTES",
    3: "INVALID_SEQ", 4: "ACCESS_DENIED",
}
_SCRATCH_BLOCK_ERRORS = {
    0: "OK",            1: "COMPLETED_OK",       2: "COMPLETED_ERROR",
    3: "INVALID_STATE", 4: "NOT_ONGOING",         5: "INVALID_START_ADDR",
    6: "INVALID_NUM_BYTES",                       7: "INVALID_DATA",
}
_SCRATCH_TARGET_ERRORS = {
    0: "OK", 1: "INVALID_ROLE", 2: "INVALID_VALUE", 3: "ACCESS_DENIED",
}
_SCRATCH_ACTION_NAMES = {
    0: "NO_OTAP", 1: "PROPAGATE_ONLY", 2: "PROPAGATE_AND_PROCESS",
    3: "PROPAGATE_AND_PROCESS_WITH_DELAY", 5: "LEGACY",
}


def cmd_otap_status(conn: WapsConn, log_cb=None) -> Optional[dict]:
    """Read scratchpad status. Returns a dict or None on error."""
    def log(msg):
        if log_cb:
            log_cb(msg)
        else:
            print(msg)
    cnf = conn.request(FC.SCRATCH_STATUS_REQ)
    if cnf is None:
        log("OTAP status: timeout")
        return None
    p = cnf["payload"]
    if len(p) < 24:
        log(f"OTAP status: short response ({len(p)}B)")
        return None
    (num_bytes, crc, seq, typ, status,
     proc_bytes, proc_crc, proc_seq, area_id,
     major, minor, maint, devel) = struct.unpack_from('<IHBBBIHBIBBBB', p)
    log(f"Scratchpad stored:  {num_bytes}B  crc=0x{crc:04x}  seq={seq}  type={typ}  status={status}")
    log(f"Scratchpad processed: {proc_bytes}B  crc=0x{proc_crc:04x}  seq={proc_seq}  area=0x{area_id:08x}")
    log(f"Firmware: v{major}.{minor}.{maint}.{devel}")
    return dict(num_bytes=num_bytes, crc=crc, seq=seq, type=typ, status=status,
                proc_bytes=proc_bytes, proc_crc=proc_crc, proc_seq=proc_seq,
                area_id=area_id, fw=(major, minor, maint, devel))


def cmd_otap_clear(conn: WapsConn, log_cb=None) -> bool:
    def log(msg):
        if log_cb:
            log_cb(msg)
        else:
            print(msg)
    # Flash erase can take several seconds — use a longer timeout than the default 3s
    cnf = conn.request(FC.SCRATCH_CLEAR_REQ, timeout=12.0)
    if cnf is None:
        log(f"OTAP clear: timeout")
        return False
    result = cnf["payload"][0] if cnf["payload"] else 0xFF
    if result == 0:
        log("OTAP clear: OK")
        return True
    if result == 1:
        # MSAP_SCRATCHPAD_CLEAR_INVALID_STATE — scratchpad already empty, treat as OK
        log("OTAP clear: already empty")
        return True
    log(f"OTAP clear: ERR result={result}")
    return False


def cmd_otap_upload(conn: WapsConn, filename: str, seq: int = 1,
                    progress_cb: Optional[Callable] = None,
                    log_cb: Optional[Callable] = None,
                    stop_stack: bool = True,
                    target_action: Optional[int] = None) -> bool:
    """Upload a scratchpad (OTAP) file to the node.
    stop_stack: stop the stack before upload and restart it after (required by firmware).
    target_action: if set (e.g. 2=propagate_and_process), write the target BEFORE
                   restarting the stack so Wirepas immediately triggers the reboot.
    progress_cb(pct, offset, total) called per block; log_cb(msg) for status.
    Returns True on success."""
    def log(msg):
        (log_cb or print)(msg)

    if not os.path.isfile(filename):
        log(f"File not found: {filename}")
        return False
    with open(filename, "rb") as f:
        fw = f.read()
    total = len(fw)
    log(f"OTAP upload: {os.path.basename(filename)}  {total} bytes  seq={seq}")

    # Firmware requires stack STOPPED for SCRATCH_START
    stack_was_started = False
    if stop_stack:
        ok = cmd_stack_stop(conn)
        if ok:
            log("Stack stopped for OTAP upload")
            stack_was_started = True
            time.sleep(0.2)  # let the node settle after stack stop
        else:
            log("Stack stop failed — attempting upload anyway")

    def _restart():
        if stop_stack and stack_was_started:
            ok = cmd_stack_start(conn)
            log(f"Stack restart: {'OK' if ok else 'FAIL'}")

    # Retry clear up to 3 times — first attempt may timeout if flash erase is slow
    for attempt in range(3):
        if cmd_otap_clear(conn, log_cb=log_cb):
            break
        if attempt < 2:
            log(f"OTAP clear retry {attempt + 2}/3…")
            time.sleep(1.0)
    else:
        log("OTAP clear failed after 3 attempts")
        _restart()
        return False

    # msap_scratchpad_start_req_t: num_bytes(I) seq(B)
    cnf = conn.request(FC.SCRATCH_START_REQ, struct.pack('<IB', total, seq))
    if cnf is None:
        log("OTAP start: timeout")
        return False
    result = cnf["payload"][0] if cnf["payload"] else 0xFF
    if result != 0:
        err = _SCRATCH_START_ERRORS.get(result, f"ERR({result})")
        log(f"OTAP start: {err}")
        return False
    log("OTAP start: OK")

    # msap_scratchpad_block_req_t: start_addr(I) num_bytes(B) bytes[N]
    offset = 0
    while offset < total:
        chunk = fw[offset:offset + SCRATCH_BLOCK_MAX]
        n     = len(chunk)
        pkt   = struct.pack('<IB', offset, n) + chunk
        cnf   = conn.request(FC.SCRATCH_BLOCK_REQ, pkt)
        if cnf is None:
            log(f"OTAP block @{offset}: timeout")
            return False
        result = cnf["payload"][0] if cnf["payload"] else 0xFF
        if result == 2:
            log(f"OTAP block @{offset}: COMPLETED_ERROR (CRC mismatch)")
            return False
        if result not in (0, 1):
            err = _SCRATCH_BLOCK_ERRORS.get(result, f"ERR({result})")
            log(f"OTAP block @{offset}: {err}")
            return False
        offset += n
        pct = offset * 100 // total
        if progress_cb:
            progress_cb(pct, offset, total)
        else:
            print(f"  [{pct:3d}%] @{offset:6d} +{n:3d}B", end="\r", flush=True)
        if result == 1:  # COMPLETED_OK — stack confirmed last block
            break

    log("OTAP upload: complete")

    cnf = conn.request(FC.SCRATCH_BOOTABLE_REQ)
    if cnf is None:
        log("OTAP bootable: timeout")
        return False
    result = cnf["payload"][0] if cnf["payload"] else 0xFF
    if result == 0:
        log("OTAP bootable: OK — scratchpad marked bootable")
    else:
        log(f"OTAP bootable: ERR result={result}")
        _restart()
        return False

    # Set target BEFORE restarting the stack.
    # OTAPv2 requires the exact CRC (not 0) so that remote nodes match the NPD target.
    # Read it back from the scratchpad status (stack is still stopped — that's fine).
    if target_action is not None:
        stored_crc = 0
        st = cmd_otap_status(conn, log_cb=log_cb)
        if st is not None:
            stored_crc = st.get("crc", 0)
            log(f"Scratchpad CRC for target: 0x{stored_crc:04x}")
        cmd_otap_target(conn, seq=seq, crc=stored_crc, action=target_action, log_cb=log_cb)

    _restart()
    return True


def cmd_otap_target(conn: WapsConn, seq: int, crc: int = 0,
                    action: int = 2, param: int = 0,
                    log_cb: Optional[Callable] = None) -> bool:
    """
    Set scratchpad processing target.
    action: 0=no_otap  1=propagate_only  2=propagate_and_process  5=legacy
    param:  delay in minutes for action=3, ignored otherwise
    """
    def log(msg):
        if log_cb: log_cb(msg)
        else: print(msg)
    # msap_scratchpad_target_write_req_t: target_sequence(B) target_crc(H) action(B) param(B)
    pkt = struct.pack('<BHBB', seq, crc, action, param)
    cnf = conn.request(FC.SCRATCH_TARGET_WRITE_REQ, pkt)
    if cnf is None:
        log("OTAP target: timeout")
        return False
    result = cnf["payload"][0] if cnf["payload"] else 0xFF
    action_name = _SCRATCH_ACTION_NAMES.get(action, str(action))
    err = _SCRATCH_TARGET_ERRORS.get(result, f"ERR({result})")
    log(f"OTAP target (seq={seq} action={action_name}): {err}")
    return result == 0


def cmd_otap_target_read(conn: WapsConn, log_cb=None) -> Optional[dict]:
    """Read the current scratchpad processing target.
    Returns dict with keys: result, seq, crc, action, param — or None on timeout."""
    def log(msg):
        if log_cb: log_cb(msg)
        else: print(msg)
    cnf = conn.request(FC.SCRATCH_TARGET_READ_REQ)
    if cnf is None:
        log("OTAP target read: timeout")
        return None
    p = cnf["payload"]
    if len(p) < 6:
        log(f"OTAP target read: short frame ({len(p)}B)")
        return None
    result, seq, crc, action, param = struct.unpack_from('<BBHBB', p)
    if result != 0:
        log(f"OTAP target read: ERR result={result}")
        return None
    action_name = _SCRATCH_ACTION_NAMES.get(action, str(action))
    log(f"OTAP target: seq={seq} crc=0x{crc:04x} action={action_name} param={param}")
    return {"result": result, "seq": seq, "crc": crc, "action": action, "param": param}


# ─── Remote API (over-the-air node configuration) ──────────────────────────────
# Wirepas Remote API: a data packet sent on src EP 255 / dst EP 240 carrying a
# sequence of TLV commands  [type(1)][len(1)][value(len)].
# The CSAP-write command (0x0D) reuses the dual-MCU CSAP function code; its value
# is [attr_id(2 LE)][attr_value(N)] — attribute size matches local CSAP.
# Session is framed by Begin (0x01) ... Update (0x05, reboot delay seconds).
REMOTE_API_SRC_EP = 255
REMOTE_API_DST_EP = 240

REMOTE_API_BEGIN        = 0x01   # start a config session
REMOTE_API_END          = 0x03   # apply settings, no reboot
REMOTE_API_CANCEL       = 0x04
REMOTE_API_UPDATE       = 0x05   # apply + reboot after delay (uint16 s)
REMOTE_API_WRITE_CSAP   = 0x0D   # write CSAP attribute (== WAPS_FUNC_CSAP_ATTR_WRITE_REQ)
REMOTE_API_READ_CSAP    = 0x0E


def _ra_tlv(cmd: int, value: bytes = b"") -> bytes:
    return bytes([cmd, len(value)]) + value


def _ra_write_csap(attr_id: int, value: bytes) -> bytes:
    return _ra_tlv(REMOTE_API_WRITE_CSAP, struct.pack('<H', attr_id) + value)


# Update countdown is in SECONDS; valid range 10..32767 (WP-RM-117 Table 5).
REMOTE_API_MIN_DELAY_S = 10
REMOTE_API_MAX_DELAY_S = 32767


def build_remote_config_payload(node_addr: Optional[int] = None,
                                net_addr: Optional[int] = None,
                                channel: Optional[int] = None,
                                reboot_delay_s: int = 10) -> bytes:
    """Build a Remote API TLV payload that updates the given CSAP attributes and
    schedules a reboot to apply them. Only non-None fields are written.

    Sequence per WP-RM-117: Cancel, Begin, Write CSAP…, End, Update.
    The End TLV is mandatory — without it the buffered writes are not committed
    and the Update is rejected with an Invalid Begin error."""
    if reboot_delay_s < REMOTE_API_MIN_DELAY_S:
        reboot_delay_s = REMOTE_API_MIN_DELAY_S
    if reboot_delay_s > REMOTE_API_MAX_DELAY_S:
        reboot_delay_s = REMOTE_API_MAX_DELAY_S

    payload  = _ra_tlv(REMOTE_API_CANCEL)   # clear any stale buffer/countdown
    payload += _ra_tlv(REMOTE_API_BEGIN)
    if node_addr is not None:
        payload += _ra_write_csap(CSAP.NODE_ID, struct.pack('<I', node_addr))
    if net_addr is not None:
        payload += _ra_write_csap(CSAP.NETWORK_ADDR,
                                  bytes([net_addr & 0xFF, (net_addr >> 8) & 0xFF,
                                         (net_addr >> 16) & 0xFF]))
    if channel is not None:
        payload += _ra_write_csap(CSAP.NETWORK_CHANNEL, struct.pack('<B', channel))
    payload += _ra_tlv(REMOTE_API_END)      # commit buffered writes
    # Update: reboot after delay (seconds) to apply settings
    payload += _ra_tlv(REMOTE_API_UPDATE, struct.pack('<H', reboot_delay_s))
    return payload


def cmd_remote_configure(conn: WapsConn, dst: int,
                         node_addr: Optional[int] = None,
                         net_addr: Optional[int] = None,
                         channel: Optional[int] = None,
                         reboot_delay_s: int = 5,
                         qos: int = 1) -> bool:
    """Send a Remote API config request to remote node `dst`.
    Changes node address / network address / RF channel, then reboots the node
    after reboot_delay_s seconds to apply. Returns True if the TX was accepted."""
    payload = build_remote_config_payload(node_addr, net_addr, channel,
                                          reboot_delay_s)
    print(f"{CYEL}Remote API → 0x{dst:08x}  "
          f"ep={REMOTE_API_SRC_EP}→{REMOTE_API_DST_EP}  "
          f"{len(payload)}B: {' '.join(f'{b:02x}' for b in payload)}{C0}")
    return cmd_send(conn, dst, REMOTE_API_DST_EP, payload,
                    src_ep=REMOTE_API_SRC_EP, qos=qos)


_REMOTE_API_TYPE_NAMES = {
    0x00: "Ping", 0x01: "Begin", 0x02: "BeginWithBackup", 0x03: "End",
    0x04: "Cancel", 0x05: "Update", 0x06: "WriteMSAP", 0x07: "ReadMSAP",
    0x0A: "WriteMSAP", 0x0B: "ReadMSAP", 0x0D: "WriteCSAP", 0x0E: "ReadCSAP",
    # responses often set the high bit
    0x80: "PingRsp", 0x81: "BeginRsp", 0x83: "EndRsp", 0x85: "UpdateRsp",
    0x8D: "WriteCSAPRsp", 0x8E: "ReadCSAPRsp",
}


def decode_remote_api_response(payload: bytes) -> str:
    """Walk the TLV fields of a Remote API response/request payload for display.
    Returns a human-readable one-line summary."""
    parts = []
    off = 0
    while off + 2 <= len(payload):
        t = payload[off]
        ln = payload[off + 1]
        val = payload[off + 2:off + 2 + ln]
        if len(val) < ln:
            parts.append(f"<truncated t=0x{t:02x} len={ln}>")
            break
        name = _REMOTE_API_TYPE_NAMES.get(t, f"0x{t:02x}")
        vhex = val.hex()
        parts.append(f"{name}[{vhex}]" if vhex else name)
        off += 2 + ln
    return "  ".join(parts) if parts else "(empty)"


# ─── CBOR decoding (minimal, dependency-free) ──────────────────────────────────
def cbor_decode(data: bytes):
    """Decode a single CBOR data item. Returns (value, bytes_consumed).
    Raises ValueError on malformed input. Supports the common subset:
    ints, byte/text strings, arrays, maps, tags, bool/null/float."""
    if not data:
        raise ValueError("empty")

    def _read(off):
        ib = data[off]
        major = ib >> 5
        minor = ib & 0x1F
        off += 1
        if minor < 24:
            val = minor
        elif minor == 24:
            val = data[off]; off += 1
        elif minor == 25:
            val = int.from_bytes(data[off:off+2], 'big'); off += 2
        elif minor == 26:
            val = int.from_bytes(data[off:off+4], 'big'); off += 4
        elif minor == 27:
            val = int.from_bytes(data[off:off+8], 'big'); off += 8
        elif minor == 31:
            val = None  # indefinite length
        else:
            raise ValueError(f"reserved minor {minor}")

        if major == 0:                    # unsigned int
            return val, off
        if major == 1:                    # negative int
            return -1 - val, off
        if major == 2:                    # byte string
            b = data[off:off+val]
            if len(b) < val:
                raise ValueError("truncated byte string")
            return bytes(b), off + val
        if major == 3:                    # text string
            s = data[off:off+val]
            if len(s) < val:
                raise ValueError("truncated text string")
            return s.decode('utf-8', 'replace'), off + val
        if major == 4:                    # array
            arr = []
            for _ in range(val):
                item, off = _read(off)
                arr.append(item)
            return arr, off
        if major == 5:                    # map
            d = {}
            for _ in range(val):
                k, off = _read(off)
                v, off = _read(off)
                d[k if isinstance(k, (int, str)) else str(k)] = v
            return d, off
        if major == 6:                    # tag — return tagged inner value
            inner, off = _read(off)
            return inner, off
        if major == 7:                    # simple / float
            if minor == 20: return False, off
            if minor == 21: return True, off
            if minor == 22: return None, off
            if minor == 23: return None, off
            if minor == 25: return _half_to_float(val), off
            if minor == 26:
                import struct as _s
                return _s.unpack('>f', val.to_bytes(4, 'big'))[0], off
            if minor == 27:
                import struct as _s
                return _s.unpack('>d', val.to_bytes(8, 'big'))[0], off
            return val, off
        raise ValueError(f"bad major {major}")

    return _read(0)


def _half_to_float(h: int) -> float:
    sign = (h >> 15) & 1
    exp  = (h >> 10) & 0x1F
    mant = h & 0x3FF
    if exp == 0:
        f = mant / 1024.0 * (2 ** -14)
    elif exp == 0x1F:
        f = float('inf') if mant == 0 else float('nan')
    else:
        f = (1 + mant / 1024.0) * (2 ** (exp - 15))
    return -f if sign else f


try:
    import cbor2 as _cbor2
except ImportError:
    _cbor2 = None


def cbor_to_str(data: bytes) -> str:
    """Decode CBOR and return a compact human-readable string, or '' if the
    payload is not valid CBOR. Uses the cbor2 library when available, with a
    minimal built-in fallback otherwise."""
    if not data:
        return ""
    if _cbor2 is not None:
        import io
        try:
            fp = io.BytesIO(data)
            value = _cbor2.CBORDecoder(fp).decode()
        except Exception:
            return ""
        # Require exact consumption to reject raw bytes with trailing junk
        if fp.tell() != len(data):
            return ""
        return _cbor_fmt(value)
    # Fallback: built-in minimal decoder
    try:
        value, consumed = cbor_decode(data)
    except Exception:
        return ""
    if consumed != len(data):
        return ""
    return _cbor_fmt(value)


def _cbor_fmt(v) -> str:
    if isinstance(v, bytes):
        return "h'" + v.hex() + "'"
    if isinstance(v, str):
        return repr(v)
    if isinstance(v, bool):
        return "true" if v else "false"
    if v is None:
        return "null"
    if isinstance(v, float):
        return f"{v:g}"
    if isinstance(v, list):
        return "[" + ", ".join(_cbor_fmt(x) for x in v) + "]"
    if isinstance(v, dict):
        return "{" + ", ".join(f"{_cbor_fmt(k)}: {_cbor_fmt(val)}"
                               for k, val in v.items()) + "}"
    return str(v)


# ─── Wirepas diagnostics (EP 247) field labels ────────────────────────────────
# The Wirepas v5 diagnostics packet (source EP 247 → 255) is a CBOR map keyed by
# integer field IDs. The official ID→meaning table is not public, so this is a
# best-effort, editable mapping. Add/correct entries here as you identify fields;
# unknown IDs render as "field_NN". Keys are the integer CBOR map keys.
DIAG_FIELD_LABELS = {
    1:  "boot_count",
    4:  "node_role",
    5:  "voltage",
    7:  "cur_access_cycle",
    8:  "max_access_cycle",
    24: "uptime_s",
    26: "scan_count",
    27: "lltx_count",
    46: "buffer_stats",
    48: "antenna",
    64: "events",
    65: "neighbor_info",
    66: "route_info",
    69: "cluster_members",
    88: "traffic_counters",
}

# Wirepas diagnostics endpoints
DIAG_SRC_EP = 247
DIAG_DST_EP = 255

# node_role (CBOR key 4) uses a SEQUENTIAL enum — different from the CSAP NodeRole bitmask.
# Confirmed from live packets: SINK_LL=2, HEADNODE_LL=4 (sequential pairs LE/LL per role type)
_DIAG_ROLE_MAP: dict[int, tuple[str, str]] = {
    1: ("SINK",     "LE"),
    2: ("SINK",     "LL"),
    3: ("HEADNODE", "LE"),
    4: ("HEADNODE", "LL"),
    5: ("SUBNODE",  "LE"),
    6: ("SUBNODE",  "LL"),
    7: ("AUTOROLE", "LE"),
    8: ("AUTOROLE", "LL"),
    9: ("ADVERTISER", "—"),
}


def _diag_cbor_raw(data: bytes) -> Optional[dict]:
    """Decode diagnostics CBOR payload to a raw Python dict, or None."""
    if _cbor2 is not None:
        import io
        try:
            raw = _cbor2.CBORDecoder(io.BytesIO(data)).decode()
        except Exception:
            return None
    else:
        try:
            raw, consumed = cbor_decode(data)
            if consumed != len(data):
                return None
        except Exception:
            return None
    return raw if isinstance(raw, dict) else None


def parse_diag_packet(data: bytes) -> Optional[dict]:
    """Decode a Wirepas EP-247 diagnostic CBOR packet into a structured dict.

    Returned keys (all optional — only present when the field is in the packet):
      role, mode, voltage_mv, access_cycle_ms, max_cycle_ms,
      uptime_s, boot_count, rssi_min, cost_to_sink
    Returns None if data is not a valid CBOR diagnostic map.
    """
    raw = _diag_cbor_raw(data)
    if raw is None:
        return None

    result: dict = {}

    # node_role (key 4) — sequential diagnostic enum (NOT the CSAP bitmask)
    if 4 in raw:
        role_val = raw[4] if isinstance(raw[4], int) else int(raw[4])
        if role_val in _DIAG_ROLE_MAP:
            result["role"], result["mode"] = _DIAG_ROLE_MAP[role_val]
        else:
            result["role"] = f"0x{role_val:02x}"
            result["mode"] = "?"

    # voltage mV (key 5)
    if 5 in raw:
        result["voltage_mv"] = raw[5]

    # current / max access cycle in ms (keys 7, 8)
    if 7 in raw:
        result["access_cycle_ms"] = raw[7]
    if 8 in raw:
        result["max_cycle_ms"] = raw[8]

    # uptime seconds (key 24), boot count (key 1)
    if 24 in raw:
        result["uptime_s"] = raw[24]
    if 1 in raw:
        result["boot_count"] = raw[1]

    # neighbor_info (key 65): list/map of neighbor entries
    # Extract minimum RSSI seen (proxy for signal quality)
    nbr = raw.get(65)
    if isinstance(nbr, list) and nbr:
        rssi_vals = []
        for entry in nbr:
            if isinstance(entry, dict):
                for k, v in entry.items():
                    if "rssi" in str(DIAG_FIELD_LABELS.get(k, k)).lower() or k == 3:
                        try:
                            rssi_vals.append(int(v))
                        except (TypeError, ValueError):
                            pass
        if rssi_vals:
            result["rssi_min"] = min(rssi_vals)

    # route_info (key 66): map/list with cost-to-sink
    rte = raw.get(66)
    if isinstance(rte, dict):
        for k, v in rte.items():
            lbl = DIAG_FIELD_LABELS.get(k, str(k))
            if "cost" in str(lbl).lower():
                try:
                    result["cost_to_sink"] = int(v)
                except (TypeError, ValueError):
                    pass
    elif isinstance(rte, list) and rte:
        entry = rte[0]
        if isinstance(entry, dict):
            for k, v in entry.items():
                if "cost" in str(DIAG_FIELD_LABELS.get(k, k)).lower():
                    try:
                        result["cost_to_sink"] = int(v)
                    except (TypeError, ValueError):
                        pass

    return result or None


def cbor_diag_to_str(data: bytes) -> str:
    """Decode an EP-247 Wirepas diagnostics CBOR map and label its integer keys
    using DIAG_FIELD_LABELS. Returns '' if not a valid CBOR map."""
    if _cbor2 is not None:
        import io
        try:
            fp = io.BytesIO(data)
            value = _cbor2.CBORDecoder(fp).decode()
        except Exception:
            return ""
        if fp.tell() != len(data):
            return ""
    else:
        try:
            value, consumed = cbor_decode(data)
        except Exception:
            return ""
        if consumed != len(data):
            return ""
    if not isinstance(value, dict):
        return cbor_to_str(data)
    parts = []
    for k, v in value.items():
        if isinstance(k, int):
            label = DIAG_FIELD_LABELS.get(k, f"field_{k}")
            parts.append(f"{label}({k})={_cbor_fmt(v)}")
        else:
            parts.append(f"{_cbor_fmt(k)}={_cbor_fmt(v)}")
    return "  ".join(parts)


# ─── CSAP attribute helpers ───────────────────────────────────────────────────
# attr_t is uint16_t (waps_frames.h). Frames (attribute_frames.h):
#   read_req_t  : attr_id(H)                       -> 2 bytes
#   read_cnf_t  : result(B) attr_id(H) attr_len(B) attr[N]
#   write_req_t : attr_id(H) attr_len(B) attr[N]
#   write_cnf_t : result(B)
_ATTR_RESULT = {
    0: "OK", 1: "UNSUPPORTED_ATTRIBUTE", 2: "INVALID_STACK_STATE",
    3: "INV_LENGTH", 4: "INV_VALUE", 5: "WRITE_ONLY", 6: "ACCESS_DENIED",
}


def csap_attr_read(conn: WapsConn, attr_id: int) -> Optional[bytes]:
    """Read a CSAP attribute. Returns raw value bytes or None on error."""
    pkt = struct.pack('<H', attr_id)
    cnf = conn.request(FC.CSAP_ATTR_READ_REQ, pkt)
    if cnf is None:
        print(f"{CRED}CSAP read attr={attr_id}: timeout{C0}")
        return None
    p = cnf["payload"]
    if len(p) < 4:
        print(f"{CRED}CSAP read attr={attr_id}: short response{C0}")
        return None
    result   = p[0]
    attr_len = p[3]
    if result != 0:
        err = _ATTR_RESULT.get(result, f"ERR({result})")
        print(f"{CRED}CSAP read attr={attr_id}: {err}{C0}")
        return None
    return p[4:4 + attr_len]


def csap_attr_write(conn: WapsConn, attr_id: int, value: bytes) -> bool:
    """Write a CSAP attribute. Returns True on success."""
    pkt = struct.pack('<HB', attr_id, len(value)) + value
    cnf = conn.request(FC.CSAP_ATTR_WRITE_REQ, pkt)
    if cnf is None:
        print(f"{CRED}CSAP write attr={attr_id}: timeout{C0}")
        return False
    p = cnf["payload"]
    if not p:
        return False
    result = p[0]
    ok = result == 0
    err = _ATTR_RESULT.get(result, f"ERR({result})")
    col = CGRN if ok else CRED
    print(f"CSAP write attr={attr_id}: {col}{err}{C0}")
    return ok


def cmd_node_info(conn: WapsConn):
    """Read and display node address, network address, channel, role, firmware."""
    fields = [
        (CSAP.NODE_ID,         "Node addr",  lambda b: f"0x{struct.unpack_from('<I', b)[0]:08x}  ({struct.unpack_from('<I', b)[0]})"),
        (CSAP.NETWORK_ADDR,    "Network",    lambda b: f"0x{(b[0] | b[1]<<8 | b[2]<<16):06x}"),
        (CSAP.NETWORK_CHANNEL, "Channel",    lambda b: str(b[0])),
        (CSAP.NODE_ROLE,       "Role",       lambda b: _ROLE_NAMES.get(b[0], f"0x{b[0]:02x}")),
        (CSAP.FIRMWARE_MAJOR,  "FW major",   lambda b: str(struct.unpack_from('<H', b)[0])),
        (CSAP.FIRMWARE_MINOR,  "FW minor",   lambda b: str(struct.unpack_from('<H', b)[0])),
        (CSAP.FIRMWARE_MAINT,  "FW maint",   lambda b: str(struct.unpack_from('<H', b)[0])),
    ]
    print(f"{CYEL}Node info:{C0}")
    for attr_id, label, fmt in fields:
        val = csap_attr_read(conn, attr_id)
        if val is not None:
            print(f"  {label:<14} {fmt(val)}")


def cmd_node_configure(conn: WapsConn, role: int, node_addr: int,
                       net_addr: int, channel: int,
                       log_cb: Optional[Callable] = None) -> bool:
    """Configure node role, address, network address and channel via CSAP.
    CSAP attribute writes require the stack to be STOPPED, so stop it first.
    Restarts the stack after writing.

    The NODE_ROLE write is special: on success the firmware calls
    Waps_uart_powerReset() which drops the write CNF even though it worked.
    We write the role LAST and confirm by reading it back (500 ms settle time)."""
    def log(msg):
        if log_cb: log_cb(msg)
        else: print(msg)

    cmd_stack_stop(conn)
    ok = True

    # Read current role to skip the write if unchanged (avoids unnecessary UART reset)
    cur_role_b = csap_attr_read(conn, CSAP.NODE_ROLE)
    cur_role = cur_role_b[0] if (cur_role_b and len(cur_role_b) >= 1) else None

    ok &= csap_attr_write(conn, CSAP.NODE_ID, struct.pack('<I', node_addr))
    net_bytes = bytes([net_addr & 0xFF, (net_addr >> 8) & 0xFF,
                       (net_addr >> 16) & 0xFF])
    ok &= csap_attr_write(conn, CSAP.NETWORK_ADDR, net_bytes)
    ok &= csap_attr_write(conn, CSAP.NETWORK_CHANNEL, struct.pack('<B', channel))

    if cur_role == role:
        log("Role unchanged — skipping role write")
    else:
        # Role write last — tolerate the lost CNF (UART power reset inside firmware)
        role_ok = csap_attr_write(conn, CSAP.NODE_ROLE, struct.pack('<B', role))
        if not role_ok:
            time.sleep(0.5)  # let the UART power reset settle (was 0.2 — too short)
            readback = csap_attr_read(conn, CSAP.NODE_ROLE)
            if readback and len(readback) >= 1 and readback[0] == role:
                log("Role confirmed by read-back (CNF was lost to UART reset)")
                role_ok = True
            else:
                log(f"Role write FAILED (wanted {role}, got {readback})")
        ok &= role_ok

    if ok:
        cmd_stack_start(conn)
    return ok


# ─── MSAP stack control ───────────────────────────────────────────────────────
# stack start result is a bitfield (app_stack_state_flags_e). 0 == started OK.
_STACK_STATE_FLAGS = [
    (1,   "STOPPED"),
    (2,   "NET_ADDR_NOT_SET"),
    (4,   "NODE_ID_NOT_SET"),
    (8,   "CHANNEL_NOT_SET"),
    (16,  "ROLE_NOT_SET"),
    (32,  "INTERESTS_MISSING"),
    (128, "ACCESS_DENIED"),
]


def _stack_state_str(flags: int) -> str:
    if flags == 0:
        return "STARTED"
    return "|".join(name for bit, name in _STACK_STATE_FLAGS if flags & bit) \
        or f"0x{flags:02x}"


def cmd_stack_start(conn: WapsConn, autostart: bool = False) -> bool:
    # msap_start_req_t: start_options(B) — autostart bit
    cnf = conn.request(FC.STACK_START_REQ, struct.pack('<B', 1 if autostart else 0))
    if cnf is None:
        print(f"{CRED}Stack start: timeout{C0}")
        return False
    p = cnf["payload"]
    flags = p[0] if p else 0xFF
    ok = flags == 0
    col = CGRN if ok else CRED
    print(f"Stack start: {col}{_stack_state_str(flags)}{C0}")
    return ok


def cmd_stack_stop(conn: WapsConn) -> bool:
    cnf = conn.request(FC.STACK_STOP_REQ)
    if cnf is None:
        print(f"{CRED}Stack stop: timeout{C0}")
        return False
    result = cnf["payload"][0] if cnf["payload"] else 0xFF
    _print_result("Stack stop", result)
    return result == 0


def cmd_poll(conn: WapsConn) -> Optional[int]:
    """Send one INDICATION_POLL_REQ.
    Returns queued indication count, or None on timeout/no response."""
    cnf = conn.request(FC.INDICATION_POLL_REQ)
    if cnf is None:
        return None
    # payload: queued(B)
    return cnf["payload"][0] if cnf["payload"] else 0


# ─── RX indication parsing / fragment reassembly ──────────────────────────────
def parse_rx_ind(p: bytes) -> Optional[dict]:
    """Parse dsap_data_rx_ind_t (non-fragmented)."""
    if len(p) < 17:
        return None
    (queued, src_addr, src_ep, dst_addr, dst_ep,
     info, delay, apdu_len) = struct.unpack_from('<BIBIBBIB', p)
    return dict(src_addr=src_addr, src_ep=src_ep, dst_addr=dst_addr,
                dst_ep=dst_ep, info=info, delay=delay,
                apdu=p[17:17 + apdu_len])


def parse_rx_frag_ind(p: bytes) -> Optional[dict]:
    """Parse dsap_data_rx_frag_ind_t (fragmented). dualmcu forces frag mode so
    even single-fragment packets arrive here."""
    # queued(B) src_addr(I) src_ep(B) dst_addr(I) dst_ep(B) info(B) delay(I)
    #   full_packet_id(H) fragment_offset_flag(H) apdu_len(B) apdu[N]
    if len(p) < 21:
        return None
    (queued, src_addr, src_ep, dst_addr, dst_ep, info, delay,
     packet_id, frag_off_flag, apdu_len) = struct.unpack_from('<BIBIBBIHHB', p)
    return dict(src_addr=src_addr, src_ep=src_ep, dst_addr=dst_addr,
                dst_ep=dst_ep, info=info, delay=delay, packet_id=packet_id,
                offset=frag_off_flag & 0x0FFF,
                last=bool(frag_off_flag & 0x8000),
                apdu=p[21:21 + apdu_len])


class FragReassembler:
    """Reassembles RX_FRAG_IND fragments into complete APDUs, keyed by
    (src_addr, packet_id). Returns the full APDU when the last fragment lands."""
    def __init__(self):
        self._partial: dict = {}

    def add(self, src_addr: int, packet_id: int, offset: int,
            last: bool, apdu: bytes) -> Optional[bytes]:
        key = (src_addr, packet_id)
        frags = self._partial.setdefault(key, {})
        frags[offset] = apdu
        if last:
            total = b"".join(frags[o] for o in sorted(frags))
            del self._partial[key]
            return total
        return None


def classify_dst(dst_addr: int):
    if dst_addr == ADDR_BROADCAST:
        return "BCAST", CYEL
    if dst_addr & ADDR_MCAST_BIT:
        return "MCAST", CMAG
    return "UCAST", CCYN


# ─── Indication handler ────────────────────────────────────────────────────────
def make_indication_handler(conn: WapsConn) -> Callable:
    reasm = FragReassembler()

    def _print_rx(ts, ptype, pcol, info, src_addr, src_ep,
                  dst_addr, dst_ep, delay, apdu):
        qos      = info & 0x03
        hops     = (info >> 2) & 0x3F
        delay_ms = delay * 1000 // 128
        print(f"{CDIM}[{ts}]{C0} "
              f"{pcol}{CBOLD}{ptype}{C0}{CBOLD}{CGRN}: "
              f"src=0x{src_addr:08x}{CDIM}({src_addr}){CGRN} "
              f"dst=0x{dst_addr:08x}{CDIM}({dst_addr}){CGRN} "
              f"ep={src_ep}→{dst_ep} hops={hops} "
              f"delay={delay_ms}ms qos={qos} ({len(apdu)}B){C0}")
        if apdu:
            print(f"  hex: {' '.join(f'{b:02x}' for b in apdu)}")
            try:
                txt = apdu.decode("utf-8")
                if txt.isprintable():
                    print(f"  str: {txt}")
            except UnicodeDecodeError:
                pass

    def _handle(frame: dict):
        func = frame["func"]
        fid  = frame["fid"]
        p    = frame["payload"]
        ts   = time.strftime("%H:%M:%S")

        if func == FC.RX_IND:
            d = parse_rx_ind(p)
            if d is None:
                return
            ptype, pcol = classify_dst(d["dst_addr"])
            _print_rx(ts, ptype, pcol, d["info"], d["src_addr"], d["src_ep"],
                      d["dst_addr"], d["dst_ep"], d["delay"], d["apdu"])
            # pld[0]=1 → node sends next queued indication automatically
            conn.respond(FC.RX_RSP, fid, struct.pack('<B', 1))

        elif func == FC.RX_FRAG_IND:
            d = parse_rx_frag_ind(p)
            if d is None:
                return
            full = reasm.add(d["src_addr"], d["packet_id"], d["offset"],
                             d["last"], d["apdu"])
            if full is not None:
                ptype, pcol = classify_dst(d["dst_addr"])
                _print_rx(ts, ptype, pcol, d["info"], d["src_addr"], d["src_ep"],
                          d["dst_addr"], d["dst_ep"], d["delay"], full)
            # ack with pld[0]=1 to chain next indication
            conn.respond(FC.RX_FRAG_RSP, fid, struct.pack('<B', 1))

        elif func == FC.STACK_STATE_IND:
            # msap_state_ind_t: queued(B) result(B)
            result = p[1] if len(p) >= 2 else 0xFF
            state = "STARTED" if result == 0 else f"flags=0x{result:02x}"
            print(f"{CDIM}[{ts}]{C0} {CYEL}Stack state: {state}{C0}")
            # MUST ack, else it stays at head of queue and blocks all RX
            conn.respond(FC.STACK_STATE_RSP, fid, struct.pack('<B', 1))

        elif func == FC.TX_IND:
            if len(p) < 14:
                return
            # dsap_data_tx_ind_t:
            # queued(B) apdu_id(H) src_ep(B) dst_addr(I) dst_ep(B) queue_delay(I) result(B)
            (queued, apdu_id, src_ep, dst_addr, dst_ep,
             queue_delay, result) = struct.unpack_from('<BHBIBIB', p)
            err = _DSAP_TX_ERRORS.get(result, f"ERR({result})")
            col = CGRN if result == 0 else CRED
            print(f"{CDIM}[{ts}]{C0} TX-IND apdu_id={apdu_id} "
                  f"dst=0x{dst_addr:08x} {col}{err}{C0}")

        elif func == FC.APP_CONFIG_RX_IND:
            if len(p) < 4:
                return
            # msap_int_ind_t: queued(B) seq(B) interval(H) config[80]
            queued, seq, interval = struct.unpack_from('<BBH', p)
            config = p[4:]
            print(f"{CDIM}[{ts}]{C0} {CYEL}AppConfig RX: "
                  f"seq={seq}  interval={interval}s  len={len(config)}{C0}")
            _hexdump(config, indent=2)
            conn.respond(FC.APP_CONFIG_RX_RSP, fid, struct.pack('<B', 1))

        elif func == FC.CDD_IND:
            if len(p) < 4:
                return
            # msap_config_data_item_ind_t: queued(B) npd_endpoint(H) npd_payload_len(B) payload[N]
            queued   = p[0]
            type_id  = struct.unpack_from('<H', p, 1)[0]
            plen     = p[3]
            data     = p[4:4 + plen]
            print(f"{CDIM}[{ts}]{C0} {CYEL}CDD IND: "
                  f"type=0x{type_id:04x} ({type_id})  len={plen}{C0}")
            _hexdump(data, indent=2)
            conn.respond(FC.CDD_RSP, fid, struct.pack('<B', 1))

    return _handle


# ─── Helpers ──────────────────────────────────────────────────────────────────
def _hexdump(data: bytes, indent: int = 0):
    pfx = " " * indent
    for off in range(0, max(len(data), 1), 16):
        chunk   = data[off:off + 16]
        hex_str = " ".join(f"{b:02x}" for b in chunk)
        asc_str = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
        print(f"{pfx}[{off:03d}]  {hex_str:<47}  {asc_str}")


def _print_result(label: str, result: int):
    col = CGRN if result == 0 else CRED
    msg = "OK" if result == 0 else f"ERR result={result}"
    print(f"{label}: {col}{msg}{C0}")


def _parse_addr(s: str) -> int:
    return int(s, 0)


def _parse_data(s: str, use_hex: bool) -> bytes:
    if use_hex:
        return bytes.fromhex(s.replace(" ", ""))
    return s.encode("utf-8")


# ─── CLI ──────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        prog="wirepas_uart.py",
        description="Wirepas WAPS UART tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    parser.add_argument("-p", "--port",     required=True, help="Serial port (e.g. /dev/ttyUSB0)")
    parser.add_argument("-b", "--baudrate", type=int, default=115200)
    parser.add_argument("-t", "--timeout",  type=float, default=3.0,
                        help="Request timeout in seconds (default 3)")

    sub = parser.add_subparsers(dest="cmd", required=True)

    # listen ──────────────────────────────────────────────────────────────────
    sub.add_parser("listen", help="Receive all downlink messages (Ctrl+C to stop)")

    # send ────────────────────────────────────────────────────────────────────
    s = sub.add_parser("send", help="Send a data message to the network")
    s.add_argument("type", choices=["unicast", "multicast", "broadcast"],
                   help="Addressing mode")
    s.add_argument("--dst",    default="0xFFFFFFFF",
                   help="Destination address in hex (0x…) or decimal")
    s.add_argument("--src-ep", type=int, default=1, dest="src_ep",
                   help="Source endpoint (default 1)")
    s.add_argument("--dst-ep", type=int, default=1, dest="dst_ep",
                   help="Destination endpoint (default 1)")
    s.add_argument("--qos",    type=int, default=0, choices=[0, 1],
                   help="QoS class: 0=normal 1=high")
    s.add_argument("--hex",    action="store_true", dest="use_hex",
                   help="Payload is a hex string (e.g. deadbeef01)")
    s.add_argument("payload",  help="Payload as text (default) or hex (--hex)")

    # appconfig ───────────────────────────────────────────────────────────────
    ac   = sub.add_parser("appconfig", help="Legacy AppConfig (MSAP-APP_CONFIG)")
    ac_s = ac.add_subparsers(dest="ac_cmd", required=True)
    ac_s.add_parser("read", help="Read current AppConfig from sink")
    w = ac_s.add_parser("write", help="Write AppConfig to network")
    w.add_argument("--seq",      type=int, default=1)
    w.add_argument("--interval", type=int, default=30,
                   help="Propagation interval in seconds (default 30)")
    w.add_argument("--hex",      action="store_true", dest="use_hex")
    w.add_argument("data",       help="Config payload (max 80 bytes)")

    # cdd ─────────────────────────────────────────────────────────────────────
    cdd   = sub.add_parser("cdd", help="Config Data Item / CDD (MSAP-CONFIG_DATA_ITEM)")
    cdd_s = cdd.add_subparsers(dest="cdd_cmd", required=True)
    cdd_s.add_parser("list", help="List all registered CDD type IDs")
    cg = cdd_s.add_parser("get", help="Read a CDD item by type")
    cg.add_argument("type", help="Type ID (hex 0x… or decimal)")
    cs = cdd_s.add_parser("set", help="Write a CDD item")
    cs.add_argument("type", help="Type ID (hex 0x… or decimal)")
    cs.add_argument("--hex", action="store_true", dest="use_hex")
    cs.add_argument("data",  help="Payload (max 80 bytes)")

    # otap ────────────────────────────────────────────────────────────────────
    ot   = sub.add_parser("otap", help="OTAP scratchpad management")
    ot_s = ot.add_subparsers(dest="otap_cmd", required=True)
    ot_s.add_parser("status", help="Read scratchpad status")
    ot_s.add_parser("clear",  help="Erase scratchpad storage")
    up = ot_s.add_parser("upload",
                         help="Upload .otap file, set bootable (clear → start → blocks → bootable)")
    up.add_argument("file",  help="Path to .otap firmware file")
    up.add_argument("--seq", type=int, default=1,
                    help="Scratchpad sequence number (default 1)")
    tg = ot_s.add_parser("target",
                         help="Write scratchpad processing target (triggers network update)")
    tg.add_argument("--seq",    type=int, default=1,
                    help="Target scratchpad sequence number")
    tg.add_argument("--crc",    type=int, default=0,
                    help="Target scratchpad CRC (use otap status to read)")
    tg.add_argument("--action", type=int, default=2,
                    help="Action: 0=none 1=propagate 2=propagate+process 5=legacy (default 2)")
    tg.add_argument("--param",  type=int, default=0,
                    help="Action parameter (delay in minutes for action=3)")

    args = parser.parse_args()

    conn = WapsConn(args.port, args.baudrate, args.timeout)
    conn.add_callback(make_indication_handler(conn))
    conn.open()
    print(f"Connected  {args.port}  @ {args.baudrate} baud")

    try:
        if args.cmd == "listen":
            print("Listening for messages… Ctrl+C to stop")
            while True:
                time.sleep(0.1)

        elif args.cmd == "send":
            if args.type == "broadcast":
                dst = ADDR_BROADCAST
            else:
                dst = _parse_addr(args.dst)
                if args.type == "multicast" and not (dst & ADDR_MCAST_BIT):
                    print(f"{CYEL}Warning: 0x{dst:08x} has no multicast bit (0x80000000){C0}")
            data = _parse_data(args.payload, args.use_hex)
            cmd_send(conn, dst, args.dst_ep, data, args.src_ep, args.qos)

        elif args.cmd == "appconfig":
            if args.ac_cmd == "read":
                cmd_appconfig_read(conn)
            else:
                data = _parse_data(args.data, args.use_hex)
                cmd_appconfig_write(conn, args.seq, args.interval, data)

        elif args.cmd == "cdd":
            if args.cdd_cmd == "list":
                cmd_cdd_list(conn)
            elif args.cdd_cmd == "get":
                cmd_cdd_get(conn, int(args.type, 0))
            elif args.cdd_cmd == "set":
                data = _parse_data(args.data, args.use_hex)
                cmd_cdd_set(conn, int(args.type, 0), data)

        elif args.cmd == "otap":
            if args.otap_cmd == "status":
                cmd_otap_status(conn)
            elif args.otap_cmd == "clear":
                cmd_otap_clear(conn)
            elif args.otap_cmd == "upload":
                cmd_otap_upload(conn, args.file, args.seq)
            elif args.otap_cmd == "target":
                cmd_otap_target(conn, args.seq, args.crc, args.action, args.param)

    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        conn.close()


if __name__ == "__main__":
    main()
