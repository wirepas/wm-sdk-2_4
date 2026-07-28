#!/usr/bin/env python3
"""
wirepas_mqtt.py — Wirepas backend (MQTT / MQTTS) transport for wirepas_gui.py

Presents the *same* interface as wirepas_uart.WapsConn so the GUI can talk to a
Wirepas Gateway-to-Backend broker exactly like a locally attached serial sink.
The WAPS request/response and RX-indication frames the GUI already speaks are
translated to/from the Wirepas backend protobuf API:

    conn.request(func, payload)   →  SendData / GetConfigs / SetConfig ...
    conn.add_callback(cb)         ←  received_data events (as synthetic RX_IND)

Requires:  pip install wirepas-mqtt-library     (pulls paho-mqtt + protobuf)

Supports mqtt and mqtts (TLS) with username/password. TLS is on by default;
`insecure=True` keeps TLS but skips certificate verification.
"""

import struct
import threading
import time
from typing import Callable, Optional

from wirepas_uart import FC, CSAP

try:
    from wirepas_mqtt_library import WirepasNetworkInterface
    _MQTT_LIB_OK = True
    _MQTT_LIB_ERR = ""
except Exception as _e:                                    # pragma: no cover
    WirepasNetworkInterface = None                         # type: ignore
    _MQTT_LIB_OK = False
    _MQTT_LIB_ERR = str(_e)

try:
    from wirepas_mesh_messaging.otap_helper import ScratchpadAction
except Exception:                                          # pragma: no cover
    ScratchpadAction = None

# OTAP action code (dualmcu/serial convention) ↔ backend ScratchpadAction enum.
def _scratchpad_action(code: int):
    if ScratchpadAction is None:
        return code
    return {
        0: ScratchpadAction.ACTION_NO_OTAP,
        1: ScratchpadAction.ACTION_PROPAGATE_ONLY,
        2: ScratchpadAction.ACTION_PROPAGATE_AND_PROCESS,
        3: ScratchpadAction.ACTION_PROPAGATE_AND_PROCESS_WITH_DELAY,
        4: ScratchpadAction.ACTION_LEGACY_OTAP,
    }.get(code, ScratchpadAction.ACTION_NO_OTAP)


def _action_to_code(action) -> int:
    if ScratchpadAction is None:
        try:
            return int(action)
        except Exception:
            return 0
    for code, a in ((0, ScratchpadAction.ACTION_NO_OTAP),
                    (1, ScratchpadAction.ACTION_PROPAGATE_ONLY),
                    (2, ScratchpadAction.ACTION_PROPAGATE_AND_PROCESS),
                    (3, ScratchpadAction.ACTION_PROPAGATE_AND_PROCESS_WITH_DELAY),
                    (4, ScratchpadAction.ACTION_LEGACY_OTAP)):
        if action == a:
            return code
    return 0


def mqtt_lib_available() -> tuple[bool, str]:
    """Return (ok, hint). hint tells the user how to enable MQTT support."""
    if _MQTT_LIB_OK:
        return True, ""
    return False, ("wirepas-mqtt-library not installed — "
                   "run:  pip install wirepas-mqtt-library")


def _fw_parts(cfg: dict) -> list[int]:
    """Extract [major, minor, maint, dev] from a backend sink config."""
    fw = cfg.get("firmware_version")
    if fw is None:
        return [0, 0, 0, 0]
    if isinstance(fw, (list, tuple)):
        parts = list(fw)
    else:
        parts = [int(x) for x in str(fw).replace("-", ".").split(".") if x.isdigit()]
    parts = (parts + [0, 0, 0, 0])[:4]
    return [int(x) & 0xFFFF for x in parts]


def _as_int(v, default=0) -> int:
    """Coerce a backend field (int / enum / obj with .value) to int."""
    if v is None:
        return default
    if isinstance(v, int):
        return v
    for attr in ("value", "role"):
        if hasattr(v, attr):
            try:
                return int(getattr(v, attr))
            except Exception:
                pass
    try:
        return int(v)
    except Exception:
        return default


