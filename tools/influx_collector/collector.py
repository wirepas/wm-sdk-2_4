#!/usr/bin/env python3
"""
collector.py — Wirepas MQTT backend → InfluxDB collector.

Subscribes to a Wirepas Gateway-to-Backend broker (mqtts) via
wirepas-mqtt-library, decodes the per-endpoint CBOR uplinks the sensorv26 /
nrf54L15_tag firmware sends, and writes them to InfluxDB using the HTTP
line-protocol API (`/api/v2/write`) — which is accepted by both InfluxDB 2.x
and 3.x, so the same code targets either version.

All configuration is via environment variables (see .env.example). Designed to
run as a long-lived container (docker compose).
"""

import os
import sys
import json
import time
import threading
import logging
from typing import Optional

import requests

try:
    import cbor2
except ImportError:
    print("Missing dependency: pip install cbor2", file=sys.stderr)
    sys.exit(1)

try:
    from wirepas_mqtt_library import WirepasNetworkInterface
except ImportError:
    print("Missing dependency: pip install wirepas-mqtt-library", file=sys.stderr)
    sys.exit(1)

# Transitive dependency of wirepas-mqtt-library (pulled in automatically) -
# used here to decode the official gw-event/status StatusEvent protobuf.
import wirepas_mesh_messaging as wmm


logging.basicConfig(
    level=os.getenv("LOG_LEVEL", "INFO").upper(),
    format="%(asctime)s %(levelname)s %(message)s",
)
log = logging.getLogger("wirepas-influx")


def _env(name: str, default: Optional[str] = None, required: bool = False) -> str:
    v = os.getenv(name, default)
    if required and not v:
        log.error("Missing required environment variable %s", name)
        sys.exit(2)
    return v


# ── Endpoint → (measurement, field names) ─────────────────────────────────────
# Values are decoded from a CBOR array of numbers; extra elements become v<i>.
ENDPOINT_MAP = {
    9:  ("aem",   ["vsto", "vsrc", "temp_c", "power_uw", "status", "charging"]),
    11: ("ctn",   ["temp_c", "r_t", "diag"]),
    12: ("sp110", ["irradiance_wm2", "mv", "diag"]),
    13: ("davis", ["wind_speed_ms", "wind_gust_ms", "wind_dir_deg"]),
    30: ("pt100", ["temp_c", "resistance_ohm", "fault"]),
    20: ("tag",   ["temp_c", "pressure_pa", "humidity_pct", "gas_ohm",
                   "acc_x", "acc_y", "acc_z", "gyr_x", "gyr_y", "gyr_z"]),
}

# Wirepas EP-247 diagnostic CBOR map: known integer keys → field label.
DIAG_FIELDS = {
    1: "boot_count", 4: "role", 5: "voltage_mv",
    7: "access_cycle_ms", 8: "max_cycle_ms", 24: "uptime_s",
}

# EP 14 UMB uplink: CBOR array [dev_addr, ch, val, ch, val, ...] per device.
# UMB channel number → InfluxDB field name (OTT/Lufft WS series). Values sent as
# CBOR null (channel in error / not available) are skipped.
UMB_CHANNELS = {
    100:   "air_temp_c",           160: "air_temp_avg_c",
    110:   "dewpoint_c",           114: "wetbulb_c",
    200:   "humidity_pct",         260: "humidity_avg_pct",
    300:   "pressure_abs_hpa",     305: "pressure_rel_hpa",  365: "pressure_rel_avg_hpa",
    400:   "wind_speed_ms",        440: "wind_gust_ms",      460: "wind_speed_avg_ms",
    500:   "wind_dir_deg",         520: "wind_dir_min_deg",  540: "wind_dir_max_deg",
    510:   "compass_deg",
    900:   "global_rad_wm2",       960: "global_rad_avg_wm2",
    620:   "precip_abs_mm",        625: "precip_diff_mm",     820: "precip_intensity_mmh",
    700:   "precip_type",
    112:   "heater_wind_temp_c",   113: "heater_temp_c",
    10000: "supply_v",
}

