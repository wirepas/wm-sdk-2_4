/* nRF54L15 tag application — nrf54l15_tag board
 *
 * Features
 *   - LE mode (not LL): optimised for CR2032 3 V lithium cell
 *   - Periodic sensor read + CBOR uplink on EP_TAG_SENSOR (EP 20)
 *     CBOR array: [T_degC, P_Pa, H_pct, acc_x, acc_y, acc_z, gyr_x, gyr_y, gyr_z]
 *   - BLE beacon (Wirepas network info + tag ID) every 1 s
 *   - NFC Type-2 tag: node address readable + commissioning writable
 *   - Antenna diversity: P1.09 / P1.10 toggled each beacon slot
 *   - LED1 RGB state machine (boot, wirepas TX, wirepas idle)
 *   - Sensors sleep between measurement periods (standby via driver API)
 *   - No debug UART (P0.00/P0.01 are buttons on this board)
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "api.h"
#include "mcu.h"
#include "node_configuration.h"
#include "shared_data.h"
#include "app_scheduler.h"
#include "gpio.h"
#include "board.h"

#include "app_setup.h"

#include "wms_beacon_tx.h"

#include "nfc_hw.h"
#include "t2_emulation.h"
#include "ndef.h"
#include "aem10900.h"

#include "cbor.h"

#include "bme688.h"
#include "adxl367.h"
#include "bmi270.h"

/* ── Wirepas endpoint ───────────────────────────────────────────────────────── */
#define EP_TAG_SENSOR   20u     /* CBOR [T, P, H, ax, ay, az, gx, gy, gz]      */

/* ── Timing ─────────────────────────────────────────────────────────────────── */
/* CR2032 budget: keep sensor on-time minimal.
 * 30 s period: BME688 forced ~2 ms, ADXL367+BMI270 burst <1 ms. */
#define SENSOR_PERIOD_MS        30000u
#define SENSOR_EXEC_US          15000u  /* I2C + SPI read budget               */

#define BEACON_PERIOD_MS         1000u
#define BEACON_EXEC_US           2000u  /* must not starve the LL/LE radio      */

#define LED_BLINK_MS              100u  /* TX/RX blink duration                 */
#define LED_BOOT_MS              1000u  /* boot blink duration                  */

/* ── BLE beacon ─────────────────────────────────────────────────────────────── */
#define BEACON_SERVICE_UUID     0x1234u
#define BEACON_COMPANY_ID_LO    0xFFu
#define BEACON_COMPANY_ID_HI    0xFFu
#define BEACON_VERSION_TAG      0x03u   /* 0x03 = tag payload + AEM energy bytes */
#define BEACON_INTERVAL_MS      1000u
#define BEACON_POWER_DBM        0       /* 0 dBm — reasonable for CR2032        */

/* ── NFC ────────────────────────────────────────────────────────────────────── */
#define NFC_MEMORY_SIZE         512u
#define NFC_COMMISSION_MAGIC    0x4E464332u  /* "NFC2" */
#define NFC_STORE_WRITE_TRIES   3u
#define TXT_HDR_SIZE            3u      /* UTF-8 flag + 'e' + 'n'              */

typedef struct
{
    uint32_t magic;
    uint32_t net_addr;
    uint8_t  net_channel;
    uint8_t  _pad[2];
    uint32_t crc;
} nfc_commission_store_t;

typedef char _nfc_store_size_check[(sizeof(nfc_commission_store_t) <= 32) ? 1 : -1];

static uint8_t          m_nfc_memory[NFC_MEMORY_SIZE];
static volatile uint8_t m_nfc_events;
static volatile bool    m_nfc_write_pending;

#define NFC_EVT_FIELD_ON     (1u << 0)
#define NFC_EVT_FIELD_OFF    (1u << 1)
#define NFC_EVT_SELECTED     (1u << 2)
#define NFC_EVT_COMMISSIONED (1u << 3)

/* ── Antenna diversity state ────────────────────────────────────────────────── */
/* Simple round-robin: alternate ANT1/ANT2 each beacon period.
 * The two antennas are 90° apart — toggling each slot gives spatial diversity
 * without needing RSSI feedback on a constrained CR2032 budget. */
