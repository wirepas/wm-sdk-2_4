# Wirepas MQTT → InfluxDB collector

Standalone service that subscribes to a Wirepas Gateway-to-Backend MQTT broker,
decodes the sensorv26 / nrf54L15_tag CBOR uplinks, and writes them to InfluxDB.
Runs in Docker on the Mac mini.

## Endpoints decoded

| EP    | Measurement | Fields |
|-------|-------------|--------|
| 9     | `aem`       | vsto, vsrc, temp_c, power_uw, status, charging |
| 11    | `ctn`       | temp_c, r_t, diag |
| 12    | `sp110`     | irradiance_wm2, mv, diag |
| 20    | `tag`       | temp_c, pressure_pa, humidity_pct, gas_ohm, acc_x/y/z, gyr_x/y/z |
| 247→255 | `diag`    | role, voltage_mv, uptime_s, boot_count, … |
| other | `ep<N>`     | v0, v1, … (any CBOR array of numbers) |

Tags on every point: `node` (0x……), `ep`, `net`, `gw`, `sink`.
Timestamp = the uplink's `rx_time_ms_epoch` (falls back to ingest time).

## Quick start

```bash
cd tools/influx_collector
cp .env.example .env
# edit .env: broker password, and a long random INFLUX_TOKEN + admin password
docker compose up -d
docker compose logs -f collector      # watch uplinks being written
```

This starts **InfluxDB 2.x** (auto-configured from the `.env`) and the collector.
Open the InfluxDB UI at http://<mac-mini>:8086 (login = INFLUX_ADMIN_USER /
INFLUX_ADMIN_PASSWORD) and query the `sensors` bucket.

## InfluxDB 3 Core instead of 2.x

The collector writes line protocol via `/api/v2/write`, accepted by both
versions. To switch, follow the commented block at the bottom of
`docker-compose.yml`: use the `influxdb:3-core` service, set
`INFLUX_URL=http://influxdb:8181`, `INFLUX_AUTH_SCHEME=Bearer`, treat
`INFLUX_BUCKET` as the v3 database, and create an admin token with
`docker compose exec influxdb influxdb3 create token --admin`.

## Run without the bundled InfluxDB

If InfluxDB already runs elsewhere, delete the `influxdb` service from the
compose file and point `INFLUX_URL` / `INFLUX_TOKEN` / `INFLUX_ORG` /
`INFLUX_BUCKET` at it.

## Configuration (environment variables)

| Var | Default | Notes |
|-----|---------|-------|
| `WIREPAS_MQTT_HOST` | — (required) | broker host |
| `WIREPAS_MQTT_PORT` | 8883 | mqtts |
| `WIREPAS_MQTT_USER` / `_PASSWORD` | — | broker credentials |
| `WIREPAS_MQTT_INSECURE` | 0 | 1 = TLS without cert verification |
| `INFLUX_URL` | http://influxdb:8086 | v3: `…:8181` |
| `INFLUX_TOKEN` | — (required) | API token |
| `INFLUX_ORG` | wirepas | (v2) |
| `INFLUX_BUCKET` | — (required) | bucket / v3 database |
| `INFLUX_AUTH_SCHEME` | Token | v3 Core → `Bearer` |
| `INFLUX_BATCH_SIZE` | 200 | flush after N points |
| `INFLUX_FLUSH_INTERVAL_S` | 5 | flush at least every N seconds |
| `LOG_LEVEL` | INFO | DEBUG to log every write |