# UMB device address (class<<12 | id) → friendly model tag.
UMB_MODELS = {
    0x7001: "WS501", 0x7002: "WS100",
}


def _lp_escape_tag(s: str) -> str:
    """Escape a line-protocol tag key/value (commas, spaces, equals)."""
    return str(s).replace("\\", "\\\\").replace(" ", "\\ ") \
                 .replace(",", "\\,").replace("=", "\\=")


class InfluxWriter:
    """Buffered InfluxDB line-protocol writer over the /api/v2/write HTTP API."""

    def __init__(self):
        base = _env("INFLUX_URL", "http://influxdb:8086").rstrip("/")
        org = _env("INFLUX_ORG", "wirepas")
        bucket = _env("INFLUX_BUCKET", required=True)
        precision = _env("INFLUX_PRECISION", "ns")
        token = _env("INFLUX_TOKEN", required=True)
        scheme = _env("INFLUX_AUTH_SCHEME", "Token")   # v2: Token, v3 Core: Bearer

        self._url = f"{base}/api/v2/write?org={org}&bucket={bucket}&precision={precision}"
        self._headers = {
            "Authorization": f"{scheme} {token}",
            "Content-Type": "text/plain; charset=utf-8",
        }
        self._batch_size = int(_env("INFLUX_BATCH_SIZE", "200"))
        self._flush_interval = float(_env("INFLUX_FLUSH_INTERVAL_S", "5"))
        self._timeout = float(_env("INFLUX_HTTP_TIMEOUT_S", "10"))
        self._buf: list[str] = []
        self._lock = threading.Lock()
        self._session = requests.Session()
        self._running = True
        threading.Thread(target=self._flush_loop, daemon=True).start()
        log.info("InfluxDB writer → %s (batch=%d, flush=%.0fs)",
                 self._url, self._batch_size, self._flush_interval)

    def add(self, line: str):
        with self._lock:
            self._buf.append(line)
            n = len(self._buf)
        if n >= self._batch_size:
            self.flush()

    def flush(self):
        with self._lock:
            if not self._buf:
                return
            lines, self._buf = self._buf, []
        payload = "\n".join(lines).encode("utf-8")
        for attempt in (1, 2, 3):
            try:
                r = self._session.post(self._url, data=payload,
                                       headers=self._headers, timeout=self._timeout)
                if r.status_code in (200, 204):
                    log.debug("wrote %d points", len(lines))
                    return
                log.warning("influx write HTTP %s: %s", r.status_code, r.text[:200])
                if 400 <= r.status_code < 500:
                    return   # bad request → don't retry (would loop forever)
            except requests.RequestException as e:
                log.warning("influx write error (attempt %d): %s", attempt, e)
            time.sleep(1.0 * attempt)
        log.error("dropping %d points after retries", len(lines))

    def _flush_loop(self):
        while self._running:
            time.sleep(self._flush_interval)
            try:
                self.flush()
            except Exception as e:
                log.exception("flush loop error: %s", e)

    def close(self):
        self._running = False
        self.flush()