static bool m_ant_select = false;   /* false = ANT1 active, true = ANT2 active  */

/* ── Sensor data ────────────────────────────────────────────────────────────── */
static struct
{
    float   temp_c;
    float   pressure_pa;
    float   humidity_pct;
    float   gas_resistance;   /* Ω — VOC proxy: higher = cleaner air */
    bool    gas_valid;
    int16_t acc_x, acc_y, acc_z;
    int16_t gyr_x, gyr_y, gyr_z;
    bool    valid;
} m_meas;

/* Two-phase sensor task state: false = trigger phase, true = read phase. */
static bool m_sensor_measuring = false;

/* ── LED helpers ─────────────────────────────────────────────────────────────── */

/* RGB LED is common-anode (active-low): driving a pin LOW lights it. */
#if BOARD_LED_ACTIVE_LOW
#define LED1_LVL_ON   GPIO_LEVEL_LOW
#define LED1_LVL_OFF  GPIO_LEVEL_HIGH
#else
#define LED1_LVL_ON   GPIO_LEVEL_HIGH
#define LED1_LVL_OFF  GPIO_LEVEL_LOW
#endif

static void led1_off(void)
{
    Gpio_outputWrite(LED1_R, LED1_LVL_OFF);
    Gpio_outputWrite(LED1_G, LED1_LVL_OFF);
    Gpio_outputWrite(LED1_B, LED1_LVL_OFF);
}

static void led1_set(bool r, bool g, bool b)
{
    Gpio_outputWrite(LED1_R, r ? LED1_LVL_ON : LED1_LVL_OFF);
    Gpio_outputWrite(LED1_G, g ? LED1_LVL_ON : LED1_LVL_OFF);
    Gpio_outputWrite(LED1_B, b ? LED1_LVL_ON : LED1_LVL_OFF);
}

/* Scheduler task: turn LED1 off after a blink. */
static uint32_t led1_off_task(void)
{
    led1_off();
    return APP_SCHEDULER_STOP_TASK;
}

/* One-shot scheduled blink — colour off after LED_BLINK_MS. */
static void led1_blink(bool r, bool g, bool b)
{
    led1_set(r, g, b);
    App_Scheduler_addTask_execTime(led1_off_task, LED_BLINK_MS, 200u);
}

/* Boot blink off task. */
static uint32_t led1_boot_off_task(void)
{
    led1_off();
    return APP_SCHEDULER_STOP_TASK;
}

/* ── Antenna diversity ───────────────────────────────────────────────────────── */

static void antenna_init(void)
{
    static const gpio_out_cfg_t ant_cfg = {
        .out_mode_cfg  = GPIO_OUT_MODE_PUSH_PULL,
        .level_default = GPIO_LEVEL_LOW,
    };
    Gpio_outputSetCfg(BOARD_GPIO_ID_ANT1, &ant_cfg);
    Gpio_outputSetCfg(BOARD_GPIO_ID_ANT2, &ant_cfg);

    /* Start with ANT1 active. */
    Gpio_outputWrite(BOARD_GPIO_ID_ANT1, GPIO_LEVEL_HIGH);
    Gpio_outputWrite(BOARD_GPIO_ID_ANT2, GPIO_LEVEL_LOW);
    m_ant_select = false;
}