class MqttConn:
    """Drop-in replacement for WapsConn, backed by a Wirepas MQTT backend."""

    def __init__(self, host: str, port: int = 8883,
                 username: str = "", password: str = "",
                 tls: bool = True, insecure: bool = False,
                 gw: Optional[str] = None, sink: Optional[str] = None,
                 timeout: float = 5.0):
        self._host = host
        self._port = int(port)
        self._user = username
        self._pass = password
        self._tls = tls
        self._insecure = insecure
        self._timeout = timeout
        self.gw = gw
        self.sink = sink
        self._wni = None
        self._callbacks: list[Callable] = []
        self._apdu_id = 0
        self._sinks: list[tuple] = []          # [(gw, sink, cfg), ...]
        self._cfg: dict = {}                   # current sink cached config
        self._pending_cfg: dict = {}           # staged CSAP writes → flush on start
        self._running = False
        self._log = None                       # optional GUI logger callback

    def set_logger(self, fn):
        self._log = fn

    def _logmsg(self, msg: str):
        if self._log:
            try:
                self._log(msg)
            except Exception:
                pass

    # ── lifecycle ────────────────────────────────────────────────────────────
    def open(self):
        if not _MQTT_LIB_OK:
            raise RuntimeError(_MQTT_LIB_ERR or "wirepas-mqtt-library missing")
        # WirepasNetworkInterface always uses TLS; `insecure` skips cert check.
        # For a plain-mqtt broker, insecure=True is the closest supported mode.
        kwargs = dict(insecure=self._insecure or not self._tls)
        # gw_timeout_s: how long get_sinks() waits for each gateway's config
        # response. The 2 s default often expires → 0 sinks (unknown sink id).
        try:
            self._wni = WirepasNetworkInterface(
                self._host, self._port, self._user, self._pass,
                gw_timeout_s=max(5, int(self._timeout)), **kwargs)
        except TypeError:
            self._wni = WirepasNetworkInterface(
                self._host, self._port, self._user, self._pass, **kwargs)
        self._running = True
        self._wni.register_data_cb(self._on_data)
        # Gateway/sink discovery is passive (broker pushes gw-event/status,
        # often retained). Do NOT query here — it races ahead of those
        # messages and returns empty. The caller runs discover() in a thread.

    def close(self):
        self._running = False
        wni, self._wni = self._wni, None
        if wni is not None:
            for m in ("close", "disconnect", "stop"):
                if hasattr(wni, m):
                    try:
                        getattr(wni, m)()
                    except Exception:
                        pass
                    break

    def add_callback(self, cb: Callable):
        self._callbacks.append(cb)

    # ── gateway / sink discovery ─────────────────────────────────────────────
    def refresh_sinks(self) -> list[tuple]:
        """Query gateways/sinks. Returns [(gw_id, sink_id, cfg), ...]."""
        self._sinks = []
        if self._wni is None:
            return self._sinks
        try:
            raw = self._wni.get_sinks()
        except Exception:
            raw = []
        for entry in raw:
            try:
                gw, sink, cfg = entry[0], entry[1], (entry[2] if len(entry) > 2 else {})
            except Exception:
                continue
            self._sinks.append((gw, sink, dict(cfg) if cfg else {}))
        # Default selection = first sink
        if self._sinks and (self.gw is None or self.sink is None):
            self.gw, self.sink, self._cfg = self._sinks[0]
        else:
            self._sync_cfg()
        return self._sinks

    def list_sinks(self) -> list[tuple]:
        return list(self._sinks)

    def list_gateways(self) -> list:
        """Gateways currently seen on the broker (from their status messages)."""
        if self._wni is None:
            return []
        try:
            return list(self._wni.get_gateways())
        except Exception:
            return []

    def discover(self, timeout: float = 6.0, interval: float = 0.7) -> list[tuple]:
        """Poll for sinks until at least one appears or `timeout` elapses.
        Gateway status arrives asynchronously after subscribing, so a few
        retries are needed right after connecting."""
        deadline = time.time() + timeout
        while self._running:
            self.refresh_sinks()
            # Stop as soon as we see a gateway (sinks may still be empty if the
            # gateway's config hasn't been fetched yet — the user can pick sink0).
            if self._sinks or self.list_gateways() or time.time() >= deadline:
                break
            time.sleep(interval)
        return self._sinks

    def select(self, gw: str, sink: str):
        self.gw, self.sink = gw, sink
        self._pending_cfg.clear()
        self._sync_cfg()

    def _sync_cfg(self):
        for gw, sink, cfg in self._sinks:
            if gw == self.gw and sink == self.sink:
                self._cfg = dict(cfg)
                return
        self._cfg = {}

    def _cur_cfg(self) -> dict:
        """Cached config merged with staged (not-yet-applied) CSAP writes."""
        merged = dict(self._cfg)
        merged.update(self._pending_cfg)
        return merged

    # ── uplink: backend event → synthetic WAPS RX_IND ────────────────────────
    def sniff_topics(self, duration: float = 6.0):
        """Diagnostic: log every unique topic published on the broker for a few
        seconds, to confirm uplinks flow on gw-event/received_data/…"""
        cli = getattr(self._wni, "_mqtt_client", None)
        if cli is None:
            self._logmsg("sniff: no internal mqtt client")
            return
        seen: dict = {}

        def _cb(client, userdata, msg):
            if msg.topic not in seen:
                seen[msg.topic] = len(msg.payload)
                self._logmsg(f"topic: {msg.topic}  ({len(msg.payload)}B)")

        self._logmsg(f"sniffing broker topics for {duration:.0f}s …")
        try:
            cli.message_callback_add("#", _cb)
            cli.subscribe("#", qos=0)
            time.sleep(duration)
        finally:
            try:
                cli.unsubscribe("#")
            except Exception:
                pass
            try:
                cli.message_callback_remove("#")
            except Exception:
                pass
        self._logmsg(f"sniff done: {len(seen)} unique topic(s)")

    # ── OTAP (scratchpad) over the backend ───────────────────────────────────
    def otap_status(self, timeout: float = 15.0) -> Optional[dict]:
        """Return a dict compatible with the GUI's OTAP status/target views.
        Uses the async callback form (the blocking form times out after 2 s,
        which is often too short for the gateway to answer an OTAP query)."""
        if self._wni is None or self.gw is None:
            return None
        box: dict = {}
        ev = threading.Event()

        def _cb(gw_res, _param, status):
            box["res"], box["status"] = gw_res, status
            ev.set()

        try:
            self._wni.get_scratchpad_status(self.gw, self.sink, cb=_cb)
        except Exception as e:
            self._logmsg(f"OTAP status error: {e}")
            return None
        if not ev.wait(timeout):
            self._logmsg(f"OTAP status: no gateway response within {timeout:.0f}s")
            return None
        st = box.get("status")
        if not st:
            self._logmsg(f"OTAP status: {box.get('res')}")
            return None
        stored = st.get("stored_scratchpad") or {}
        proc = st.get("processed_scratchpad") or {}
        tgt = st.get("target_scratchpad_and_action") or {}
        return {
            "num_bytes": stored.get("len", 0), "crc": stored.get("crc", 0),
            "seq": stored.get("seq", 0), "type": st.get("stored_type", 0),
            "status": st.get("stored_status", 0),
            "proc_bytes": proc.get("len", 0), "proc_crc": proc.get("crc", 0),
            "proc_seq": proc.get("seq", 0), "area_id": 0,
            "fw": _fw_parts(self._cur_cfg()),
            "target_seq": tgt.get("target_sequence", 0) or 0,
            "target_crc": tgt.get("target_crc", 0) or 0,
            "target_action": _action_to_code(tgt.get("action", 0)),
        }

    def otap_read_target(self) -> Optional[dict]:
        st = self.otap_status()
        if st is None:
            return None
        return {"seq": st["target_seq"], "crc": st["target_crc"],
                "action": st["target_action"]}

    def otap_set_target(self, code: int, seq: int, crc: int, param: int = 0,
                        timeout: float = 15.0) -> bool:
        if self._wni is None or self.gw is None:
            return False
        target = {"action": _scratchpad_action(code),
                  "target_sequence": seq, "target_crc": crc, "param": param}
        box: dict = {}
        ev = threading.Event()

        def _cb(gw_res, _param):
            box["res"] = gw_res
            ev.set()

        try:
            self._wni.set_target_scratchpad(self.gw, self.sink, target, cb=_cb)
        except Exception as e:
            self._logmsg(f"OTAP set target error: {e}")
            return False
        if not ev.wait(timeout):
            self._logmsg(f"OTAP set target: no gateway response within {timeout:.0f}s")
            return False
        return "OK" in str(box.get("res"))

    def otap_upload(self, data: bytes, seq: int) -> bool:
        if self._wni is None or self.gw is None:
            return False
        try:
            res = self._wni.upload_scratchpad(self.gw, self.sink, seq, bytes(data))
        except Exception as e:
            self._logmsg(f"OTAP upload error: {e}")
            return False
        return "OK" in str(res)

    def otap_clear(self) -> bool:
        if self._wni is None or self.gw is None:
            return False
        try:  # upload with scratchpad=None clears the stored one
            res = self._wni.upload_scratchpad(self.gw, self.sink, 0, None)
        except Exception as e:
            self._logmsg(f"OTAP clear error: {e}")
            return False
        return "OK" in str(res)

    def otap_process(self) -> bool:
        if self._wni is None or self.gw is None:
            return False
        try:
            res = self._wni.process_scratchpad(self.gw, self.sink)
        except Exception as e:
            self._logmsg(f"OTAP process error: {e}")
            return False
        return "OK" in str(res)

    def _on_data(self, data):
        if not self._running:
            return
        try:
            src_addr = _as_int(getattr(data, "source_address", 0))
            dst_addr = _as_int(getattr(data, "destination_address", src_addr), src_addr)
            src_ep = _as_int(getattr(data, "source_endpoint", 0))
            dst_ep = _as_int(getattr(data, "destination_endpoint", 0))
            qos = _as_int(getattr(data, "qos", 0)) & 0x03
            hops = _as_int(getattr(data, "hop_count", 0)) & 0x3F
            travel_ms = _as_int(getattr(data, "travel_time_ms", 0))
            apdu = getattr(data, "data_payload", b"") or b""
            if isinstance(apdu, str):
                apdu = apdu.encode("latin-1", "ignore")
        except Exception:
            return
        info = (qos & 0x03) | ((hops & 0x3F) << 2)
        delay = (travel_ms * 128) // 1000            # RX_IND delay is in 1/128 s
        # dsap_data_rx_ind_t: queued(B) src(I) sep(B) dst(I) dep(B) info(B) delay(I) len(B)
        pld = struct.pack('<BIBIBBIB', 0, src_addr & 0xFFFFFFFF, src_ep,
                          dst_addr & 0xFFFFFFFF, dst_ep, info, delay & 0xFFFFFFFF,
                          len(apdu)) + bytes(apdu)
        frame = {"func": FC.RX_IND, "fid": 0, "payload": pld}
        for cb in self._callbacks:
            try:
                cb(frame)
            except Exception:
                pass

    # ── downlink / control: WAPS request → backend action ────────────────────
    def request(self, func: int, payload: bytes = b"",
                timeout: Optional[float] = None) -> Optional[dict]:
        try:
            if func == FC.TX_REQ:
                return self._req_tx(payload)
            if func == FC.INDICATION_POLL_REQ:
                return self._cnf(FC.INDICATION_POLL_CNF, b"\x00")   # 0 queued
            if func == FC.STACK_START_REQ:
                return self._req_stack(True)
            if func == FC.STACK_STOP_REQ:
                return self._req_stack(False)
            if func == FC.CSAP_ATTR_READ_REQ:
                return self._req_csap_read(payload)
            if func == FC.CSAP_ATTR_WRITE_REQ:
                return self._req_csap_write(payload)
            if func == FC.APP_CONFIG_READ_REQ:
                return self._req_appcfg_read()
            if func == FC.APP_CONFIG_WRITE_REQ:
                return self._req_appcfg_write(payload)
        except Exception:
            return None
        # Everything else (CDD, local scratchpad/OTAP) has no backend equivalent
        return None

    def respond(self, func_rsp: int, fid: int, payload: bytes = b""):
        # Backend pushes events; there is no per-indication ack to send.
        return

    # ── request helpers ──────────────────────────────────────────────────────
    @staticmethod
    def _cnf(func: int, payload: bytes) -> dict:
        return {"func": func, "fid": 0, "payload": payload}

    def _req_tx(self, payload: bytes) -> Optional[dict]:
        # dsap_data_tx_req_t header is <HBIBBBB> = 11 bytes, then the APDU.
        _HDR = struct.calcsize('<HBIBBBB')            # == 11 (not 12!)
        if len(payload) < _HDR:
            return None
        (apdu_id, src_ep, dst, dst_ep, qos, _opts, apdu_len) = \
            struct.unpack_from('<HBIBBBB', payload)
        apdu = payload[_HDR:_HDR + apdu_len]
        if self._wni is None or self.gw is None or self.sink is None:
            self._logmsg(f"TX aborted: no gateway/sink selected "
                         f"(gw={self.gw!r} sink={self.sink!r})")
            return self._cnf(FC.TX_CNF, struct.pack('<HBB', apdu_id, 1, 0))  # STACK_STOPPED
        result = 0
        dst32 = dst & 0xFFFFFFFF

        def _sent_cb(gw_res, _param=None):
            # Async gateway ack — logged only if not OK, to keep the log clean.
            if "OK" not in str(gw_res):
                self._logmsg(f"TX result dst=0x{dst32:08x} ep {src_ep}->{dst_ep}: {gw_res}")

        try:
            # Async (cb set) → returns immediately; no blocking wait for the
            # gateway response (which timed out when chaining several sends).
            self._wni.send_message(self.gw, self.sink, dst32,
                                   src_ep, dst_ep, bytes(apdu), qos=qos,
                                   cb=_sent_cb)
        except Exception as e:
            self._logmsg(f"TX send_message error: {e}")
            result = 4                                       # OUT_OF_MEMORY-ish
        return self._cnf(FC.TX_CNF, struct.pack('<HBB', apdu_id, result, 255))

    def _req_stack(self, start: bool) -> dict:
        cnf_func = FC.STACK_START_CNF if start else FC.STACK_STOP_CNF
        if self._wni is None or self.gw is None:
            return self._cnf(cnf_func, b"\x01")             # STOPPED / error flag
        flags = 0
        try:
            if start:
                # Flush any staged CSAP writes as one sink-config update, then start.
                if self._pending_cfg:
                    self._apply_pending_cfg(started=True)
                elif hasattr(self._wni, "start_stack"):
                    self._wni.start_stack(self.gw, self.sink)
                else:
                    self._wni.set_sink_config(self.gw, self.sink, {"started": True})
            else:
                if hasattr(self._wni, "stop_stack"):
                    self._wni.stop_stack(self.gw, self.sink)
                else:
                    self._wni.set_sink_config(self.gw, self.sink, {"started": False})
            self.refresh_sinks()
        except Exception as e:
            self._logmsg(f"stack {'start' if start else 'stop'} error: {e}")
            flags = 1
        return self._cnf(cnf_func, struct.pack('<B', flags))

    def _apply_pending_cfg(self, started: Optional[bool] = None):
        new = dict(self._pending_cfg)
        if started is not None:
            new["started"] = started
        self._wni.set_sink_config(self.gw, self.sink, new)
        self._cfg.update(new)
        self._pending_cfg.clear()

    def _req_csap_read(self, payload: bytes) -> Optional[dict]:
        if len(payload) < 2:
            return None
        attr_id = struct.unpack_from('<H', payload)[0]
        cfg = self._cur_cfg()
        val: Optional[bytes] = None
        if attr_id == CSAP.NODE_ID:
            val = struct.pack('<I', _as_int(cfg.get("node_address")) & 0xFFFFFFFF)
        elif attr_id == CSAP.NETWORK_ADDR:
            n = _as_int(cfg.get("network_address"))
            val = bytes([n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF])
        elif attr_id == CSAP.NETWORK_CHANNEL:
            val = struct.pack('<B', _as_int(cfg.get("network_channel")) & 0xFF)
        elif attr_id == CSAP.NODE_ROLE:
            val = struct.pack('<B', _as_int(cfg.get("node_role")) & 0xFF)
        elif attr_id in (CSAP.FIRMWARE_MAJOR, CSAP.FIRMWARE_MINOR,
                         CSAP.FIRMWARE_MAINT, CSAP.FIRMWARE_DEV):
            idx = {CSAP.FIRMWARE_MAJOR: 0, CSAP.FIRMWARE_MINOR: 1,
                   CSAP.FIRMWARE_MAINT: 2, CSAP.FIRMWARE_DEV: 3}[attr_id]
            val = struct.pack('<H', _fw_parts(cfg)[idx])
        if val is None:
            # attr not available from backend → report "invalid range" (result 2)
            return self._cnf(FC.CSAP_ATTR_READ_CNF, struct.pack('<BHB', 2, attr_id, 0))
        # result(B)=0, attr_id(H), attr_len(B), value
        return self._cnf(FC.CSAP_ATTR_READ_CNF,
                         struct.pack('<BHB', 0, attr_id, len(val)) + val)

    def _req_csap_write(self, payload: bytes) -> Optional[dict]:
        if len(payload) < 3:
            return None
        attr_id, vlen = struct.unpack_from('<HB', payload)
        val = payload[3:3 + vlen]
        if attr_id == CSAP.NODE_ID and len(val) >= 4:
            self._pending_cfg["node_address"] = struct.unpack_from('<I', val)[0]
        elif attr_id == CSAP.NETWORK_ADDR and len(val) >= 3:
            self._pending_cfg["network_address"] = val[0] | (val[1] << 8) | (val[2] << 16)
        elif attr_id == CSAP.NETWORK_CHANNEL and len(val) >= 1:
            self._pending_cfg["network_channel"] = val[0]
        elif attr_id == CSAP.NODE_ROLE and len(val) >= 1:
            self._pending_cfg["node_role"] = val[0]
        else:
            return self._cnf(FC.CSAP_ATTR_WRITE_CNF, b"\x02")   # invalid attr
        return self._cnf(FC.CSAP_ATTR_WRITE_CNF, b"\x00")       # OK (applied on start)

    def _req_appcfg_read(self) -> dict:
        cfg = self._cur_cfg()
        seq = _as_int(cfg.get("app_config_seq")) & 0xFF
        interval = _as_int(cfg.get("app_config_diag")) & 0xFFFF
        data = cfg.get("app_config_data") or b""
        if isinstance(data, str):
            try:
                data = bytes.fromhex(data)
            except ValueError:
                data = data.encode("latin-1", "ignore")
        # msap_int_read_cnf_t: result(B) seq(B) interval(H) config[]
        pld = struct.pack('<BBH', 0, seq, interval) + bytes(data)
        return self._cnf(FC.APP_CONFIG_READ_CNF, pld)

    def _req_appcfg_write(self, payload: bytes) -> Optional[dict]:
        if len(payload) < 3:
            return None
        seq, interval = struct.unpack_from('<BH', payload)
        data = payload[3:].rstrip(b"\xff")
        if self._wni is None or self.gw is None:
            return self._cnf(FC.APP_CONFIG_WRITE_CNF, b"\x01")
        result = 0
        try:
            self._wni.set_sink_config(self.gw, self.sink, {
                "app_config_seq": seq,
                "app_config_diag": interval,
                "app_config_data": bytes(data),
            })
            self._cfg.update({"app_config_seq": seq, "app_config_diag": interval,
                              "app_config_data": bytes(data)})
        except Exception as e:
            self._logmsg(f"appconfig write error: {e}")
            result = 2
        return self._cnf(FC.APP_CONFIG_WRITE_CNF, struct.pack('<B', result))