def _num(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def decode_uplink(src_ep: int, dst_ep: int, payload: bytes) -> Optional[tuple]:
    """Return (measurement, {field: float}, {tag: str}) for a known uplink, or None."""
    if not payload:
        return None

    # Wirepas diagnostic packet (EP 247 → 255): CBOR map with integer keys.
    if src_ep == 247 and dst_ep == 255:
        try:
            m = cbor2.loads(bytes(payload))
        except Exception:
            return None
        if not isinstance(m, dict):
            return None
        fields = {}
        for k, v in m.items():
            val = _num(v)
            if val is None:
                continue
            fields[DIAG_FIELDS.get(k, f"k{k}")] = val
        return ("diag", fields, {}) if fields else None

    # Application endpoints: CBOR array.
    try:
        arr = cbor2.loads(bytes(payload))
    except Exception:
        return None
    if not isinstance(arr, (list, tuple)) or not arr:
        return None

    # EP 14 is shared by two apps, told apart by the CBOR array shape:
    #  - UMB weather (rs485_bridge): [dev_addr, ch, val, ...]  -> odd length,
    #    dev_addr an int class<<12|id (>= 0x1000); null channel = skipped
    #  - MAX17261 fuel gauge (canopee): [voltage_mv, soc, temp_c, current_ma]
    if src_ep == 14:
        n = len(arr)
        if n >= 3 and n % 2 == 1 and isinstance(arr[0], int) and arr[0] >= 0x1000:
            dev = int(arr[0])
            fields = {}
            for i in range(1, n, 2):
                v = _num(arr[i + 1])
                if v is None:                 # channel in error / not available
                    continue
                try:
                    ch = int(arr[i])
                except (TypeError, ValueError):
                    continue
                fields[UMB_CHANNELS.get(ch, f"ch{ch}")] = v
            if not fields:
                return None
            tags = {"umb_dev": f"0x{dev:04x}"}
            if dev in UMB_MODELS:
                tags["umb_model"] = UMB_MODELS[dev]
            return ("umb", fields, tags)
        if n == 4:                            # MAX17261 fuel gauge (legacy)
            v = [_num(x) for x in arr]
            if any(x is None for x in v):
                return None
            return ("gauge", {"voltage_mv": v[0], "soc_pct": v[1],
                              "temp_c": v[2], "current_ma": v[3]}, {})
        if n == 14:                           # MAX17261 fuel gauge + health
            v = [_num(x) for x in arr]
            if any(x is None for x in v):
                return None
            return ("gauge", {
                "voltage_mv": v[0], "soc_pct": v[1], "temp_c": v[2],
                "current_ma": v[3], "avg_current_ma": v[4],
                "rep_cap_mah": v[5], "full_cap_mah": v[6],
                "design_cap_mah": v[7], "age_pct": v[8], "cycles": v[9],
                "tte_s": v[10], "ttf_s": v[11], "timer_h": v[12],
                "status": v[13]}, {})
        return None

    # Generic application endpoints: CBOR array of numbers.
    vals = [_num(x) for x in arr]
    if any(v is None for v in vals):
        return None
    measurement, names = ENDPOINT_MAP.get(src_ep, (f"ep{src_ep}", []))
    fields = {}
    for i, v in enumerate(vals):
        name = names[i] if i < len(names) else f"v{i}"
        fields[name] = v
    return (measurement, fields, {})


class Collector:
    def __init__(self, writer: InfluxWriter):
        self._writer = writer
        self._count = 0

    def on_data(self, data):
        try:
            src = int(getattr(data, "source_address", 0)) & 0xFFFFFFFF
            src_ep = int(getattr(data, "source_endpoint", 0))
            dst_ep = int(getattr(data, "destination_endpoint", 0))
            net = getattr(data, "network_address", None)
            gw = getattr(data, "gw_id", None)
            sink = getattr(data, "sink_id", None)
            rx_ms = getattr(data, "rx_time_ms_epoch", None)
            payload = getattr(data, "data_payload", b"") or b""
        except Exception:
            return

        decoded = decode_uplink(src_ep, dst_ep, bytes(payload))
        if decoded is None:
            return
        measurement, fields, extra_tags = decoded
        if not fields:
            return

        tags = [
            f"node=0x{src:08x}",
            f"ep={src_ep}",
        ]
        if net is not None:
            tags.append(f"net={_lp_escape_tag(net)}")
        if gw:
            tags.append(f"gw={_lp_escape_tag(gw)}")
        if sink:
            tags.append(f"sink={_lp_escape_tag(sink)}")
        for k, v in extra_tags.items():
            tags.append(f"{k}={_lp_escape_tag(str(v))}")

        field_str = ",".join(f"{k}={v}" for k, v in fields.items())
        ts_ns = int(rx_ms) * 1_000_000 if rx_ms else time.time_ns()
        line = f"{measurement},{','.join(tags)} {field_str} {ts_ns}"
        self._writer.add(line)

        self._count += 1
        if self._count % 100 == 0:
            log.info("%d uplinks written", self._count)

    def on_gateway_status(self, client, userdata, message):
        """gw-event/status/<gw_id> - official StatusEvent protobuf (ONLINE/OFFLINE)."""
        try:
            status = wmm.StatusEvent.from_payload(message.payload)
        except Exception as e:
            log.debug("bad StatusEvent on %s: %s", message.topic, e)
            return

        online = 1 if status.state == wmm.GatewayState.ONLINE else 0
        tags = [f"gw={_lp_escape_tag(status.gw_id)}"]
        if status.gateway_model:
            tags.append(f"model={_lp_escape_tag(status.gateway_model)}")
        if status.gateway_version:
            tags.append(f"version={_lp_escape_tag(status.gateway_version)}")

        field_str = f"online={online}i"
        ts_ns = time.time_ns()
        line = f"gw_status,{','.join(tags)} {field_str} {ts_ns}"
        self._writer.add(line)
        log.info("Gateway %s is %s", status.gw_id, status.state.name)

    def _on_json_telemetry(self, message, measurement, numeric_keys):
        """Shared handler for our custom (non-Wirepas-API) gw-event/<x>/<gw_id>
        JSON topics: extract the numeric fields listed in numeric_keys and
        write them as one InfluxDB point tagged by gw."""
        try:
            gw_id = message.topic.rsplit("/", 1)[-1]
            data = json.loads(message.payload.decode("utf-8"))
        except Exception as e:
            log.debug("bad %s payload on %s: %s", measurement, message.topic, e)
            return None

        fields = {}
        for key in numeric_keys:
            val = _num(data.get(key))
            if val is not None:
                fields[key] = val
        if not fields:
            return None

        tags = [f"gw={_lp_escape_tag(gw_id)}"]
        field_str = ",".join(f"{k}={v}" for k, v in fields.items())
        ts_ns = time.time_ns()
        line = f"{measurement},{','.join(tags)} {field_str} {ts_ns}"
        self._writer.add(line)
        return gw_id, fields

    def on_battery(self, client, userdata, message):
        """gw-event/battery/<gw_id> - custom JSON topic (Thingy:91 fuel gauge)."""
        result = self._on_json_telemetry(message, "battery",
                                         ["voltage_mv", "soc_percent"])
        if result:
            gw_id, fields = result
            log.info("Battery %s: %s mV %s%%", gw_id, fields.get("voltage_mv"),
                     fields.get("soc_percent"))

    def on_lte_info(self, client, userdata, message):
        """gw-event/lte/<gw_id> - custom JSON topic (RSRP/RSRQ/SNR/band/cell)."""
        result = self._on_json_telemetry(
            message, "lte_info",
            ["rsrp_dbm", "rsrq_db", "snr_db", "band", "phy_cid", "earfcn",
             "tx_power_dbm"])
        if result:
            gw_id, fields = result
            log.info("LTE %s: RSRP=%s RSRQ=%s SNR=%s band=%s cid=%s", gw_id,
                     fields.get("rsrp_dbm"), fields.get("rsrq_db"),
                     fields.get("snr_db"), fields.get("band"),
                     fields.get("phy_cid"))

    def on_location(self, client, userdata, message):
        """gw-event/location/<gw_id> - custom JSON topic (GNSS fix attempt,
        retained, published on every attempt - not just successful fixes -
        to track the ongoing GPS-reliability investigation: fix_valid,
        satellites_tracked/used and elapsed_ms explain *why* a fix did or
        didn't happen, without needing a manual RTT capture each time)."""
        result = self._on_json_telemetry(
            message, "gw_location",
            ["fix_valid", "lat", "lon", "accuracy_m", "satellites_tracked",
             "satellites_used", "elapsed_ms"])
        if result:
            gw_id, fields = result
            if fields.get("fix_valid"):
                log.info("Location %s: FIX lat=%s lon=%s accuracy=%sm "
                         "(tracked=%s used=%s)", gw_id, fields.get("lat"),
                         fields.get("lon"), fields.get("accuracy_m"),
                         fields.get("satellites_tracked"),
                         fields.get("satellites_used"))
            else:
                log.info("Location %s: no fix (tracked=%s used=%s "
                         "elapsed=%sms)", gw_id,
                         fields.get("satellites_tracked"),
                         fields.get("satellites_used"),
                         fields.get("elapsed_ms"))


def main():
    host = _env("WIREPAS_MQTT_HOST", required=True)
    port = int(_env("WIREPAS_MQTT_PORT", "8883"))
    user = _env("WIREPAS_MQTT_USER", "")
    password = _env("WIREPAS_MQTT_PASSWORD", "")
    insecure = _env("WIREPAS_MQTT_INSECURE", "0") in ("1", "true", "True")

    writer = InfluxWriter()
    collector = Collector(writer)

    log.info("Connecting to broker %s:%d …", host, port)
    wni = WirepasNetworkInterface(host, port, user, password, insecure=insecure)
    wni.register_data_cb(collector.on_data)

    # Extra subscriptions on the same MQTT connection: the official gateway
    # status event, and our custom (non-Wirepas-API) battery/LTE telemetry
    # topics. WirepasNetworkInterface.__init__() only *starts* an async
    # connection (loop_start() + a background connect task); the paho client
    # isn't actually connected yet at this point, so subscribe() calls issued
    # here race the real CONNACK and are silently dropped. The library's own
    # topics work because it (re-)subscribes from inside its on_connect
    # callback, which only fires once the connection is really up. Chain onto
    # that same callback so ours run at the right time too, including on any
    # future reconnect.
    #
    # message_callback_add() keyed on "gw-event/status/+" must also be
    # (re-)registered *inside* this same callback, after calling the
    # library's own on_connect: paho's filtered-callback matcher stores one
    # callback per exact filter string (last write wins, it doesn't stack),
    # and the library registers its own handler for that identical filter
    # ("gw-event/status/+", via TopicGenerator.make_status_topic()) on every
    # connect. Registering ours once at startup, before the first connect,
    # meant the library's own registration silently clobbered ours on
    # connect - status data parsed fine standalone but on_gateway_status
    # never actually fired. gw-event/battery|lte/+ don't collide with
    # anything the library subscribes to, so those were never affected.
    mqtt_client = wni._mqtt_client
    _wni_on_connect = mqtt_client.on_connect

    def _on_connect(client, userdata, flags, rc):
        _wni_on_connect(client, userdata, flags, rc)
        if rc == 0:
            client.subscribe("gw-event/status/+", qos=1)
            client.subscribe("gw-event/battery/+", qos=1)
            client.subscribe("gw-event/lte/+", qos=1)
            client.subscribe("gw-event/location/+", qos=1)
            client.message_callback_add("gw-event/status/+", collector.on_gateway_status)
            client.message_callback_add("gw-event/battery/+", collector.on_battery)
            client.message_callback_add("gw-event/lte/+", collector.on_lte_info)
            client.message_callback_add("gw-event/location/+", collector.on_location)
            log.info("Subscribed to gateway status, battery, LTE info and location.")

    mqtt_client.on_connect = _on_connect

    log.info("Collecting sensor uplinks; gateway status/battery/LTE/location subscribed on connect.")

    try:
        while True:
            time.sleep(60)
    except KeyboardInterrupt:
        log.info("Shutting down …")
    finally:
        writer.close()


if __name__ == "__main__":
    main()