static void antenna_toggle(void)
{
    m_ant_select = !m_ant_select;
    Gpio_outputWrite(BOARD_GPIO_ID_ANT1,
                     m_ant_select ? GPIO_LEVEL_LOW  : GPIO_LEVEL_HIGH);
    Gpio_outputWrite(BOARD_GPIO_ID_ANT2,
                     m_ant_select ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
}

/* ── BLE beacon ──────────────────────────────────────────────────────────────── */

static bool m_beacon_started = false;

/* AEM10900 energy: storage (battery) and source (harvester) voltages, encoded
 * as 1 byte each at 20 mV/LSB (0-5.1 V). Refreshed by sensor_task, copied into
 * the beacon. 0 = not yet read / AEM absent. */
static uint8_t m_aem_sto_b = 0u;
static uint8_t m_aem_src_b = 0u;

static uint8_t aem_volt_to_byte(float v)
{
    int32_t b = (int32_t)(v * 50.0f + 0.5f);   /* 20 mV/LSB */
    if (b < 0)   b = 0;
    if (b > 255) b = 255;
    return (uint8_t)b;
}

static uint32_t beacon_tx_task(void)
{
    /* Toggle antenna for spatial diversity each slot. */
    antenna_toggle();

    app_addr_t addr = 0;
    lib_settings->getNodeAddress(&addr);

    app_lib_state_route_info_t route;
    memset(&route, 0, sizeof(route));
    lib_state->getRouteInfo(&route);

    uint8_t pdu[38];
    uint8_t i = 0;

    /* ADV_NONCONN_IND header + address (6 bytes). */
    pdu[i++] = 0x42u;
    pdu[i++] = (uint8_t)(addr >>  0);
    pdu[i++] = (uint8_t)(addr >>  8);
    pdu[i++] = (uint8_t)(addr >> 16);
    pdu[i++] = (uint8_t)(addr >> 24);
    pdu[i++] = 0x00u;
    pdu[i++] = 0x00u;

    /* AD: Flags. */
    pdu[i++] = 0x02u; pdu[i++] = 0x01u; pdu[i++] = 0x04u;

    /* AD: 16-bit UUID list. */
    pdu[i++] = 0x03u; pdu[i++] = 0x03u;
    pdu[i++] = (uint8_t)(BEACON_SERVICE_UUID & 0xFFu);
    pdu[i++] = (uint8_t)(BEACON_SERVICE_UUID >> 8);

    /* AD: TX power. */
    pdu[i++] = 0x02u; pdu[i++] = 0x0Au;
    pdu[i++] = (uint8_t)(int8_t)BEACON_POWER_DBM;

    /* AD: Manufacturer Specific — node addr + sink addr + route cost + AEM.
     * Length = type(1)+company(2)+version(1)+addr(4)+sink(4)+cost(1)+aem(2) = 15. */
    pdu[i++] = 0x0Fu; pdu[i++] = 0xFFu;
    pdu[i++] = BEACON_COMPANY_ID_LO;
    pdu[i++] = BEACON_COMPANY_ID_HI;
    pdu[i++] = BEACON_VERSION_TAG;
    pdu[i++] = (uint8_t)(addr >>  0);
    pdu[i++] = (uint8_t)(addr >>  8);
    pdu[i++] = (uint8_t)(addr >> 16);
    pdu[i++] = (uint8_t)(addr >> 24);
    pdu[i++] = (uint8_t)((uint32_t)route.sink >>  0);
    pdu[i++] = (uint8_t)((uint32_t)route.sink >>  8);
    pdu[i++] = (uint8_t)((uint32_t)route.sink >> 16);
    pdu[i++] = (uint8_t)((uint32_t)route.sink >> 24);
    pdu[i++] = route.cost;
    /* AEM10900 energy (20 mV/LSB): storage (battery), source (harvester). */
    pdu[i++] = m_aem_sto_b;
    pdu[i++] = m_aem_src_b;
    /* i = 38 — exactly the 31-byte AD limit (addr+header = 7). */

    if (!m_beacon_started)
    {
        int8_t pwr = (int8_t)BEACON_POWER_DBM;
        lib_beacon_tx->clearBeacons();
        lib_beacon_tx->setBeaconInterval(BEACON_INTERVAL_MS);
        lib_beacon_tx->setBeaconPower(0, &pwr);
        lib_beacon_tx->setBeaconChannels(0, APP_LIB_BEACON_TX_CHANNELS_ALL);
        lib_beacon_tx->setBeaconContents(0, pdu, i);
        lib_beacon_tx->enableBeacons(true);
        m_beacon_started = true;
    }
    else
    {
        lib_beacon_tx->setBeaconContents(0, pdu, i);
    }

    return BEACON_PERIOD_MS;
}

/* ── Sensor tasks ────────────────────────────────────────────────────────────── */

/* ── Sensor task (two-phase) ─────────────────────────────────────────────────── */
/*
 * Phase 0 — TRIGGER:
 *   Wake ADXL367, trigger BME688 forced-mode measurement.
 *   Return BME688_meas_duration_us()/1000 ms so the scheduler
 *   re-enters this function only after the heater has finished.
 *
 * Phase 1 — READ:
 *   Read all sensors, pack CBOR frame, send on EP_TAG_SENSOR.
 *   Put sensors to sleep.
 *   Return the remaining inter-period time.
 *
 * Total period ≈ SENSOR_PERIOD_MS (30 s).
 * Measurement window ≈ 165 ms (2 ms T/P/H + 150 ms heater + 5 % margin).
 */
static uint32_t sensor_task(void)
{
    if (!m_sensor_measuring)
    {
        /* ── Phase 0: trigger ── */
        ADXL367_measure();
        BME688_trigger();
        m_sensor_measuring = true;
        return BME688_meas_duration_us() / 1000u + 1u;
    }

    /* ── Phase 1: read + transmit ── */
    m_sensor_measuring = false;

    /* BME688: T / P / H / gas resistance. */
    bme688_result_t env = {0};
    (void) BME688_read(&env);
    m_meas.temp_c        = env.temperature;
    m_meas.pressure_pa   = env.pressure;
    m_meas.humidity_pct  = env.humidity;
    m_meas.gas_resistance = env.gas_resistance;
    m_meas.gas_valid     = env.gas_valid && env.heat_stable;

    /* ADXL367: raw 14-bit signed X/Y/Z (1 LSB = 1 mg at ±2 g). */
    adxl367_sample_t adxl = {0};
    (void) ADXL367_read_xyz(&adxl);
    m_meas.acc_x = adxl.x;
    m_meas.acc_y = adxl.y;
    m_meas.acc_z = adxl.z;

    /* BMI270: raw 16-bit gyro (BMI270_load_config TODO). */
    bmi270_sample_t bmi = {0};
    (void) BMI270_read(&bmi);
    m_meas.gyr_x = bmi.gyr_x;
    m_meas.gyr_y = bmi.gyr_y;
    m_meas.gyr_z = bmi.gyr_z;
    m_meas.valid = true;

    /* AEM10900 energy: cache encoded storage/source voltages for the beacon. */
    float aem_v;
    if (AEM10900_read_storage_voltage(&aem_v) == AEM10900_RES_OK)
        m_aem_sto_b = aem_volt_to_byte(aem_v);
    if (AEM10900_read_source_voltage(&aem_v) == AEM10900_RES_OK)
        m_aem_src_b = aem_volt_to_byte(aem_v);

    /* ── CBOR uplink EP_TAG_SENSOR ──
     * Array[10]: [T(°C), P(Pa), H(%RH), gas_R(Ω),
     *             ax, ay, az,            ← raw 14-bit counts, ±2 g
     *             gx, gy, gz]            ← raw 16-bit counts
     * Sizes: 4×float32(5B) + 6×small_int(≤3B) ≈ 38 B → 64 B buf sufficient.
     *
     * gas_R: VOC proxy.  > 50 kΩ = good, 10–50 kΩ = moderate, < 10 kΩ = poor.
     * gas_valid=false if heater did not stabilise (discard gas_R on first boot
     * or when supply is marginal on CR2032). */
    uint8_t buf[64];
    CborEncoder enc, arr;
    cbor_encoder_init(&enc, buf, sizeof(buf), 0);
    cbor_encoder_create_array(&enc, &arr, 10);
    cbor_encode_float(&arr, m_meas.temp_c);
    cbor_encode_float(&arr, m_meas.pressure_pa);
    cbor_encode_float(&arr, m_meas.humidity_pct);
    cbor_encode_float(&arr, m_meas.gas_resistance);
    cbor_encode_int(&arr, m_meas.acc_x);
    cbor_encode_int(&arr, m_meas.acc_y);
    cbor_encode_int(&arr, m_meas.acc_z);
    cbor_encode_int(&arr, m_meas.gyr_x);
    cbor_encode_int(&arr, m_meas.gyr_y);
    cbor_encode_int(&arr, m_meas.gyr_z);
    cbor_encoder_close_container(&enc, &arr);

    size_t len = cbor_encoder_get_buffer_size(&enc, buf);

    app_lib_data_to_send_t pkt = {
        .bytes         = buf,
        .num_bytes     = len,
        .dest_address  = APP_ADDR_ANYSINK,
        .src_endpoint  = EP_TAG_SENSOR,
        .dest_endpoint = EP_TAG_SENSOR,
        .qos           = APP_LIB_DATA_QOS_NORMAL,
        .flags         = APP_LIB_DATA_SEND_FLAG_NONE,
        .tracking_id   = APP_LIB_DATA_NO_TRACKING_ID,
    };
    Shared_Data_sendData(&pkt, NULL);

    /* Blue blink = Wirepas TX. */
    led1_blink(false, false, true);

    /* Sleep ADXL367; BME688 auto-returns to sleep after forced mode. */
    ADXL367_standby();

    /* Remaining time until next period. */
    uint32_t meas_ms = BME688_meas_duration_us() / 1000u + 1u;
    return (SENSOR_PERIOD_MS > meas_ms) ? (SENSOR_PERIOD_MS - meas_ms) : SENSOR_PERIOD_MS;
}

/* ── NFC ─────────────────────────────────────────────────────────────────────── */

static uint32_t nfc_crc32(const void * data, size_t len)
{
    const uint8_t * p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= (uint32_t)p[i];
        for (uint8_t b = 0; b < 8u; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}

static uint32_t nfc_store_crc(const nfc_commission_store_t * s)
{
    return nfc_crc32(s, sizeof(*s) - sizeof(s->crc));
}

static bool nfc_store_valid(const nfc_commission_store_t * s)
{
    return (s->magic == NFC_COMMISSION_MAGIC) && (s->crc == nfc_store_crc(s));
}

static bool nfc_store_write(const nfc_commission_store_t * s)
{
    if (lib_storage == NULL) return false;
    for (uint8_t t = 0; t < NFC_STORE_WRITE_TRIES; t++)
    {
        if (lib_storage->writePersistent(s, sizeof(*s)) != APP_RES_OK) continue;
        nfc_commission_store_t rb;
        if (lib_storage->readPersistent(&rb, sizeof(rb)) == APP_RES_OK
            && memcmp(&rb, s, sizeof(*s)) == 0)
            return true;
    }
    return false;
}

static void nfc_event_cb(t2_emu_event_e event)
{
    switch (event)
    {
        case T2_EMU_EVENT_FIELD_ON:
            m_nfc_events |= NFC_EVT_FIELD_ON;
            break;
        case T2_EMU_EVENT_FIELD_OFF:
            m_nfc_events |= NFC_EVT_FIELD_OFF;
            if (m_nfc_write_pending)
            {
                m_nfc_write_pending = false;
                m_nfc_events |= NFC_EVT_COMMISSIONED;
            }
            break;
        case T2_EMU_EVENT_SELECTED:
            m_nfc_events |= NFC_EVT_SELECTED;
            break;
        case T2_EMU_DATA_WRITTEN:
            m_nfc_write_pending = true;
            break;
        default:
            break;
    }
}

static bool nfc_ndef_get_text(char * out, size_t out_size)
{
    const uint8_t * p = &m_nfc_memory[T2_HEADER_SIZE];
    if (p[0] != 0x03u) return false;          /* TLV type must be NDEF */
    uint8_t ndef_len = p[1];
    if (ndef_len < 4u) return false;
    const uint8_t * ndef = &p[2];
    /* Minimal NDEF: flags(1) + type_len(1) + payload_len(1) + type(1) + payload */
    if ((ndef[0] & 0x10u) == 0u) return false; /* SR flag — short record */
    uint8_t type_len    = ndef[1];
    uint8_t payload_len = ndef[2];
    const uint8_t * payload = &ndef[3 + type_len];
    if (payload_len < TXT_HDR_SIZE + 1u) return false;
    uint8_t lang_len = payload[0] & 0x3Fu;
    size_t  txt_len  = payload_len - 1u - lang_len;
    if (txt_len == 0u || txt_len >= out_size) return false;
    memcpy(out, &payload[1u + lang_len], txt_len);
    out[txt_len] = '\0';
    return true;
}

static void nfc_commissioning_apply(void)
{
    char text[64];
    if (!nfc_ndef_get_text(text, sizeof(text))) return;

    /* Parse "net=XXXXXX ch=YY" */
    char * net_ptr = strstr(text, "net=");
    char * ch_ptr  = strstr(text, "ch=");
    if (!net_ptr || !ch_ptr) return;

    uint32_t net = (uint32_t)strtoul(net_ptr + 4, NULL, 16);
    uint8_t  ch  = (uint8_t) strtoul(ch_ptr  + 3, NULL, 10);

    if (net == 0u || net > 0xFFFFFFu || ch < 1u || ch > 39u) return;

    nfc_commission_store_t store;
    memset(&store, 0, sizeof(store));
    store.magic       = NFC_COMMISSION_MAGIC;
    store.net_addr    = net;
    store.net_channel = ch;
    store.crc         = nfc_store_crc(&store);

    if (!nfc_store_write(&store)) return;

    lib_state->stopStack();   /* stopStack() triggers a system reboot */
}

static void apply_pending_commissioning(void)
{
    if (lib_storage == NULL) return;
    nfc_commission_store_t s;
    if (lib_storage->readPersistent(&s, sizeof(s)) != APP_RES_OK) return;
    if (!nfc_store_valid(&s)) return;

    lib_settings->setNetworkAddress(s.net_addr);
    lib_settings->setNetworkChannel(s.net_channel);

    nfc_commission_store_t cleared;
    memset(&cleared, 0, sizeof(cleared));
    nfc_store_write(&cleared);
}

static void nfc_write_address_ndef(app_addr_t node_addr)
{
    uint8_t payload[TXT_HDR_SIZE + 12];
    payload[0] = 0x02u;   /* UTF-8, lang code len = 2 */
    payload[1] = 0x65u;   /* 'e' */
    payload[2] = 0x6Eu;   /* 'n' */

    int addr_len = snprintf((char *)&payload[TXT_HDR_SIZE],
                            sizeof(payload) - TXT_HDR_SIZE,
                            "0x%08X", (unsigned)node_addr);
    if (addr_len <= 0) return;

    uint8_t       ndef_buf[64];
    ndef_record_t ndef_msg;
    ndef_msg.buffer = ndef_buf;

    uint16_t ndef_size = ndef_create(&ndef_msg, 0x01u, true, true,
                                     "T", 1u,
                                     payload,
                                     (size_t)(TXT_HDR_SIZE + addr_len));

    uint8_t * tag = &m_nfc_memory[T2_HEADER_SIZE];
    tag[0] = 0x03u;
    tag[1] = (uint8_t)ndef_size;
    memcpy(&tag[2], ndef_buf, ndef_size);
    tag[2u + ndef_size] = 0xFEu;
    tag[3u + ndef_size] = 0x00u;
}

static void nfc_init_tag(void)
{
    app_addr_t node_addr = 0;
    lib_settings->getNodeAddress(&node_addr);

    memset(m_nfc_memory, 0, sizeof(m_nfc_memory));
    nfc_write_address_ndef(node_addr);

    t2_emu_init(nfc_event_cb, m_nfc_memory, NFC_MEMORY_SIZE);
    nfc_hw_start();
}

/* Scheduler task: poll NFC events (set asynchronously from the NFC IRQ). */
static uint32_t nfc_poll_task(void)
{
    uint8_t events = m_nfc_events;
    m_nfc_events   = 0u;

    if (events & NFC_EVT_COMMISSIONED)
    {
        /* Magenta = NFC commission received. */
        led1_blink(true, false, true);
        nfc_commissioning_apply();
    }
    else if (events & NFC_EVT_FIELD_ON)
    {
        /* White flash = NFC field detected (phone tap). */
        led1_blink(true, true, true);
    }

    return 200u;   /* check every 200 ms — well within Wirepas exec budget */
}

/* ── Wirepas data received (downlink) ───────────────────────────────────────── */

static app_lib_data_receive_res_e data_received_cb(
    const shared_data_item_t * item,
    const app_lib_data_received_t * data)
{
    (void)item;
    (void)data;
    /* Green blink = Wirepas packet received. */
    led1_blink(false, true, false);
    return APP_LIB_DATA_RECEIVE_RES_HANDLED;
}

static shared_data_item_t m_downlink_filter = {
    .cb     = data_received_cb,
    .filter = {
        .src_endpoint  = -1,   /* no filtering */
        .dest_endpoint = -1,
        .multicast_cb  = NULL,
    },
};

/* ── App_init ────────────────────────────────────────────────────────────────── */

void App_init(const app_global_functions_t * functions)
{
    (void) functions;

    /* --- Role: AUTOROLE_LE on first boot only --- */
    app_addr_t addr;
    if (lib_settings->getNodeAddress(&addr) != APP_RES_OK)
    {
        lib_settings->setNodeRole(APP_LIB_SETTINGS_ROLE_AUTOROLE_LE);
    }

    configureNodeFromBuildParameters();

    /* Override with any NFC commissioning staged before reboot. */
    apply_pending_commissioning();

    /* --- GPIO init --- */
    Gpio_init();

    static const gpio_out_cfg_t led_out = {
        .out_mode_cfg  = GPIO_OUT_MODE_PUSH_PULL,
        .level_default = GPIO_LEVEL_LOW,
    };
    Gpio_outputSetCfg(LED1_R, &led_out);
    Gpio_outputSetCfg(LED1_G, &led_out);
    Gpio_outputSetCfg(LED1_B, &led_out);

    /* Boot blink: white for 1 s. */
    led1_set(true, true, true);
    App_Scheduler_addTask_execTime(led1_boot_off_task, LED_BOOT_MS, 200u);

    /* --- Antenna diversity --- */
    antenna_init();

    /* --- NFC --- */
    nfc_init_tag();

    /* --- On-board sensors --- */
    bme688_cfg_t bme_cfg = {
        .i2c_addr = BME688_I2C_ADDR_LOW,   /* 0x76 */
        .pullup   = true,
    };
    (void) BME688_init(&bme_cfg);

    adxl367_cfg_t adxl_cfg = {
        .i2c_addr = ADXL367_I2C_ADDR_HIGH, /* 0x1D */
        .range    = ADXL367_RANGE_2G,
        .pullup   = true,
    };
    (void) ADXL367_init(&adxl_cfg);

    bmi270_cfg_t bmi_cfg = {
        .cs_gpio_id   = BOARD_GPIO_ID_SPI_CS_BMI270,
        .spi_clock_hz = 8000000u,
    };
    (void) BMI270_init(&bmi_cfg);
    /* TODO: BMI270_load_config() — wire in the Bosch ~8 KB config blob. */

    /* AEM10900 PMIC (energy harvester) — voltages go into the BLE beacon. */
    static const aem10900_cfg_t aem_cfg = AEM10900_LIION_DEFAULT_CFG;
    (void) AEM10900_init(&aem_cfg);

    /* Sensors start in standby; sensor_task (phase 0) wakes them before each read. */
    ADXL367_standby();

    /* --- Wirepas callbacks --- */
    Shared_Data_addDataReceivedCb(&m_downlink_filter);

    /* --- Scheduler tasks --- */
    App_Scheduler_addTask_execTime(nfc_poll_task,
                                   APP_SCHEDULER_SCHEDULE_ASAP, 500u);

    App_Scheduler_addTask_execTime(sensor_task,
                                   APP_SCHEDULER_SCHEDULE_ASAP,
                                   SENSOR_EXEC_US);

    App_Scheduler_addTask_execTime(beacon_tx_task,
                                   APP_SCHEDULER_SCHEDULE_ASAP,
                                   BEACON_EXEC_US);

    lib_state->startStack();
}
