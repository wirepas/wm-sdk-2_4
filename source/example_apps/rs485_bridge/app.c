/* RS485 ↔ Wirepas mesh bridge — Bienesis sensorv26 (nRF54L15)
 *
 * Wirepas → RS485: gateway sends a raw RS485 frame as Wirepas payload
 *   on EP_RS485_DOWN. The bridge puts it on the bus and waits for the
 *   motor controller reply, then forwards it back as EP_RS485_UP.
 *
 * RS485 → Wirepas: unsolicited frames received from the bus (replies or
 *   spontaneous status frames) are forwarded to the sink on EP_RS485_UP.
 *
 * Frame format (motor firmware protocol):
 *   STX(0x02) | ADDR | CMD | NBR_DATA | DATA[0..N-1] | END(0x03)
 */

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "api.h"
#include "mcu.h"
#include "node_configuration.h"
#include "shared_data.h"
#include "app_scheduler.h"
#include "gpio.h"
#include "usart.h"

#include "board.h"
#include "aem10900.h"
#include "wms_beacon_tx.h"
#include "wms_beacon_rx.h"
#include "shared_appconfig.h"

#include "nfc_hw.h"
#include "t2_emulation.h"
#include "ndef.h"
#include "lis2dw.h"
#include "ads1220.h"
#include "davis6410.h"
#include "umb.h"
#include "umb_config.h"
#include "rs485_uart.h"
#include "i2c.h"      /* low-level I2C HAL — used for the AEM10900 bus diagnostic */
#include "cbor.h"

#define DEBUG_LOG_MODULE_NAME "AP"
#define DEBUG_LOG_MAX_LEVEL LVL_INFO

/* Debug: silence noisy periodic logs (BLE RX / ACC / CTN / AEM) so the UMB
 * polling logs stand out. Set BLE_RX_VERBOSE to 1 to restore BLE RX decoding. */
#define BLE_RX_VERBOSE 0
#define DEBUG_LOG_UART_BAUDRATE 115200
#include "debug_log.h"

/* ANSI colour codes */
#define C0    "\033[0m"         /* reset          */
#define CBOLD "\033[1m"         /* bold           */
#define CCYN  "\033[36m"        /* cyan           */
#define CGRN  "\033[32m"        /* green          */
#define CYEL  "\033[33m"        /* yellow         */
#define CMAG  "\033[35m"        /* magenta        */
#define CBLU  "\033[34m"        /* blue           */
#define CRED  "\033[31m"        /* red            */
#define CDEC  "\033[90m"        /* dark grey — decimal annotations */

/* newlib-nano (--specs=nano.specs) builds printf/vsnprintf WITHOUT float support,
 * so "%f" emits nothing and corrupts the rest of the line. Format floats as fixed
 * point into a caller buffer using integer specifiers only, then print with "%s".
 *   dec = number of fractional digits (1..6).  Sign is kept on the whole part. */
static const char * fixed_str(char * buf, size_t n, float v, unsigned dec)
{
    int32_t scale = 1;
    for (unsigned i = 0; i < dec; i++) scale *= 10;
    bool neg = (v < 0.0f);
    if (neg) v = -v;
    int32_t scaled = (int32_t)(v * (float)scale + 0.5f);
    snprintf(buf, n, "%s%ld.%0*ld", neg ? "-" : "",
             (long)(scaled / scale), (int)dec, (long)(scaled % scale));
    return buf;
}

/* ── NFC — read node address + commissioning via NFC write ───────────────────
 *
 * READ  : phone reads the node Wirepas address as an NDEF text record.
 * WRITE : phone writes commissioning parameters as an NDEF text record.
 *
 * Write format (NFC Tools → "Text" record, language "en"):
 *   net=AABBCC;ch=7
 *   net=AABBCC;ch=7;addr=0000002A
 *
 *   net  — 24-bit network address, hex (6 digits)
 *   ch   — RF channel, decimal (1-11 for ISM24)
 *   addr — 32-bit node address, hex (optional; keep current if omitted)
 *
 * After removing the phone (field off), settings are applied and the node
 * reboots to join the network.
 */
#define NFC_MEMORY_SIZE  512
#define TXT_HDR_SIZE       3   /* NDEF text header: status byte + "en" */

static uint8_t m_nfc_memory[NFC_MEMORY_SIZE];

/* Network settings (net/ch/addr) cannot be changed while the stack is running
 * (setNetworkAddress returns APP_RES_INVALID_STACK_STATE). So NFC commissioning
 * persists the requested parameters and reboots; App_init applies them before
 * startStack (stack stopped) on the next boot.
 *
 * The persistent area is the application's 32-byte slice of lib_storage. The
 * record is CRC-protected and every write is verified by read-back, so a
 * corrupt or partially-written blob is never applied (which would otherwise
 * risk masking Remote-API settings on every boot). */
#define NFC_COMMISSION_MAGIC  0x4E464334u   /* "NFC4" */
#define NFC_STORE_WRITE_TRIES 3u

/* One-shot NFC commissioning request in the stack's 32-byte persistent area
 * (lib_storage). Pending iff net != 0; applied at boot then net cleared to 0.
 * The UMB polling config is stored separately in the app_persistent user area
 * (see umb_area_read/write), never here. */
typedef struct {
    uint32_t magic;
    uint32_t net;         /* commissioning: network addr; 0 = none pending      */
    uint32_t addr;        /* commissioning: node addr (if set_addr)             */
    uint8_t  ch;          /* commissioning: RF channel                          */
    uint8_t  set_addr;    /* commissioning: apply node addr?                    */
    uint8_t  _pad[2];
    uint32_t crc;         /* CRC32 over all preceding fields                    */
} nfc_commission_store_t;

/* Must fit the 32-byte application persistent area (see wms_storage.h). */
typedef char nfc_store_fits_check[(sizeof(nfc_commission_store_t) <= 32) ? 1 : -1];

/* Compact table-less CRC32 (poly 0xEDB88320). */
static uint32_t nfc_crc32(const void * data, size_t len)
{
    const uint8_t * p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= p[i];
        for (uint8_t k = 0; k < 8u; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

static uint32_t nfc_store_crc(const nfc_commission_store_t * s)
{
    return nfc_crc32(s, sizeof(*s) - sizeof(s->crc));
}

/* Validate magic, CRC and value ranges. Defends against flash corruption. */
static bool nfc_store_valid(const nfc_commission_store_t * s)
{
    if (s->magic != NFC_COMMISSION_MAGIC)        return false;
    if (s->crc   != nfc_store_crc(s))            return false;
    /* Commissioning is validated only when one is pending (net != 0). */
    if (s->net != 0u)
    {
        if (s->net > 0xFFFFFEu)                  return false;
        if (s->ch  < 1u  || s->ch  > 11u)        return false;
        if (s->set_addr && s->addr == 0u)        return false;
    }
    return true;
}

/* Commissioning is "pending" (to be applied once) when a network addr is set. */
static bool nfc_store_commission_pending(const nfc_commission_store_t * s)
{
    return s->net != 0u;
}

/* Write a record and confirm it by read-back. Returns true only if the stored
 * bytes match exactly. Retried a few times to ride out a transient failure. */
static bool nfc_store_write(const nfc_commission_store_t * s)
{
    if (lib_storage == NULL) return false;
    for (uint8_t attempt = 0; attempt < NFC_STORE_WRITE_TRIES; attempt++)
    {
        if (lib_storage->writePersistent(s, sizeof(*s)) != APP_RES_OK)
            continue;
        nfc_commission_store_t rb;
        if (lib_storage->readPersistent(&rb, sizeof(rb)) == APP_RES_OK
            && memcmp(&rb, s, sizeof(rb)) == 0)
            return true;
    }
    return false;
}

/* Invalidate the whole record (verified). Returns true if cleared. */
static bool nfc_store_clear(void)
{
    nfc_commission_store_t cleared;
    memset(&cleared, 0, sizeof(cleared));   /* magic = 0 → invalid */
    return nfc_store_write(&cleared);
}

/* Load the current record for read-modify-write. On invalid/absent, returns a
 * zeroed record with a valid magic (no commissioning pending, UMB disabled). */
static void nfc_store_load(nfc_commission_store_t * s)
{
    memset(s, 0, sizeof(*s));
    s->magic = NFC_COMMISSION_MAGIC;
    if (lib_storage == NULL) return;
    nfc_commission_store_t rb;
    if (lib_storage->readPersistent(&rb, sizeof(rb)) == APP_RES_OK
        && nfc_store_valid(&rb))
        *s = rb;
}

/* Runtime UMB polling config (loaded from the user flash area at boot, set via
 * EP_UMB_CFG). */
static umb_config_t m_umb_cfg;

/* ── UMB config persistence in the app_persistent "user" area (id 0x8AE573BA) ───
 * This 16 KB user area is SEPARATE from the stack's persistent-settings area used
 * by lib_storage / NFC commissioning, and is preserved across an app firmware
 * update. Stored as a fixed 64-byte record at offset 0 via lib_memory_area:
 *   [0..3] magic  [4] wire length  [5..47] wire  [48..51] CRC32(bytes 0..47). */
#define UMB_AREA_ID     0x8AE573BAu
#define UMB_AREA_MAGIC  0x554D4234u   /* "UMB4" */
#define UMB_REC_SIZE    64u           /* 4-aligned; holds 5 + wire(<=43) + crc */

static void umb_area_wait(void)
{
    if (lib_memory_area == NULL) return;
    while (lib_memory_area->isBusy(UMB_AREA_ID)) { }
}

/* Load the persisted UMB config into m_umb_cfg. Returns true if a valid record
 * was found and applied. */
static bool umb_area_read(void)
{
    if (lib_memory_area == NULL) return false;
    uint8_t buf[UMB_REC_SIZE];
    if (lib_memory_area->startRead(UMB_AREA_ID, buf, 0u, UMB_REC_SIZE)
            != APP_LIB_MEM_AREA_RES_OK) return false;
    umb_area_wait();

    uint32_t magic = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8)
                   | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    if (magic != UMB_AREA_MAGIC) return false;
    uint8_t len = buf[4];
    if (len == 0u || len > UMB_CFG_WIRE_MAX) return false;
    uint32_t crc = (uint32_t)buf[48] | ((uint32_t)buf[49] << 8)
                 | ((uint32_t)buf[50] << 16) | ((uint32_t)buf[51] << 24);
    if (crc != nfc_crc32(buf, 48u)) return false;

    return umb_config_parse(&buf[5], len, &m_umb_cfg);
}

/* Persist the pushed UMB config wire blob. Returns true on a verified write. */
static bool umb_area_write(const uint8_t * wire, uint8_t len)
{
    if (lib_memory_area == NULL || wire == NULL
        || len == 0u || len > UMB_CFG_WIRE_MAX) return false;

    uint8_t buf[UMB_REC_SIZE];
    memset(buf, 0, sizeof buf);
    buf[0] = (uint8_t)(UMB_AREA_MAGIC & 0xFFu);
    buf[1] = (uint8_t)(UMB_AREA_MAGIC >> 8);
    buf[2] = (uint8_t)(UMB_AREA_MAGIC >> 16);
    buf[3] = (uint8_t)(UMB_AREA_MAGIC >> 24);
    buf[4] = len;
    memcpy(&buf[5], wire, len);
    uint32_t crc = nfc_crc32(buf, 48u);
    buf[48] = (uint8_t)(crc & 0xFFu);
    buf[49] = (uint8_t)(crc >> 8);
    buf[50] = (uint8_t)(crc >> 16);
    buf[51] = (uint8_t)(crc >> 24);

    app_lib_mem_area_info_t info;
    if (lib_memory_area->getAreaInfo(UMB_AREA_ID, &info) != APP_LIB_MEM_AREA_RES_OK)
        return false;

    /* Erase the first sector only if the medium requires it (RRAM does not). */
    if (info.flash.erase_sector_size > 0u)
    {
        uint32_t base = 0u;
        size_t   nsec = 1u;
        (void)lib_memory_area->startErase(UMB_AREA_ID, &base, &nsec);
        umb_area_wait();
    }

    if (lib_memory_area->startWrite(UMB_AREA_ID, 0u, buf, UMB_REC_SIZE)
            != APP_LIB_MEM_AREA_RES_OK) return false;
    umb_area_wait();

    uint8_t rb[UMB_REC_SIZE];
    if (lib_memory_area->startRead(UMB_AREA_ID, rb, 0u, UMB_REC_SIZE)
            != APP_LIB_MEM_AREA_RES_OK) return false;
    umb_area_wait();
    return memcmp(rb, buf, UMB_REC_SIZE) == 0;
}

static uint32_t led_red_off_task(void);  /* used as a persist-failure cue */

/* Event flags — set from callback (IRQ context), drained in poll_task. */
#define NFC_EVT_FIELD_ON      (1u << 0)
#define NFC_EVT_FIELD_OFF     (1u << 1)
#define NFC_EVT_SELECTED      (1u << 2)
#define NFC_EVT_COMMISSIONED  (1u << 3)  /* write done + field off */

static volatile uint8_t m_nfc_events;
static volatile bool    m_nfc_write_pending;  /* DATA_WRITTEN seen, waiting for field off */

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
                m_nfc_events       |= NFC_EVT_COMMISSIONED;
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

/* Extract the text payload from an NDEF text record stored in the NFC tag
 * memory (at offset T2_HEADER_SIZE). Returns true and fills out[] on success. */
static bool nfc_ndef_get_text(char *out, size_t out_size)
{
    const uint8_t *p = &m_nfc_memory[T2_HEADER_SIZE];

    if (p[0] != 0x03u) return false;  /* not an NDEF TLV */

    uint16_t    tlv_len;
    const uint8_t *ndef;
    if (p[1] == 0xFFu)
    {
        tlv_len = ((uint16_t)p[2] << 8) | p[3];
        ndef    = p + 4;
    }
    else
    {
        tlv_len = p[1];
        ndef    = p + 2;
    }

    if (tlv_len < 3u) return false;

    uint8_t        flags    = ndef[0];
    uint8_t        type_len = ndef[1];
    const uint8_t *cur      = ndef + 2;

    uint32_t payload_len;
    if (flags & 0x10u)  /* SR — short record */
    {
        payload_len = *cur++;
    }
    else
    {
        payload_len = ((uint32_t)cur[0] << 24) | ((uint32_t)cur[1] << 16)
                    | ((uint32_t)cur[2] <<  8) |  (uint32_t)cur[3];
        cur += 4;
    }

    if (flags & 0x08u) cur++;  /* IL — skip ID length byte */

    cur += type_len;  /* skip type field ("T") */

    /* Text record: 3-byte header (status byte + "en") before the actual text */
    if (payload_len <= TXT_HDR_SIZE) return false;
    cur         += TXT_HDR_SIZE;
    payload_len -= TXT_HDR_SIZE;

    size_t n = (payload_len < out_size - 1u) ? payload_len : out_size - 1u;
    memcpy(out, cur, n);
    out[n] = '\0';
    return true;
}

/* Parse commissioning string and apply settings.
 * Called from poll_task (main context) — safe to call lib_settings and LOG. */
static void nfc_commissioning_apply(void)
{
    char text[64];
    if (!nfc_ndef_get_text(text, sizeof(text)))
    {
        LOG(LVL_ERROR, CRED "NFC commissioning: NDEF parse failed" C0);
        return;
    }

    LOG(LVL_INFO, CCYN "NFC commissioning: \"%s\"" C0, text);

    /* net=AABBCC (24-bit hex, mandatory) */
    char *p = strstr(text, "net=");
    if (!p)
    {
        LOG(LVL_ERROR, CRED "NFC commissioning: missing net=" C0);
        return;
    }
    app_lib_settings_net_addr_t net_addr =
        (app_lib_settings_net_addr_t)strtoul(p + 4, NULL, 16);

    /* ch=N (decimal, mandatory) */
    p = strstr(text, "ch=");
    if (!p)
    {
        LOG(LVL_ERROR, CRED "NFC commissioning: missing ch=" C0);
        return;
    }
    app_lib_settings_net_channel_t channel =
        (app_lib_settings_net_channel_t)strtoul(p + 3, NULL, 10);

    /* addr=ZZZZZZZZ (32-bit hex, optional) */
    app_addr_t node_addr = 0;
    bool set_addr = false;
    p = strstr(text, "addr=");
    if (p)
    {
        node_addr = (app_addr_t)strtoul(p + 5, NULL, 16);
        set_addr  = (node_addr != 0u);
    }

    /* Basic validation */
    if (net_addr == 0u || net_addr > 0xFFFFFEu)
    {
        LOG(LVL_ERROR, CRED "NFC commissioning: invalid net=0x%06X" C0,
            (unsigned)net_addr);
        return;
    }
    if (channel < 1u || channel > 11u)
    {
        LOG(LVL_ERROR, CRED "NFC commissioning: invalid ch=%u" C0,
            (unsigned)channel);
        return;
    }

    /* Persist for the next boot — the stack is running here so the settings
     * setters would fail. apply_pending_commissioning() applies it in App_init.
     * Read-modify-write so the durable UMB config section is preserved. */
    nfc_commission_store_t store;
    nfc_store_load(&store);
    store.net      = (uint32_t)net_addr;
    store.addr     = (uint32_t)node_addr;
    store.ch       = (uint8_t)channel;
    store.set_addr = set_addr ? 1u : 0u;
    store.crc      = nfc_store_crc(&store);

    /* Only reboot if the parameters were durably stored AND verified. Otherwise
     * keep running so the user can retry the tap (a reboot would change nothing
     * and hide the failure). */
    if (!nfc_store_write(&store))
    {
        LOG(LVL_ERROR, CRED "NFC commissioning: persist failed — not rebooting" C0);
        /* Visible failure cue without the debug UART: pulse the red LED. */
        Gpio_outputWrite(BOARD_GPIO_ID_LED_RED, GPIO_LEVEL_HIGH);
        App_Scheduler_addTask_execTime(led_red_off_task, 500U, 100U);
        return;
    }

    LOG(LVL_INFO,
        CCYN CBOLD "NFC commissioning staged: net=0x%06X ch=%u%s — rebooting" C0,
        (unsigned)net_addr, (unsigned)channel, set_addr ? " (addr set)" : "");

    /* Short delay so the log message is sent before reset */
    for (volatile uint32_t i = 0; i < 200000u; i++) {}

    NVIC_SystemReset();
}

/* At boot: load the durable UMB config, and apply (once) any staged NFC
 * commissioning. Must be called from App_init, before startStack.
 * m_umb_cfg must already hold defaults (see App_init order). */
static void apply_pending_commissioning(void)
{
    if (lib_storage == NULL) return;

    nfc_commission_store_t s;
    if (lib_storage->readPersistent(&s, sizeof(s)) != APP_RES_OK) return;
    if (s.magic != NFC_COMMISSION_MAGIC) return;   /* nothing stored */

    /* Discard anything that does not pass CRC + range validation. */
    if (!nfc_store_valid(&s))
    {
        LOG(LVL_ERROR, CRED "persistent record corrupt — discarded" C0);
        nfc_store_clear();
        return;
    }

    /* One-shot commissioning: apply, then clear the commissioning fields. */
    if (nfc_store_commission_pending(&s))
    {
        uint32_t a_net = s.net; uint8_t a_ch = s.ch; uint8_t a_sa = s.set_addr;
        app_res_e r1 = lib_settings->setNetworkAddress((app_lib_settings_net_addr_t)s.net);
        app_res_e r2 = lib_settings->setNetworkChannel((app_lib_settings_net_channel_t)s.ch);
        app_res_e r3 = APP_RES_OK;
        if (s.set_addr)
            r3 = lib_settings->setNodeAddress((app_addr_t)s.addr);

        s.net = 0u; s.addr = 0u; s.set_addr = 0u;   /* clear commissioning only */
        s.crc = nfc_store_crc(&s);
        if (!nfc_store_write(&s))
            LOG(LVL_ERROR, CRED "commissioning: clear failed (will re-apply)" C0);

        LOG(LVL_INFO,
            CCYN CBOLD "NFC commissioning applied: net=0x%06X ch=%u%s (r=%d/%d/%d)" C0,
            (unsigned)a_net, (unsigned)a_ch, a_sa ? " (addr)" : "",
            (int)r1, (int)r2, (int)r3);
    }
}

/* Build and write an NDEF text record into m_nfc_memory[T2_HEADER_SIZE..].
 * tlv_encode() from nfc/lib conflicts with util/tlv.h, so inlined here. */
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

    uint8_t *tag = &m_nfc_memory[T2_HEADER_SIZE];
    tag[0] = 0x03u;
    tag[1] = (uint8_t)ndef_size;
    memcpy(&tag[2], ndef_buf, ndef_size);
    tag[2u + ndef_size] = 0xFEu;
    tag[3u + ndef_size] = 0x00u;
}

static void nfc_init_address_tag(void)
{
    app_addr_t node_addr = 0;
    lib_settings->getNodeAddress(&node_addr);

    nfc_write_address_ndef(node_addr);

    t2_emu_res_e t2r  = t2_emu_init(nfc_event_cb, m_nfc_memory, NFC_MEMORY_SIZE);
    nfc_hw_res_e nfcr = nfc_hw_start();
    LOG(LVL_INFO, CCYN "NFC init: addr=0x%08X t2=%d nfc=%d" C0,
        (unsigned)node_addr, (int)t2r, (int)nfcr);
}

/* ── Multicast groups ───────────────────────────────────────────────────────── */
/* Group addresses use the APP_ADDR_MULTICAST (0x80000000) bitmask.
 * Add entries to m_multicast_groups[] to subscribe to more groups. */
#define MCAST_GROUP_RS485   (APP_ADDR_MULTICAST | 0x485200u)   /* "RS\x00" */

static const app_addr_t m_multicast_groups[] = {
    MCAST_GROUP_RS485,
};
#define MCAST_GROUP_COUNT   (sizeof(m_multicast_groups) / sizeof(m_multicast_groups[0]))

/* Called by the stack to check if this node belongs to a multicast group.
 * Without this, the network does not route multicast packets to this node. */
static bool multicast_group_cb(app_addr_t group_addr)
{
    for (uint8_t i = 0; i < MCAST_GROUP_COUNT; i++)
    {
        if (m_multicast_groups[i] == group_addr) return true;
    }
    return false;
}

/* ── Node info log ──────────────────────────────────────────────────────────── */
static void log_node_info(void)
{
    app_addr_t                     node_addr  = 0;
    app_lib_settings_net_addr_t    net_addr   = 0;
    app_lib_settings_net_channel_t channel    = 0;
    app_lib_settings_role_t        role       = 0;

    lib_settings->getNodeAddress(&node_addr);
    lib_settings->getNetworkAddress(&net_addr);
    lib_settings->getNetworkChannel(&channel);
    lib_settings->getNodeRole(&role);

    bool ll = (role & APP_LIB_SETTINGS_LL_ROLE_BIT) != 0;

    const char * role_str;
    switch (role & ~APP_LIB_SETTINGS_LL_ROLE_BIT)
    {
        case APP_LIB_SETTINGS_ROLE_SINK_LE:    role_str = "SINK";      break;
        case APP_LIB_SETTINGS_ROLE_HEADNODE_LE: role_str = "HEADNODE"; break;
        case APP_LIB_SETTINGS_ROLE_SUBNODE_LE:  role_str = "SUBNODE";  break;
        case APP_LIB_SETTINGS_ROLE_AUTOROLE_LE: role_str = "AUTOROLE"; break;
        case APP_LIB_SETTINGS_ROLE_ADVERTISER:  role_str = "ADVERTISER"; break;
        default:                                role_str = "UNKNOWN";   break;
    }

    LOG(LVL_INFO, CBOLD CCYN "Node addr   : 0x%08x " CDEC "(%u)" C0, node_addr, node_addr);
    LOG(LVL_INFO, CCYN "Net addr    : 0x%06x " CDEC "(%u)" C0, net_addr, net_addr);
    LOG(LVL_INFO, CCYN "RF channel  : %u" C0, channel);
    LOG(LVL_INFO, CBOLD CCYN "Mode        : %s %s" C0, role_str, ll ? "LL" : "LE");
    for (uint8_t i = 0; i < MCAST_GROUP_COUNT; i++)
        LOG(LVL_INFO, CCYN "Mcast group : 0x%08x " CDEC "(%u)" C0,
            m_multicast_groups[i], m_multicast_groups[i]);
}

/* ── Energy monitoring ──────────────────────────────────────────────────────── */
#define ENERGY_MONITOR_PERIOD_MS    10000U  /* sampling period                 */
#define ENERGY_MONITOR_EXEC_US      12000U  /* I2C budget: median-of-5 reads @ 100 kHz */

/* NTC parameters — NCP15XH103J03RC + 22 kΩ divider */
#define NTC_R25_OHM     10000.0f
#define NTC_B_K          3380.0f
#define NTC_RDIV_OHM    15000.0f   /* divider resistor on sensorv26 (measured) */
#define NTC_T_REF_K       298.15f

typedef struct
{
    float   storage_v;      /* battery voltage (V), formula: 4.8 × raw/256     */
    float   source_v;       /* harvester voltage (V), from lookup table         */
    float   temperature_c;  /* thermistor temperature (°C)                      */
    float   apm_uw;         /* average harvested power (µW), 0 if APM disabled  */
    uint8_t status;         /* raw STATUS register [0x0D]                       */
    bool    is_charging;    /* true when STATUS.CHARGE is set                   */
    bool    valid;          /* false until first successful read                */
} energy_monitor_t;

#if defined(USE_AEM10900)
static energy_monitor_t  m_energy;
#endif
static volatile uint16_t m_brx_total;

static struct {
    uint8_t  buf[APP_LIB_DATA_MAX_APP_CONFIG_NUM_BYTES];
    uint8_t  len;
    uint16_t type;        /* SHARED_APP_CONFIG_INCOMPATIBLE_FILTER = raw bytes */
    bool     pending;
    uint8_t  print_off;  /* next offset to print, 0xFF = header not yet printed */
} m_appconfig;

#if defined(USE_AEM10900)
static const aem10900_cfg_t m_pmic_cfg = AEM10900_LIION_DEFAULT_CFG;
#endif

static const lis2dw_cfg_t m_lis2dw_cfg = {
    .i2c_addr   = LIS2DW_I2C_ADDR_SA0_HIGH, /* SA0=VCC → 0x19 */
    .odr        = LIS2DW_ODR_25HZ,
    .mode       = LIS2DW_MODE_LP2,           /* 14-bit, low-power */
    .full_scale = LIS2DW_FS_2G,
    .low_noise  = true,
    .pullup     = true,  /* no external pull-ups on sensorv26 */
};

/* ADS1220 ADC — disabled for now (chip not responding on the bench; revisit later).
 * Set to 1 to re-enable init + periodic reads. */
#define USE_ADS1220  1

#if USE_ADS1220
/* ── CTN foliar / frost probe — ratiometric half-bridge on the ADS1220 ────────
 * Probe: Red = excitation/REFP1 (+3V3), White = mid-point/AIN1, Black = GND/REFN1.
 * A fixed 24.9 kΩ (inside the probe) and the 10k CTN form the divider; the ADC
 * reference is taken across the excitation (REFP1/REFN1 = AIN0/AIN3), so V_EX
 * cancels out → R_T = R_BRIDGE * (FS/code - 1), independent of the 3V3 value.
 *
 * MUX = AIN1-AVSS with PGA bypass (not AIN1-AIN2): Black sits at GND, so AINN≈AVSS.
 * A true differential pair needs the PGA enabled (AINN ≥ AVSS+0.2V), which GND
 * violates; PGA bypass is only allowed for the AINx-AVSS settings. Same math. */
#define CTN_R_BRIDGE_OHM   24900.0f   /* fixed series resistor inside the probe   */
#define ADS_FS             8388608.0f /* ADS1220 positive full scale = 2^23       */
/* Steinhart-Hart coefficients supplied with the probe */
#define CTN_SH_A           1.129241e-3f
#define CTN_SH_B           2.341077e-4f
#define CTN_SH_C           8.775468e-8f
#define ADS_AVG_SAMPLES    1u         /* 1 sample (purge already settles the spike) */
#define ADS_PURGE_SAMPLES  1u         /* discard 1 conversion to settle after WREG */

/* ── SP-110 pyranometer (irradiance) — absolute measurement, AIN2 ─────────────
 * Self-powered silicon photodiode: NEVER apply a voltage to its wires.
 * White=AIN2 (signal+), Black=GND, Clear=shield→GND (single point).
 * Internal 2.048 V reference, gain 4 → FS = 512 mV (0–400 mV calibrated range).
 *   V_mV = code/FS * (2048/4) = code * 512/FS ; irradiance = V_mV * 5.0 W/m². */
#define SP110_VREF_MV          2048.0f
#define SP110_GAIN             4.0f
#define SP110_CAL_WM2_PER_MV   5.0f    /* Apogee SP-110 calibration factor */

static const ads1220_regs_t m_ads_cfg_ctn = {
    .reg = {
        /* REG0: MUX=AIN1-AVSS, gain=1, PGA bypass (single-supply, AINN=AVSS) */
        ADS1220_MUX_AIN1_AVSS | ADS1220_GAIN_1 | ADS1220_PGA_BYPASS,
        /* REG1: 20 SPS, normal mode, single-shot (averaged in software) */
        ADS1220_DR_20SPS | ADS1220_MODE_NORMAL,
        /* REG2: external ratiometric ref REFP1/REFN1 (=AIN0/AIN3), 50/60 Hz FIR */
        ADS1220_VREF_EXT_AIN01 | ADS1220_FIR_50_60HZ,
        /* REG3: IDAC off (voltage-excitation mode) */
        0x00u,
    }
};

static const ads1220_regs_t m_ads_cfg_sp110 = {
    .reg = {
        /* REG0: MUX=AIN2-AVSS, gain=4 (PGA enabled) — absolute mV measurement */
        ADS1220_MUX_AIN2_AVSS | ADS1220_GAIN_4,
        /* REG1: 20 SPS, normal mode, single-shot */
        ADS1220_DR_20SPS | ADS1220_MODE_NORMAL,
        /* REG2: internal 2.048 V reference, 50/60 Hz FIR */
        ADS1220_VREF_INT | ADS1220_FIR_50_60HZ,
        /* REG3: IDAC off */
        0x00u,
    }
};

/* Ratiometric ADC code → CTN resistance (Ω). Returns <0 on out-of-range code
 * (probe shorted: code→FS, or open: code→0). */
static float ctn_code_to_ohms(int32_t code)
{
    if (code <= 0 || (float)code >= ADS_FS)
        return -1.0f;
    return CTN_R_BRIDGE_OHM * (ADS_FS / (float)code - 1.0f);
}

/* CTN resistance (Ω) → temperature (°C) via Steinhart-Hart. */
static float ctn_ohms_to_celsius(float r_t)
{
    float ln_r  = logf(r_t);
    float t_inv = CTN_SH_A + CTN_SH_B * ln_r + CTN_SH_C * ln_r * ln_r * ln_r;
    if (t_inv == 0.0f)
        return -999.0f;
    return (1.0f / t_inv) - 273.15f;
}

/* SP-110 ADC code (internal ref, gain 4) → photodiode voltage (mV). */
static float sp110_code_to_mv(int32_t code)
{
    return (float)code * (SP110_VREF_MV / SP110_GAIN) / ADS_FS;
}
#endif /* USE_ADS1220 */

#if defined(USE_AEM10900)
/* Convert AEM10900 TEMP register raw value to °C using B-parameter equation.
 * TEMP encodes the NTC divider ratio: raw = 256 × R_NTC / (R_NTC + R_DIV) */
static float ntc_raw_to_celsius(uint8_t raw)
{
    if (raw == 0u || raw == 255u)
        return -999.0f;     /* sensor disconnected or shorted */

    float r_ntc = NTC_RDIV_OHM * (float)raw / (256.0f - (float)raw);
    float t_inv = (1.0f / NTC_T_REF_K) + (logf(r_ntc / NTC_R25_OHM) / NTC_B_K);
    return (1.0f / t_inv) - 273.15f;
}
#endif /* USE_AEM10900 */
static void appconfig_cb(uint16_t type, uint8_t length, uint8_t * value_p);
static bool appconfig_log(void);

#if defined(USE_AEM10900)
static void send_pmic_cbor_uplink(void);   /* defined near the other CBOR uplinks */
static bool m_pmic_ready = false;

/* One-shot low-level I2C diagnostic: confirms whether anything ACKs on the bus
 * and what the AEM10900 (0x2D) returns. Granular i2c_res_e tells bus from chip:
 *   6=ANACK (no device / chip unpowered), 8=BUS_HANG (SDA/SCL stuck / no pull-up). */
static void i2c_bus_diag(void)
{
    i2c_conf_t cfg = { .clock = 100000u, .pullup = true };
    i2c_res_e ir = I2C_init(&cfg);
    LOG(LVL_DEBUG, CYEL "I2C diag: init=%d (0=OK 4=ALREADY)" C0, (int)ir);

    uint8_t b;
    i2c_xfer_t probe = { .address = 0x41u /* AEM10900 addr */, .write_ptr = NULL,
                         .write_size = 0u, .read_ptr = &b, .read_size = 1u };
    i2c_res_e pr = I2C_transfer(&probe, NULL);
    LOG(LVL_DEBUG, CYEL "I2C 0x2D probe=%d (0=OK 6=ANACK 7=DNACK 8=BUS_HANG)" C0, (int)pr);

    uint8_t found = 0u;
    for (uint8_t a = 0x08u; a <= 0x77u; a++)
    {
        uint8_t rb;
        i2c_xfer_t sx = { .address = a, .write_ptr = NULL, .write_size = 0u,
                          .read_ptr = &rb, .read_size = 1u };
        if (I2C_transfer(&sx, NULL) == I2C_RES_OK)
        {
            LOG(LVL_INFO, CGRN "I2C device found @ 0x%02X" C0, a);
            found++;
        }
    }
    LOG(LVL_DEBUG, CYEL "I2C scan: %u device(s) on the bus" C0, found);
}

/* Median of 5 — rejects up to 2 outliers (I2C glitches / register transients). */
static float median5f(const float in[5])
{
    float s[5];
    for (uint8_t i = 0; i < 5u; i++) s[i] = in[i];
    for (uint8_t i = 1; i < 5u; i++) {
        float x = s[i]; int8_t j = (int8_t)i - 1;
        while (j >= 0 && s[j] > x) { s[j + 1] = s[j]; j--; }
        s[j + 1] = x;
    }
    return s[2];
}
static uint8_t median5u8(const uint8_t in[5])
{
    uint8_t s[5];
    for (uint8_t i = 0; i < 5u; i++) s[i] = in[i];
    for (uint8_t i = 1; i < 5u; i++) {
        uint8_t x = s[i]; int8_t j = (int8_t)i - 1;
        while (j >= 0 && s[j] > x) { s[j + 1] = s[j]; j--; }
        s[j + 1] = x;
    }
    return s[2];
}

/* Physical plausibility windows — a value outside these is impossible and is
 * discarded (the previous good reading is held instead). vsto can never exceed
 * the Li-Ion overcharge limit (VOVCH ≈ 4.22 V); a higher reading is a charge-
 * pulse overshoot / transient on the STO sense node, not the battery voltage. */
#define PMIC_VSTO_MIN_V   2.6f
#define PMIC_VSTO_MAX_V   4.22f
#define PMIC_VSRC_MIN_V   0.0f
#define PMIC_VSRC_MAX_V   3.0f
#define PMIC_TEMP_MIN_C  -30.0f
#define PMIC_TEMP_MAX_C   90.0f

static uint32_t energy_monitor_task(void)
{
    aem10900_res_e r;

    /* (Re)initialise on demand. Logged here (10 s apart) rather than at boot,
     * where a burst of lines overflows the 115200 UART TX buffer and is lost.
     * Also auto-recovers if the PMIC only becomes ready later (storage charges). */
    if (!m_pmic_ready)
    {
        r = AEM10900_init(&m_pmic_cfg);
        if (r != AEM10900_RES_OK)
        {
            LOG(LVL_DEBUG,
                CRED "AEM10900 init error %d (1=I2C_ERR 3=SYNC_TIMEOUT) — retry in 10s" C0,
                (int)r);
            /* On the first failure, dump a low-level bus diagnostic. */
            static bool diag_done = false;
            if (!diag_done) { diag_done = true; i2c_bus_diag(); }
            m_energy.valid = false;
            return ENERGY_MONITOR_PERIOD_MS;
        }
        LOG(LVL_INFO, CGRN "AEM10900 init OK" C0);
        m_pmic_ready = true;
    }

    /* Median-of-5 per analog channel: each register is sampled 5× and the
     * middle value kept, so up to two transient reads (mid-update register /
     * I2C glitch) cannot reach the output. */
    float   sto5[5], src5[5];
    uint8_t tr5[5];
    for (uint8_t k = 0; k < 5u; k++)
    {
        if ((r = AEM10900_read_storage_voltage(&sto5[k])) != AEM10900_RES_OK) goto fail;
        if ((r = AEM10900_read_source_voltage(&src5[k]))  != AEM10900_RES_OK) goto fail;
        if ((r = AEM10900_read_reg(AEM10900_REG_TEMP, &tr5[k])) != AEM10900_RES_OK) goto fail;
    }
    float sto_v = median5f(sto5);
    float src_v = median5f(src5);
    float tmp_c = ntc_raw_to_celsius(median5u8(tr5));

    /* Plausibility hold: keep the last good value when a sample is physically
     * impossible (e.g. vsto > VOVCH — charge-pulse overshoot the median can't
     * filter because it persists across all 5 reads). */
    if (sto_v >= PMIC_VSTO_MIN_V && sto_v <= PMIC_VSTO_MAX_V) m_energy.storage_v     = sto_v;
    if (src_v >= PMIC_VSRC_MIN_V && src_v <= PMIC_VSRC_MAX_V) m_energy.source_v      = src_v;
    if (tmp_c >  PMIC_TEMP_MIN_C && tmp_c <  PMIC_TEMP_MAX_C) m_energy.temperature_c = tmp_c;

    if ((r = AEM10900_get_status(&m_energy.status)) != AEM10900_RES_OK) goto fail;

    /* Power: only refresh when the AEM signals a completed, error-free APM
     * window (IRQFLG: APMDONE set, APMERR clear); otherwise hold the last value
     * so a partial/invalid window never reaches the output. */
    uint8_t irqflg = 0;
    (void)AEM10900_get_irqflg(&irqflg);   /* clear-on-read */
    bool apm_fresh = (irqflg & AEM10900_IRQ_APMDONE) && !(irqflg & AEM10900_IRQ_APMERR);
    if (apm_fresh)
        (void)AEM10900_read_apm(&m_energy.apm_uw, NULL);

    m_energy.is_charging = (m_energy.status & AEM10900_STATUS_CHARGE) != 0u;
    m_energy.valid       = true;

    char bsto[24], bsrc[24], btmp[24], bpwr[24];
    LOG(LVL_INFO,
        CYEL "PMIC: vsto=%sV vsrc=%sV T=%sC pwr=%suW%s chg=%d st=0x%02x" C0,
        fixed_str(bsto, sizeof bsto, m_energy.storage_v, 2),
        fixed_str(bsrc, sizeof bsrc, m_energy.source_v, 2),
        fixed_str(btmp, sizeof btmp, m_energy.temperature_c, 1),
        fixed_str(bpwr, sizeof bpwr, m_energy.apm_uw, 1),
        apm_fresh ? "" : "(held)",
        (int)m_energy.is_charging,
        m_energy.status);

    send_pmic_cbor_uplink();   /* EP09: [vsto, vsrc, T, pwr, status, chg] */

    LOG(LVL_INFO, "BLE RX total: %u", (unsigned)m_brx_total);
    return ENERGY_MONITOR_PERIOD_MS;

fail:
    LOG(LVL_WARNING, CRED "PMIC read error %d — will re-init" C0, r);
    m_pmic_ready   = false;   /* force a fresh init next cycle */
    m_energy.valid = false;
    LOG(LVL_INFO, "BLE RX total: %u", (unsigned)m_brx_total);
    return ENERGY_MONITOR_PERIOD_MS;
}
#endif /* USE_AEM10900 */

/* ── BLE beacon TX — Wirepas network info ─────────────────────────────────────
 * Always-on advertiser (independent of the PMIC). Emits UUID 0x1234 + mfr
 * 0xFFFF/type 0x01 carrying the node's Wirepas route/neighbour state. The PMIC
 * (AEM10900), when fitted, is read separately and only logged. */
#define BEACON_SERVICE_UUID     0x1234u
#define BEACON_COMPANY_ID_LO    0xFFu
#define BEACON_COMPANY_ID_HI    0xFFu
#define BEACON_VERSION          0x01u
#define BEACON_INTERVAL_MS      1000u
#define BEACON_POWER_DBM        4

#define BEACON_TX_PERIOD_MS   1000u
#define BEACON_TX_EXEC_US     2000u   /* tiny budget: must not starve the LL radio */

static bool          m_beacon_tx_started = false;
/* START/STOP controlled by CMD_BEACON_TX (0x03) from a command beacon. */
static volatile bool m_beacon_tx_enable  = true;

static uint32_t beacon_tx_task(void)
{
    /* Honour START/STOP: when disabled, ensure the beacon is off and idle. */
    if (!m_beacon_tx_enable)
    {
        if (m_beacon_tx_started)
        {
            lib_beacon_tx->enableBeacons(false);
            m_beacon_tx_started = false;
        }
        return BEACON_TX_PERIOD_MS;
    }

    app_addr_t addr = 0;
    lib_settings->getNodeAddress(&addr);

    /* Gather Wirepas network state to advertise: route to sink + neighbours. */
    app_lib_state_route_info_t route;
    memset(&route, 0, sizeof(route));
    lib_state->getRouteInfo(&route);

    app_lib_state_nbor_info_t nbor_buf[8];
    app_lib_state_nbor_list_t nlist = { .number_nbors = 8u, .nbors = nbor_buf };
    lib_state->getNbors(&nlist);

    uint8_t nbor_count = (uint8_t)nlist.number_nbors;
    int8_t  best_rssi  = -128;
    for (uint32_t n = 0; n < nlist.number_nbors; n++)
        if (nbor_buf[n].norm_rssi > best_rssi) best_rssi = nbor_buf[n].norm_rssi;
    if (best_rssi == -128) best_rssi = 0;

    uint32_t sink = (uint32_t)route.sink;

    uint8_t pdu[40];
    uint8_t i = 0;

    pdu[i++] = 0x42u;                           /* ADV_NONCONN_IND */
    pdu[i++] = (uint8_t)(addr >>  0);
    pdu[i++] = (uint8_t)(addr >>  8);
    pdu[i++] = (uint8_t)(addr >> 16);
    pdu[i++] = (uint8_t)(addr >> 24);
    pdu[i++] = 0x00u;
    pdu[i++] = 0x00u;

    pdu[i++] = 0x02u; pdu[i++] = 0x01u; pdu[i++] = 0x04u;          /* Flags */

    pdu[i++] = 0x03u; pdu[i++] = 0x03u;                            /* UUID16 list */
    pdu[i++] = (uint8_t)(BEACON_SERVICE_UUID & 0xFFu);
    pdu[i++] = (uint8_t)(BEACON_SERVICE_UUID >> 8);

    pdu[i++] = 0x02u; pdu[i++] = 0x0Au; pdu[i++] = (uint8_t)BEACON_POWER_DBM; /* TX pwr */

    /* Mfr Specific Data — Wirepas network info:
     * type(0x01) node(4 LE) sink(4 LE) cost(1) nbor_count(1) best_rssi(1) */
    pdu[i++] = 0x0Fu; pdu[i++] = 0xFFu;
    pdu[i++] = BEACON_COMPANY_ID_LO; pdu[i++] = BEACON_COMPANY_ID_HI;
    pdu[i++] = BEACON_VERSION;                  /* 0x01 = network info */
    pdu[i++] = (uint8_t)(addr >>  0);
    pdu[i++] = (uint8_t)(addr >>  8);
    pdu[i++] = (uint8_t)(addr >> 16);
    pdu[i++] = (uint8_t)(addr >> 24);
    pdu[i++] = (uint8_t)(sink >>  0);
    pdu[i++] = (uint8_t)(sink >>  8);
    pdu[i++] = (uint8_t)(sink >> 16);
    pdu[i++] = (uint8_t)(sink >> 24);
    pdu[i++] = route.cost;                      /* route cost to sink (0xFF = none) */
    pdu[i++] = nbor_count;                       /* number of neighbours */
    pdu[i++] = (uint8_t)best_rssi;               /* best neighbour norm RSSI (signed) */

    if (!m_beacon_tx_started)
    {
        int8_t pwr = BEACON_POWER_DBM;
        lib_beacon_tx->clearBeacons();
        lib_beacon_tx->setBeaconInterval(BEACON_INTERVAL_MS);
        lib_beacon_tx->setBeaconPower(0, &pwr);
        lib_beacon_tx->setBeaconChannels(0, APP_LIB_BEACON_TX_CHANNELS_ALL);
        lib_beacon_tx->setBeaconContents(0, pdu, i);
        lib_beacon_tx->enableBeacons(true);
        m_beacon_tx_started = true;
        LOG(LVL_DEBUG,
            CBOLD CBLU "BLE beacon enabled (no PMIC) addr=0x%08x " CDEC "(%u) "
            CBLU "UUID=0x%04x" C0, addr, addr, BEACON_SERVICE_UUID);
    }
    else
    {
        lib_beacon_tx->setBeaconContents(0, pdu, i);
    }
    return BEACON_TX_PERIOD_MS;
}

/* ── Wirepas endpoints ──────────────────────────────────────────────────────── */
#define EP_RS485_DOWN   1   /* gateway → bridge → motor (commands) */
#define EP_RS485_UP     2   /* motor → bridge → gateway (replies)  */
#define EP_PMIC_CBOR    9   /* periodic AEM10900 PMIC data as CBOR array */
#define EP_HEARTBEAT   10   /* periodic uplink counter              */
#define EP_SENSOR_CBOR 11   /* periodic CTN [T_C, R_T, diag] as CBOR array */
#define EP_IRRADIANCE  12   /* periodic SP-110 [W/m2, mV, diag] as CBOR array */
#define EP_WIND_CBOR   13   /* Davis 6410 [speed_ms, gust_ms, dir_deg] as CBOR  */
#define EP_UMB_DATA    14   /* UMB readings: CBOR array of [channel, value] pairs */
#define EP_UMB_CFG     50   /* downlink: upload UMB polling config (wire format)  */

/* ── Protocol constants ─────────────────────────────────────────────────────── */
#define FRAME_STX           0x02U
#define FRAME_END           0x03U
#define FRAME_MAX_LEN       20U     /* STX+ADDR+CMD+NBR+16data+END */

/* ── Motor controller (STM32G030) RS485 protocol ─────────────────────────────── */
#define MOTOR_ADDR          0x01U   /* STM32G030 RS485 address      */
#define CMD_SENSOR_DATA     0x20U   /* sensor snapshot — above motor protocol range (max 0x18) */

/* ── RS485 / UART ───────────────────────────────────────────────────────────── */
#define RS485_BAUD_RATE     115200U

/* Poll task period in ms.
 * NOTE: the Wirepas scheduler calls this task at ~0.7 ms effective intervals
 * despite the 5 ms return value, so ticks ≠ ms×5. */
#define POLL_PERIOD_MS              5U
/* Settle window after a command before reading the reply. On nRF54L15 the UARTE
 * FIFO is only flushed to RAM on STOP, and FRAMETIMEOUT is effectively one-shot
 * (fires spuriously right after arming), so we don't use events: we wait long
 * enough for the entire reply — including a mid-gap fragment (the motor can pause
 * ≥8.7 ms mid-frame) — to arrive and the bus to go idle, then STOP once and read.
 * Sniffer shows the full 13-byte reply within ~15 ms; at ~0.65 ms/tick, 48 ticks
 * ≈ 31 ms gives margin for MOVE-state inter-fragment delays. */
#define RS485_REPLY_SETTLE_TICKS    48U

/* Ticks until the reply settle window elapses (0 = idle, not awaiting a reply) */
static uint16_t m_reply_ticks_left = 0;

/* Pending TX: one command queued while waiting for a motor reply.
 * A new downlink command arriving mid-reply would re-arm RX and lose the bytes
 * in flight, so we defer it here and send it once the reply is done or timed out. */
#define PENDING_TX_MAX  FRAME_MAX_LEN
static uint8_t m_pending_tx[PENDING_TX_MAX];
static uint8_t m_pending_tx_len = 0;

/* True while a reply is being awaited (used by downlink_cb to defer new commands) */
static inline bool rs485_busy(void) { return m_reply_ticks_left > 0u; }

/* Latest sensor readings shared between sensor_read_task and RS485/uplink helpers */
static struct {
    int16_t x_raw;
    int16_t y_raw;
    int16_t z_raw;
    int32_t adc_raw;        /* averaged CTN code — kept for EP10 / RS485 forward */
    float   ctn_temp_c;     /* CTN temperature (°C), -999 if probe fault          */
    float   ctn_ohm;        /* CTN resistance (Ω)                                 */
    uint8_t ctn_diag;       /* 0 = OK, 1 = probe open/shorted (code saturated)    */
    float   irr_wm2;        /* SP-110 irradiance (W/m²)                           */
    float   irr_mv;         /* SP-110 photodiode voltage (mV)                     */
    uint8_t irr_diag;       /* 0 = OK, 1 = saturated, 2 = negative (wiring?)      */
} m_sensor;

/* Set from ISR on VBAT_EXT_nFAULT falling edge; cleared and logged from poll_task */
static volatile bool m_vbat_fault = false;

/* ── DE pin helpers ─────────────────────────────────────────────────────────── */
static inline void de_tx(void)
{
    Gpio_outputWrite(BOARD_GPIO_ID_RS485_DE, GPIO_LEVEL_HIGH);
}

static inline void de_rx(void)
{
    Gpio_outputWrite(BOARD_GPIO_ID_RS485_DE, GPIO_LEVEL_LOW);
}

/* ── RS485 TX (main context only — blocks until frame is physically sent) ───── */
static void rs485_send(const uint8_t * data, uint8_t len)
{
    if (len == 0) return;

    de_tx();
    bool ok = rs485_uart_send(data, len);   /* UARTE20, polling DMA, ~1.3 ms for 15 B */
    de_rx();
    rs485_uart_rx_arm();   /* clear buffer + arm DMA RX immediately after DE goes low */
    m_reply_ticks_left = RS485_REPLY_SETTLE_TICKS;   /* read the reply after settling */

    if (ok)
    {
        LOG(LVL_INFO, "RS485 TX %u B: %02x %02x %02x %02x ...",
            len, data[0],
            len > 1u ? data[1] : 0u,
            len > 2u ? data[2] : 0u,
            len > 3u ? data[3] : 0u);
    }
    else
    {
        LOG(LVL_ERROR, CRED "RS485 TX DMA timeout - UARTE20 not responding" C0);
    }
}

/* ── Forward a complete RS485 frame to the Wirepas sink ─────────────────────── */
static void forward_to_sink(const uint8_t * frame, uint8_t len)
{
    app_lib_data_to_send_t pkt = {
        .bytes         = frame,
        .num_bytes     = len,
        .dest_address  = APP_ADDR_ANYSINK,
        .src_endpoint  = EP_RS485_UP,
        .dest_endpoint = EP_RS485_UP,
        .qos           = APP_LIB_DATA_QOS_HIGH,
        .flags         = APP_LIB_DATA_SEND_FLAG_NONE,
        .tracking_id   = APP_LIB_DATA_NO_TRACKING_ID,
    };
    Shared_Data_sendData(&pkt, NULL);
    LOG(LVL_INFO, CGRN "Wirepas UP %u bytes" C0, len);
}

/* ── Sensor data forwarding ─────────────────────────────────────────────────── */

/* Build and send a sensor-data frame to the motor controller via RS485:
 *   STX | MOTOR_ADDR | CMD_SENSOR_DATA | 10 | X_L X_H | Y_L Y_H | Z_L Z_H |
 *   ADC0 ADC1 ADC2 ADC3 | END
 */
static void forward_sensor_to_motor(void)
{
//     uint8_t frame[15];
//     uint8_t i = 0;
//     frame[i++] = FRAME_STX;
//     frame[i++] = MOTOR_ADDR;
//     frame[i++] = CMD_SENSOR_DATA;
//     frame[i++] = 10u;   /* NBR: 10 data bytes */
//     frame[i++] = (uint8_t)(m_sensor.x_raw);
//     frame[i++] = (uint8_t)((uint16_t)m_sensor.x_raw >> 8);
//     frame[i++] = (uint8_t)(m_sensor.y_raw);
//     frame[i++] = (uint8_t)((uint16_t)m_sensor.y_raw >> 8);
//     frame[i++] = (uint8_t)(m_sensor.z_raw);
//     frame[i++] = (uint8_t)((uint16_t)m_sensor.z_raw >> 8);
//     frame[i++] = (uint8_t)((uint32_t)m_sensor.adc_raw);
//     frame[i++] = (uint8_t)((uint32_t)m_sensor.adc_raw >> 8);
//     frame[i++] = (uint8_t)((uint32_t)m_sensor.adc_raw >> 16);
//     frame[i++] = (uint8_t)((uint32_t)m_sensor.adc_raw >> 24);
//     frame[i++] = FRAME_END;
//  //   rs485_send(frame, i);
}

/* Send the latest sensor snapshot to the Wirepas sink on EP_HEARTBEAT (EP 10):
 *   10 bytes: 3 × int16 (accel XYZ, LE) + 1 × int32 (ADC raw, LE)
 */
static void send_sensor_uplink(void)
{
    uint8_t payload[10];
    payload[0] = (uint8_t)(m_sensor.x_raw);
    payload[1] = (uint8_t)((uint16_t)m_sensor.x_raw >> 8);
    payload[2] = (uint8_t)(m_sensor.y_raw);
    payload[3] = (uint8_t)((uint16_t)m_sensor.y_raw >> 8);
    payload[4] = (uint8_t)(m_sensor.z_raw);
    payload[5] = (uint8_t)((uint16_t)m_sensor.z_raw >> 8);
    payload[6] = (uint8_t)((uint32_t)m_sensor.adc_raw);
    payload[7] = (uint8_t)((uint32_t)m_sensor.adc_raw >> 8);
    payload[8] = (uint8_t)((uint32_t)m_sensor.adc_raw >> 16);
    payload[9] = (uint8_t)((uint32_t)m_sensor.adc_raw >> 24);

    app_lib_data_to_send_t pkt = {
        .bytes         = payload,
        .num_bytes     = sizeof(payload),
        .dest_address  = APP_ADDR_ANYSINK,
        .src_endpoint  = EP_HEARTBEAT,
        .dest_endpoint = EP_HEARTBEAT,
        .qos           = APP_LIB_DATA_QOS_NORMAL,
        .flags         = APP_LIB_DATA_SEND_FLAG_NONE,
        .tracking_id   = APP_LIB_DATA_NO_TRACKING_ID,
    };
    Shared_Data_sendData(&pkt, NULL);
    LOG(LVL_DEBUG, "EP10 sensor uplink sent");
}

#if USE_ADS1220
/* Send the CTN measurement to the Wirepas sink on EP_SENSOR_CBOR (EP 11) as a
 * CBOR array: [T_C (float °C), R_T (float Ω), diag (uint, 0=OK 1=probe fault)].
 * 1 (array hdr) + 2×5 (float32) + 1 (small uint) = 12 bytes → 32-byte buf ample. */
static void send_adc_cbor_uplink(void)
{
    uint8_t buf[32];
    CborEncoder enc, arr;
    cbor_encoder_init(&enc, buf, sizeof(buf), 0);
    cbor_encoder_create_array(&enc, &arr, 3);
    cbor_encode_float(&arr, m_sensor.ctn_temp_c);
    cbor_encode_float(&arr, m_sensor.ctn_ohm);
    cbor_encode_uint(&arr, m_sensor.ctn_diag);
    cbor_encoder_close_container(&enc, &arr);

    size_t len = cbor_encoder_get_buffer_size(&enc, buf);

    app_lib_data_to_send_t pkt = {
        .bytes         = buf,
        .num_bytes     = len,
        .dest_address  = APP_ADDR_ANYSINK,
        .src_endpoint  = EP_SENSOR_CBOR,
        .dest_endpoint = EP_SENSOR_CBOR,
        .qos           = APP_LIB_DATA_QOS_NORMAL,
        .flags         = APP_LIB_DATA_SEND_FLAG_NONE,
        .tracking_id   = APP_LIB_DATA_NO_TRACKING_ID,
    };
    Shared_Data_sendData(&pkt, NULL);
    LOG(LVL_DEBUG, "EP11 CTN CBOR uplink sent (%u B)", (unsigned)len);
}

/* Send the SP-110 irradiance to the Wirepas sink on EP_IRRADIANCE (EP 12) as a
 * CBOR array: [W/m2 (float), mV (float), diag (uint 0=OK 1=sat 2=negative)]. */
static void send_irradiance_uplink(void)
{
    uint8_t buf[32];
    CborEncoder enc, arr;
    cbor_encoder_init(&enc, buf, sizeof(buf), 0);
    cbor_encoder_create_array(&enc, &arr, 3);
    cbor_encode_float(&arr, m_sensor.irr_wm2);
    cbor_encode_float(&arr, m_sensor.irr_mv);
    cbor_encode_uint(&arr, m_sensor.irr_diag);
    cbor_encoder_close_container(&enc, &arr);

    size_t len = cbor_encoder_get_buffer_size(&enc, buf);

    app_lib_data_to_send_t pkt = {
        .bytes         = buf,
        .num_bytes     = len,
        .dest_address  = APP_ADDR_ANYSINK,
        .src_endpoint  = EP_IRRADIANCE,
        .dest_endpoint = EP_IRRADIANCE,
        .qos           = APP_LIB_DATA_QOS_NORMAL,
        .flags         = APP_LIB_DATA_SEND_FLAG_NONE,
        .tracking_id   = APP_LIB_DATA_NO_TRACKING_ID,
    };
    Shared_Data_sendData(&pkt, NULL);
    LOG(LVL_DEBUG, "EP12 irradiance uplink sent (%u B)", (unsigned)len);
}
#endif /* USE_ADS1220 */

#if defined(USE_DAVIS6410)
/* ── Davis 6410/6415 anemometer + wind vane ────────────────────────────────── */
#define WIND_SAMPLE_MS   3000U    /* instantaneous-speed window (per Davis, ~2-3 s) */
#define WIND_REPORT_MS  60000U    /* PARAMETRABLE: gust window + uplink cadence      */
#define WIND_EXEC_US     2000U

static float    m_wind_speed_ms = 0.0f;   /* latest instantaneous speed */
static float    m_wind_gust_ms  = 0.0f;   /* max over the current report window */
static float    m_wind_dir_deg  = 0.0f;
static uint32_t m_wind_elapsed  = 0U;

/* Speed reed-switch interrupt: count a pulse (debounced in the driver). */
static void wind_speed_gpio_cb(gpio_id_t id, gpio_in_event_e event)
{
    (void)id;
    if (IS_FALLING_EDGE(event))
        Davis6410_on_pulse();
}

/* Send [speed_ms, gust_ms, dir_deg] on EP_WIND_CBOR (EP 13). */
static void send_wind_cbor_uplink(void)
{
    uint8_t buf[32];
    CborEncoder enc, arr;
    cbor_encoder_init(&enc, buf, sizeof(buf), 0);
    cbor_encoder_create_array(&enc, &arr, 3);
    cbor_encode_float(&arr, m_wind_speed_ms);
    cbor_encode_float(&arr, m_wind_gust_ms);
    cbor_encode_float(&arr, m_wind_dir_deg);
    cbor_encoder_close_container(&enc, &arr);

    size_t len = cbor_encoder_get_buffer_size(&enc, buf);
    app_lib_data_to_send_t pkt = {
        .bytes         = buf,
        .num_bytes     = len,
        .dest_address  = APP_ADDR_ANYSINK,
        .src_endpoint  = EP_WIND_CBOR,
        .dest_endpoint = EP_WIND_CBOR,
        .qos           = APP_LIB_DATA_QOS_NORMAL,
        .flags         = APP_LIB_DATA_SEND_FLAG_NONE,
        .tracking_id   = APP_LIB_DATA_NO_TRACKING_ID,
    };
    Shared_Data_sendData(&pkt, NULL);
}

/* Periodic: sample instantaneous speed, track the gust (max) over the report
 * window, and every WIND_REPORT_MS read the vane and uplink [speed, gust, dir]. */
static uint32_t wind_task(void)
{
    m_wind_speed_ms = Davis6410_sample_speed_ms(WIND_SAMPLE_MS);
    if (m_wind_speed_ms > m_wind_gust_ms) m_wind_gust_ms = m_wind_speed_ms;
    m_wind_elapsed += WIND_SAMPLE_MS;

    if (m_wind_elapsed >= WIND_REPORT_MS)
    {
        float d = Davis6410_read_direction_deg();
        if (d >= 0.0f) m_wind_dir_deg = d;

        char bv[16], bg[16], bd[16];
        LOG(LVL_INFO, "WIND: v=%s m/s  gust=%s m/s  dir=%s deg",
            fixed_str(bv, sizeof bv, m_wind_speed_ms, 2),
            fixed_str(bg, sizeof bg, m_wind_gust_ms, 2),
            fixed_str(bd, sizeof bd, m_wind_dir_deg, 0));

        send_wind_cbor_uplink();
        m_wind_gust_ms = 0.0f;
        m_wind_elapsed = 0U;
    }
    return WIND_SAMPLE_MS;
}
#endif /* USE_DAVIS6410 */

/* ── UMB (OTT/Lufft) meteorological sensors over RS485 @ 19200 ─────────────────
 * Config (devices, addresses, channels, cadence) is uploaded by a downlink
 * command on EP_UMB_CFG (see downlink_cb), applied live and persisted when small
 * enough. Each round the task runs one Multi-Channel Online Data Request (2Fh)
 * per configured device (e.g. WS501 @0x7001, WS100 @0x7002) and uplinks that
 * device's readings as CBOR [dev_addr, ch, value, ...] on EP_UMB_DATA. */
static float        m_umb_value[UMB_CFG_MAX_CH_PER_DEV];
static bool         m_umb_valid[UMB_CFG_MAX_CH_PER_DEV];
static uint8_t      m_umb_dev_idx = 0;
static bool         m_umb_waiting = false;

#define UMB_RESP_WAIT_MS   600U    /* > long-response ta (500 ms) + margin      */
#define UMB_GAP_MS          60U    /* gap between devices (>= 3 char times)     */
#define UMB_EXEC_US       1500U

static uint32_t umb_round_period_ms(void)
{
    uint32_t p = (uint32_t)m_umb_cfg.poll_period_s * 1000u;
    return p ? p : 60000u;
}

/* Uplink one device's readings: CBOR [dev_addr(uint), ch0(uint), v0(float), …];
 * a channel with no valid reading is encoded as null. */
static void send_umb_cbor_uplink(const umb_dev_cfg_t * d)
{
    uint8_t buf[128];
    CborEncoder enc, arr;
    cbor_encoder_init(&enc, buf, sizeof buf, 0);
    cbor_encoder_create_array(&enc, &arr, 1u + (size_t)d->num_channels * 2u);
    cbor_encode_uint(&arr, d->dev_addr);
    for (uint8_t i = 0; i < d->num_channels; i++)
    {
        cbor_encode_uint(&arr, d->channel[i]);
        if (m_umb_valid[i]) cbor_encode_float(&arr, m_umb_value[i]);
        else                cbor_encode_null(&arr);
    }
    cbor_encoder_close_container(&enc, &arr);

    size_t len = cbor_encoder_get_buffer_size(&enc, buf);
    app_lib_data_to_send_t pkt = {
        .bytes         = buf,
        .num_bytes     = len,
        .dest_address  = APP_ADDR_ANYSINK,
        .src_endpoint  = EP_UMB_DATA,
        .dest_endpoint = EP_UMB_DATA,
        .qos           = APP_LIB_DATA_QOS_NORMAL,
        .flags         = APP_LIB_DATA_SEND_FLAG_NONE,
        .tracking_id   = APP_LIB_DATA_NO_TRACKING_ID,
    };
    Shared_Data_sendData(&pkt, NULL);
}

/* Put a UMB request on the bus (no motor reply-settle); reply read next tick. */
static void umb_send(const uint8_t * req, uint8_t n)
{
    de_tx();
    (void)rs485_uart_send(req, n);
    de_rx();
    rs485_uart_rx_arm();
}

/* Format up to 40 bytes as hex into out for logging. */
static const char * umb_hex(char * out, size_t outsz, const uint8_t * p, uint8_t n)
{
    static const char H[] = "0123456789ABCDEF";
    uint8_t  max = (n > 80u) ? 80u : n;
    size_t   o   = 0;
    for (uint8_t i = 0; i < max && o + 3u < outsz; i++)
    {
        out[o++] = H[p[i] >> 4];
        out[o++] = H[p[i] & 0x0Fu];
        out[o++] = ' ';
    }
    out[o] = '\0';
    return out;
}

/* Advance to the next device; returns the delay until the next task run. */
static uint32_t umb_next_device(void)
{
    m_umb_waiting = false;
    m_umb_dev_idx++;
    if (m_umb_dev_idx >= m_umb_cfg.num_devs)
    {
        m_umb_dev_idx = 0;
        return umb_round_period_ms();
    }
    return UMB_GAP_MS;
}

/* Multi-device poll: one 2Fh transaction per device, two-phase (send / read).
 * One CBOR uplink per device; a full round every poll_period_s. */
static uint32_t umb_poll_task(void)
{
    if (!m_umb_cfg.enabled || m_umb_cfg.num_devs == 0u)
    {
        m_umb_waiting = false;
        m_umb_dev_idx = 0;
        return umb_round_period_ms();
    }

    umb_dev_cfg_t * d = &m_umb_cfg.dev[m_umb_dev_idx];
    if (d->num_channels == 0u) return umb_next_device();  /* nothing to poll */

    if (!m_umb_waiting)
    {
        uint8_t req[UMB_MAX_FRAME];
        uint8_t n = umb_build_multi_request(req, sizeof req, d->dev_addr,
                                            UMB_CONTROLLER_ADDR,
                                            d->channel, d->num_channels);
        if (n > 0u) umb_send(req, n);
        char hx[256];
        LOG(LVL_DEBUG, "UMB TX 0x%04x %uB: %s",
            d->dev_addr, n, umb_hex(hx, sizeof hx, req, n));
        m_umb_waiting = true;
        return UMB_RESP_WAIT_MS;
    }

    const uint8_t * frame = NULL;
    uint8_t total = rs485_uart_rx_stop();
    (void)rs485_uart_rx_check(total, &frame);   /* frame -> raw RX buffer start */

    {
        char hx[256];
        LOG(LVL_INFO, "UMB RX 0x%04x %uB: %s", d->dev_addr, total,
            (frame != NULL && total > 0u) ? umb_hex(hx, sizeof hx, frame, total)
                                          : "(none)");
    }

    umb_value_t vals[UMB_CFG_MAX_CH_PER_DEV];
    uint8_t got = (frame != NULL)
                ? umb_parse_multi_response(frame, total, vals, UMB_CFG_MAX_CH_PER_DEV)
                : 0u;
    /* Aggregate all channels on ONE line ("ch=val ...") to avoid overflowing the
     * log UART FIFO (which drops the tail when many lines print in one tick). */
    char   line[176];
    size_t o = 0;
    for (uint8_t i = 0; i < d->num_channels; i++)
    {
        bool ok = (i < got) && (vals[i].status == 0x00u);
        m_umb_valid[i] = ok;
        m_umb_value[i] = ok ? vals[i].value : 0.0f;

        char b[20];
        int  w = ok
               ? snprintf(line + o, sizeof(line) - o, "%u=%s ",
                          d->channel[i], fixed_str(b, sizeof b, vals[i].value, 2))
               : snprintf(line + o, sizeof(line) - o, "%u=ERR ", d->channel[i]);
        if (w > 0 && (size_t)w < sizeof(line) - o) o += (size_t)w;
    }
    LOG(LVL_INFO, "UMB 0x%04x %u/%u (%uB): %s",
        d->dev_addr, got, d->num_channels, total, line);

    rs485_uart_rx_arm();
    send_umb_cbor_uplink(d);
    return umb_next_device();
}

#if defined(USE_AEM10900)
/* Send the AEM10900 PMIC snapshot to the Wirepas sink on EP_PMIC_CBOR (EP 9) as
 * a CBOR array: [vsto (float V), vsrc (float V), temp (float °C),
 * pwr (float µW), status (uint), charging (bool)].
 * 1 (hdr) + 4×5 (float32) + 1 (uint) + 1 (bool) = 23 bytes → 32-byte buf ample. */
static void send_pmic_cbor_uplink(void)
{
    uint8_t buf[32];
    CborEncoder enc, arr;
    cbor_encoder_init(&enc, buf, sizeof(buf), 0);
    cbor_encoder_create_array(&enc, &arr, 6);
    cbor_encode_float(&arr, m_energy.storage_v);
    cbor_encode_float(&arr, m_energy.source_v);
    cbor_encode_float(&arr, m_energy.temperature_c);
    cbor_encode_float(&arr, m_energy.apm_uw);
    cbor_encode_uint(&arr, m_energy.status);
    cbor_encode_boolean(&arr, m_energy.is_charging);
    cbor_encoder_close_container(&enc, &arr);

    size_t len = cbor_encoder_get_buffer_size(&enc, buf);

    app_lib_data_to_send_t pkt = {
        .bytes         = buf,
        .num_bytes     = len,
        .dest_address  = APP_ADDR_ANYSINK,
        .src_endpoint  = EP_PMIC_CBOR,
        .dest_endpoint = EP_PMIC_CBOR,
        .qos           = APP_LIB_DATA_QOS_NORMAL,
        .flags         = APP_LIB_DATA_SEND_FLAG_NONE,
        .tracking_id   = APP_LIB_DATA_NO_TRACKING_ID,
    };
    Shared_Data_sendData(&pkt, NULL);
    LOG(LVL_DEBUG, "EP09 PMIC CBOR uplink sent (%u B)", (unsigned)len);
}
#endif /* USE_AEM10900 */

/* ── BLE beacon RX ──────────────────────────────────────────────────────────── */
/*
 * Callback runs in IRQ context — data is copied into a ring buffer and
 * processed (logged) from poll_task in cooperative context.
 */
#define BRX_PDU_MAX     38u     /* max BLE advertising PDU payload */
#define BRX_RING_LEN     8u     /* ring buffer depth               */

typedef struct {
    uint8_t  type;
    int8_t   rssi;
    uint8_t  len;
    uint8_t  data[BRX_PDU_MAX];
} brx_entry_t;

static volatile uint8_t  m_brx_wr = 0;
static volatile uint8_t  m_brx_rd = 0;
static brx_entry_t       m_brx_ring[BRX_RING_LEN];

// /* Called from IRQ — must be fast, no LOG, no malloc */
static void beacon_rx_cb(const app_lib_beacon_rx_received_t * pkt)
{
    m_brx_total++;

    uint8_t next = (uint8_t)((m_brx_wr + 1u) % BRX_RING_LEN);
    if (next == m_brx_rd) return;   /* ring full — drop frame */

    brx_entry_t * e = &m_brx_ring[m_brx_wr];
    e->type = pkt->type;
    e->rssi = pkt->rssi;
    e->len  = (pkt->length < BRX_PDU_MAX) ? pkt->length : BRX_PDU_MAX;
    memcpy(e->data, pkt->payload, e->len);
    m_brx_wr = next;
}

/* ── BLE beacon RX parser ────────────────────────────────────────────────────── */
#define BRX_AD_START    6u      /* AD structures start after BT addr (6) — PDU type is in packet->type */
#define BRX_FILTER_UUID BEACON_SERVICE_UUID

/*
 * Returns true if the AD list contains a given 16-bit UUID in:
 *  - type 0x02/0x03 (16-bit UUID list)
 *  - type 0x06/0x07 (128-bit UUID list, matches at BT SIG base UUID offset 12-13)
 */
static bool ad_has_uuid16(const uint8_t * ads, uint8_t len, uint16_t uuid)
{
    uint8_t pos = 0;
    while (pos + 1u < len)
    {
        uint8_t ad_len  = ads[pos];
        uint8_t ad_type = ads[pos + 1u];
        if (ad_len == 0u || pos + ad_len >= len) break;

        if (ad_type == 0x02u || ad_type == 0x03u)
        {
            /* 16-bit UUID list */
            for (uint8_t j = 2u; j + 1u <= ad_len; j += 2u)
            {
                uint16_t u = (uint16_t)ads[pos + j]
                           | ((uint16_t)ads[pos + j + 1u] << 8);
                if (u == uuid) return true;
            }
        }
        else if (ad_type == 0x06u || ad_type == 0x07u)
        {
            /* 128-bit UUID list: BT SIG base = XXXXXXXX-0000-1000-8000-00805F9B34FB
             * The 16-bit value sits at bytes [12..13] of each 16-byte UUID (LE order) */
            for (uint8_t j = 2u; j + 15u <= ad_len; j += 16u)
            {
                uint16_t u = (uint16_t)ads[pos + j + 12u]
                           | ((uint16_t)ads[pos + j + 13u] << 8);
                if (u == uuid) return true;
            }
        }
        pos += ad_len + 1u;
    }
    return false;
}

/* Log a single AD structure — called from cooperative context */
static void __attribute__((unused)) ad_log_one(uint8_t ad_type, const uint8_t * d, uint8_t dlen)
{
    switch (ad_type)
    {
        case 0x01u:
            LOG(LVL_INFO, "  Flags      : 0x%02x", d[0]);
            break;

        case 0x02u:
        case 0x03u:
            for (uint8_t j = 0; j + 1u < dlen; j += 2u)
            {
                uint16_t u = (uint16_t)d[j] | ((uint16_t)d[j + 1u] << 8);
                if (u == BRX_FILTER_UUID)
                {
                    LOG(LVL_INFO, "  UUID16     : " CBLU "0x%04x " CDEC "(%u) " CBLU "<-- match" C0, u, u);
                }
                else
                {
                    LOG(LVL_INFO, "  UUID16     : 0x%04x " CDEC "(%u)" C0, u, u);
                }
            }
            break;

        case 0x08u:
        case 0x09u:
        {
            char name[BRX_PDU_MAX];
            uint8_t nlen = (dlen < (uint8_t)(sizeof(name) - 1u))
                           ? dlen : (uint8_t)(sizeof(name) - 1u);
            memcpy(name, d, nlen);
            name[nlen] = '\0';
            LOG(LVL_INFO, "  Name       : %s", name);
            break;
        }

        case 0x0Au:
            LOG(LVL_INFO, "  TX Power   : %d dBm", (int)(int8_t)d[0]);
            break;

        case 0x16u:
            if (dlen >= 2u)
            {
                uint16_t u    = (uint16_t)d[0] | ((uint16_t)d[1] << 8);
                uint8_t  n    = dlen - 2u;
                const uint8_t * p = &d[2];
                /* Print up to 8 data bytes as hex */
                char hex[25] = {0};
                uint8_t hlen = (n < 8u) ? n : 8u;
                for (uint8_t k = 0; k < hlen; k++)
                {
                    hex[k * 3u]      = "0123456789ABCDEF"[p[k] >> 4];
                    hex[k * 3u + 1u] = "0123456789ABCDEF"[p[k] & 0xFu];
                    hex[k * 3u + 2u] = ' ';
                }
                LOG(LVL_INFO, "  Svc Data   : " CBLU "UUID=0x%04x " CDEC "(%u) " CBLU "len=%u data=%s" C0,
                    u, u, n, n ? hex : "(none)");
            }
            break;

        case 0xFFu:
            if (dlen >= 12u
                && d[0] == BEACON_COMPANY_ID_LO
                && d[1] == BEACON_COMPANY_ID_HI
                && d[2] == BEACON_VERSION)
            {
                /* Our Wirepas network-info format:
                 * type node(4) sink(4) cost(1) nbor_count(1) best_rssi(1) */
                uint32_t node_addr = (uint32_t)d[3]  | ((uint32_t)d[4]  << 8)
                                   | ((uint32_t)d[5] << 16) | ((uint32_t)d[6]  << 24);
                uint32_t sink_addr = (uint32_t)d[7]  | ((uint32_t)d[8]  << 8)
                                   | ((uint32_t)d[9] << 16) | ((uint32_t)d[10] << 24);
                uint8_t  cost      = d[11];
                uint8_t  nbors     = (dlen >= 13u) ? d[12] : 0u;
                int8_t   rssi      = (dlen >= 14u) ? (int8_t)d[13] : 0;
                LOG(LVL_INFO,
                    "  Mfr[Net]   : " CBLU "node=0x%08x sink=0x%08x cost=%u "
                    "nbors=%u rssi=%d" C0,
                    node_addr, sink_addr, cost, nbors, (int)rssi);
            }
            else if (dlen >= 2u)
            {
                uint16_t company = (uint16_t)d[0] | ((uint16_t)d[1] << 8);
                LOG(LVL_INFO, "  Mfr Data   : company=0x%04x " CDEC "(%u)" C0 " len=%u",
                    company, company, dlen - 2u);
            }
            break;

        default:
            LOG(LVL_INFO, "  AD[0x%02x]   : len=%u", ad_type, dlen);
            break;
    }
}

/* Minimum RSSI to log — filters distant/unrelated devices */
#define BRX_RSSI_MIN    (-90)

/* Execute a control command carried in a received beacon's manufacturer data.
 * Layout after the company id: type(1)=0x02 target(4 LE) cmd(1) param(4 LE).
 * A target of 0 is treated as a broadcast (acted on by any node). */
static void brx_handle_command(const uint8_t * d, uint8_t dlen)
{
    if (dlen < 12u) return;
    if (d[0] != BEACON_COMPANY_ID_LO || d[1] != BEACON_COMPANY_ID_HI) return;
    if (d[2] != 0x02u) return;   /* not a command beacon (0x01 = sensor) */

    uint32_t target = (uint32_t)d[3] | ((uint32_t)d[4] << 8)
                    | ((uint32_t)d[5] << 16) | ((uint32_t)d[6] << 24);
    uint8_t  cmd    = d[7];
    uint32_t param  = (uint32_t)d[8]  | ((uint32_t)d[9]  << 8)
                    | ((uint32_t)d[10] << 16) | ((uint32_t)d[11] << 24);

    app_addr_t my_addr = 0;
    lib_settings->getNodeAddress(&my_addr);
    if (target != 0u && target != (uint32_t)my_addr) return;   /* not for us */

    gpio_level_e lvl = (param != 0u) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
    switch (cmd)
    {
        case 0x01: Gpio_outputWrite(BOARD_GPIO_ID_LED_RED,   lvl); break;  /* LED red   */
        case 0x02: Gpio_outputWrite(BOARD_GPIO_ID_LED_GREEN, lvl); break;  /* LED green */
        case 0x03: m_beacon_tx_enable = (param != 0u); break;             /* card beacon START/STOP */
        default:   break;
    }
}

static void brx_log_entry(const brx_entry_t * e)
{
    if (e->len <= BRX_AD_START) return;
    if ((int)e->rssi < BRX_RSSI_MIN) return;   /* too far away — skip */

    const uint8_t * ads     = &e->data[BRX_AD_START];
    uint8_t         ads_len = e->len - BRX_AD_START;

    bool uuid_match = ad_has_uuid16(ads, ads_len, BRX_FILTER_UUID);

    if (!uuid_match) return;

#if BLE_RX_VERBOSE
    LOG(LVL_INFO,
        CBOLD CBLU "BLE RX addr=%02X:%02X:%02X:%02X:%02X:%02X  rssi=%d dBm" C0,
        e->data[5], e->data[4], e->data[3],
        e->data[2], e->data[1], e->data[0],
        (int)e->rssi);
#endif

    /* Walk the AD list: (optionally) log each, and always handle any command. */
    uint8_t pos = 0;
    while (pos + 1u < ads_len)
    {
        uint8_t ad_len  = ads[pos];
        uint8_t ad_type = ads[pos + 1u];
        if (ad_len == 0u || pos + ad_len >= ads_len) break;
#if BLE_RX_VERBOSE
        ad_log_one(ad_type, &ads[pos + 2u], ad_len - 1u);
#endif
        if (ad_type == 0xFFu)
            brx_handle_command(&ads[pos + 2u], ad_len - 1u);
        pos += ad_len + 1u;
    }
}

/* Send any command that arrived while we were waiting for a reply. */
static void flush_pending_tx(void)
{
    if (m_pending_tx_len == 0u) return;
    uint8_t len      = m_pending_tx_len;
    m_pending_tx_len = 0u;
    rs485_send(m_pending_tx, len);   /* arms the reply settle window itself */
}

/* ── Poll task: forward completed frames, handle reply timeout ───────────────── */
static uint32_t poll_task(void)
{
    /* Start BLE scanner once, on first task execution (stack is running here). */
    if (!lib_beacon_rx->isScannerStarted())
    {
        app_res_e r = lib_beacon_rx->startScanner(APP_LIB_BEACON_RX_CHANNEL_ALL);
        LOG(LVL_DEBUG, "BLE scanner start: %d", r);
    }

#if defined(USE_DAVIS6410)
    /* Wind-speed reed by polling: count each high→low transition (reed closes to
     * GND). Sampled every POLL_PERIOD_MS (5 ms) — fine up to the reed's ~90 Hz. */
    {
        static bool  wind_prev_high = true;
        gpio_level_e lvl;
        if (Gpio_inputRead(BOARD_GPIO_ID_WIND_SPEED, &lvl) == GPIO_RES_OK)
        {
            bool high = (lvl == GPIO_LEVEL_HIGH);
            if (wind_prev_high && !high) Davis6410_on_pulse();
            wind_prev_high = high;
        }
    }
#endif

    if (m_reply_ticks_left > 0U)
    {
        m_reply_ticks_left--;
        if (m_reply_ticks_left == 0U)
        {
            /* Settle window elapsed: the whole reply has arrived and the bus is
             * idle. Stop once (flushes FIFO -> RAM, latches the count) and read. */
            const uint8_t * frame = NULL;
            uint8_t total     = rs485_uart_rx_stop();
            uint8_t frame_len = rs485_uart_rx_check(total, &frame);
            if (frame_len > 0u)
            {
                LOG(LVL_INFO, "RX %u B", frame_len);
                forward_to_sink(frame, frame_len);
            }
            else if (total > 0u && frame != NULL)
            {
                /* Bytes arrived but no complete frame — show them to diagnose. */
                LOG(LVL_INFO, "RS485 incomplete got=%u: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                    total,
                    frame[0],  frame[1],  frame[2],  frame[3],
                    frame[4],  frame[5],  frame[6],  frame[7],
                    frame[8],  frame[9],  frame[10], frame[11],
                    frame[12], frame[13], frame[14], frame[15]);
            }
            else
            {
                LOG(LVL_INFO, "RS485 no reply (got=%u) buf: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                    total,
                    frame[0],  frame[1],  frame[2],  frame[3],
                    frame[4],  frame[5],  frame[6],  frame[7],
                    frame[8],  frame[9],  frame[10], frame[11],
                    frame[12], frame[13], frame[14], frame[15]);
            }
            rs485_uart_rx_arm();   /* re-arm for next reply */
            flush_pending_tx();    /* send any command queued mid-reply */
        }
    }

    /* Drain BLE beacon RX ring buffer */
    while (m_brx_rd != m_brx_wr)
    {
        brx_log_entry(&m_brx_ring[m_brx_rd]);
        m_brx_rd = (uint8_t)((m_brx_rd + 1u) % BRX_RING_LEN);
    }

    /* Log pending NFC state transitions + handle commissioning */
    uint8_t nfc_ev = m_nfc_events;
    if (nfc_ev)
    {
        m_nfc_events = 0;
        if (nfc_ev & NFC_EVT_FIELD_ON)
        {
            LOG(LVL_INFO, CCYN "NFC: field ON" C0);
            Gpio_outputWrite(BOARD_GPIO_ID_LED_RED, GPIO_LEVEL_HIGH);
        }
        if (nfc_ev & NFC_EVT_SELECTED)
            LOG(LVL_INFO, CCYN "NFC: tag SELECTED" C0);
        if (nfc_ev & NFC_EVT_COMMISSIONED)
            nfc_commissioning_apply();  /* parses NDEF, applies settings, reboots */
        else if (nfc_ev & NFC_EVT_FIELD_OFF)
        {
            LOG(LVL_INFO, CCYN "NFC: field OFF" C0);
            Gpio_outputWrite(BOARD_GPIO_ID_LED_RED, GPIO_LEVEL_LOW);
        }
    }

    /* Print AppConfig dump one line per task invocation */
    if (m_appconfig.pending)
    {
        if (!appconfig_log())
            m_appconfig.pending = false;
    }

    /* VBAT_EXT fault: nFAULT went low — log immediately */
    if (m_vbat_fault)
    {
        m_vbat_fault = false;
        LOG(LVL_ERROR, CRED "VBAT_EXT supply FAULT (nFAULT low)" C0);
    }

    return POLL_PERIOD_MS;
}

/* Sensor read task - simple blocking (like the AIN2 build that ran for hours).
 * Reads accel + (when enabled) ADS1220 CTN + SP-110 with minimal averaging,
 * publishes, then sleeps. Few samples keep one call under the stack's ~100 ms
 * per-callback budget. No state machine / chunk gate (those starved the radio). */
#define SENSOR_READ_PERIOD_MS   4000u
#define SENSOR_READ_EXEC_US    30000u  /* small reservation: big values starve LL  */

static uint8_t  m_lis_fail = 0u;       /* consecutive LIS2DW read failures */

static void sensor_read_accel(void)
{
    lis2dw_sample_t acc = {0};
    if (LIS2DW_read_xyz(&acc) != LIS2DW_RES_OK)
    {
        if ((m_lis_fail++ % 8u) == 0u)
        {
            if (LIS2DW_init(&m_lis2dw_cfg) == LIS2DW_RES_OK)
            {
                LOG(LVL_INFO, CGRN "LIS2DW reinit OK" C0);
            }
            else
            {
                LOG(LVL_WARNING, CRED "LIS2DW reinit failed" C0);
            }
        }
    }
    else
    {
        m_lis_fail = 0u;
    }
    m_sensor.x_raw = acc.x;
    m_sensor.y_raw = acc.y;
    m_sensor.z_raw = acc.z;
    float x_mg = (acc.x >> 2) * 0.244f;
    float y_mg = (acc.y >> 2) * 0.244f;
    float z_mg = (acc.z >> 2) * 0.244f;
    char bx[24], by[24], bz[24];
    LOG(LVL_DEBUG, CMAG "ACC  x=%s y=%s z=%s mg" C0,
        fixed_str(bx, sizeof bx, x_mg, 1),
        fixed_str(by, sizeof by, y_mg, 1),
        fixed_str(bz, sizeof bz, z_mg, 1));
}

static void sensor_publish(void)
{
    forward_sensor_to_motor();
    send_sensor_uplink();          /* EP10: accel + CTN raw */
#if USE_ADS1220
    send_adc_cbor_uplink();        /* EP11: CTN [T_C, R_T, diag] */
    send_irradiance_uplink();      /* EP12: SP-110 [W/m2, mV, diag] */
#endif
}

#if USE_ADS1220
/* Blocking: discard ADS_PURGE_SAMPLES then average ADS_AVG_SAMPLES conversions. */
static bool sensor_read_code(int32_t * out_code)
{
    int32_t raw = 0;
#if ADS_PURGE_SAMPLES > 0
    for (uint8_t i = 0u; i < ADS_PURGE_SAMPLES; i++)
        (void)ADS1220_read_single(&raw, 200u);
#endif
    int64_t acc = 0; uint8_t ok = 0u;
    for (uint8_t i = 0u; i < ADS_AVG_SAMPLES; i++)
        if (ADS1220_read_single(&raw, 200u) == ADS1220_RES_OK) { acc += raw; ok++; }
    if (ok == 0u) return false;
    *out_code = (int32_t)(acc / ok);
    return true;
}

static void sensor_finalize_ctn(int32_t code)
{
    float r_t = ctn_code_to_ohms(code);
    m_sensor.adc_raw = code;
    if (r_t > 0.0f)
    {
        m_sensor.ctn_ohm    = r_t;
        m_sensor.ctn_temp_c = ctn_ohms_to_celsius(r_t);
        m_sensor.ctn_diag   = 0u;
        char bt[24];
        LOG(LVL_DEBUG, CMAG "CTN  code=%ld  R=%ld ohm  T=%s C" C0,
            (long)code, (long)(r_t + 0.5f),
            fixed_str(bt, sizeof bt, m_sensor.ctn_temp_c, 2));
    }
    else
    {
        m_sensor.ctn_ohm    = -1.0f;
        m_sensor.ctn_temp_c = -999.0f;
        m_sensor.ctn_diag   = 1u;
        LOG(LVL_DEBUG, CRED "CTN probe fault (code=%ld)" C0, (long)code);
    }
}

static void sensor_finalize_sp110(int32_t code)
{
    float v_mv = sp110_code_to_mv(code);
    m_sensor.irr_mv  = v_mv;
    m_sensor.irr_wm2 = v_mv * SP110_CAL_WM2_PER_MV;
    if (code < 0)                           m_sensor.irr_diag = 2u;
    else if ((float)code >= 0.99f * ADS_FS) m_sensor.irr_diag = 1u;
    else                                    m_sensor.irr_diag = 0u;
    char bv[24], bw[24];
    LOG(LVL_DEBUG, CMAG "SP110 code=%ld  V=%s mV  E=%s W/m2" C0,
        (long)code,
        fixed_str(bv, sizeof bv, v_mv, 3),
        fixed_str(bw, sizeof bw, m_sensor.irr_wm2, 1));
}
#endif /* USE_ADS1220 */

static uint32_t sensor_read_task(void)
{
    sensor_read_accel();

#if USE_ADS1220
    /* One ADS channel per cycle (alternating) so a config switch + purge + average
     * fits the small exec budget. Each channel is refreshed every ~2 cycles. */
    int32_t code;
    static bool read_ctn = true;
    if (read_ctn)
    {
        ADS1220_write_regs(&m_ads_cfg_ctn);
        if (sensor_read_code(&code)) { sensor_finalize_ctn(code); }
        else { m_sensor.ctn_diag = 1u; LOG(LVL_WARNING, CRED "CTN read failed" C0); }
    }
    else
    {
        ADS1220_write_regs(&m_ads_cfg_sp110);
        if (sensor_read_code(&code)) { sensor_finalize_sp110(code); }
        else { m_sensor.irr_diag = 1u; LOG(LVL_WARNING, CRED "SP110 read failed" C0); }
    }
    read_ctn = !read_ctn;
#endif

    sensor_publish();
    return SENSOR_READ_PERIOD_MS;
}

static uint32_t led_red_off_task(void); /* defined near App_init */

/* ── Wirepas downlink: receive command from gateway ─────── */
static app_lib_data_receive_res_e downlink_cb(
    const shared_data_item_t * item,
    const app_lib_data_received_t * data)
{
    (void)item;

    /* Flash red LED: 50 ms pulse on every Wirepas reception */
    Gpio_outputWrite(BOARD_GPIO_ID_LED_RED, GPIO_LEVEL_HIGH);
    App_Scheduler_addTask_execTime(led_red_off_task, 50U, 100U);

    /* Reject only empty frames here. The FRAME_MAX_LEN motor-frame limit is
     * enforced inside the EP_RS485_DOWN branch, so larger app commands (e.g. the
     * UMB config on EP_UMB_CFG, up to ~43 B) are not dropped before dispatch. */
    if (data->num_bytes == 0U)
        return APP_LIB_DATA_RECEIVE_RES_NOT_FOR_APP;

    const char * pkt_type;
    const char * pkt_color;
    if (data->dest_address == APP_ADDR_BROADCAST)
    {
        pkt_type  = "BCAST"; pkt_color = CYEL;
    }
    else if ((data->dest_address & APP_ADDR_MULTICAST) != 0u)
    {
        pkt_type  = "MCAST"; pkt_color = CMAG;
    }
    else
    {
        pkt_type  = "UCAST"; pkt_color = CCYN;
    }
    LOG(LVL_INFO,
        C0 "%s" CBOLD "%s" C0 CBOLD CGRN ": "
        "src=0x%08x " CDEC "(%u) " CGRN
        "dst=0x%08x " CDEC "(%u) " CGRN
        "ep=%u->%u hops=%u delay=%u ms (%u B)" C0,
        pkt_color, pkt_type,
        data->src_address, data->src_address,
        data->dest_address, data->dest_address,
        data->src_endpoint,
        data->dest_endpoint,
        data->hops,
        data->delay * 1000u / 128u,
        (unsigned)data->num_bytes);

    /* UMB polling config upload (dedicated command). Parse the wire format,
     * apply live and restart the polling round. */
    if (data->dest_endpoint == EP_UMB_CFG)
    {
        umb_config_t nc;
        if (umb_config_parse(data->bytes, (uint8_t)data->num_bytes, &nc))
        {
            m_umb_cfg     = nc;
            m_umb_waiting = false;
            m_umb_dev_idx = 0;
            bool persisted = umb_area_write(data->bytes, (uint8_t)data->num_bytes);
            LOG(LVL_INFO, CGRN "UMB cfg: en=%u period=%us devs=%u (persist=%s)" C0,
                m_umb_cfg.enabled, m_umb_cfg.poll_period_s, m_umb_cfg.num_devs,
                persisted ? "flash" : "FAIL");
            App_Scheduler_addTask_execTime(umb_poll_task, 500U, UMB_EXEC_US);
        }
        else
        {
            LOG(LVL_WARNING, CRED "UMB cfg: invalid (%u B)" C0,
                (unsigned)data->num_bytes);
        }
        return APP_LIB_DATA_RECEIVE_RES_HANDLED;
    }

    /* Only EP_RS485_DOWN commands are forwarded to the motor controller.
     * Other endpoints (e.g. EP 10 broadcasts from peer nodes) are logged
     * above but not put on the RS485 bus. */
    if (data->dest_endpoint == EP_RS485_DOWN)
    {
        if (rs485_busy())
        {
            /* Motor reply still in flight — sending now would re-arm RX and lose
             * the bytes in the buffer. Queue this command; it is sent once the
             * current reply completes or times out. Only the latest is kept. */
            uint8_t len = (data->num_bytes < PENDING_TX_MAX)
                          ? (uint8_t)data->num_bytes : PENDING_TX_MAX;
            memcpy(m_pending_tx, data->bytes, len);
            m_pending_tx_len = len;
        }
        else
        {
            rs485_send(data->bytes, (uint8_t)data->num_bytes);
        }
    }

    return APP_LIB_DATA_RECEIVE_RES_HANDLED;
}

static shared_data_item_t m_downlink_filter =
{
    .cb = downlink_cb,
    .filter = {
        .mode          = SHARED_DATA_NET_MODE_ALL,
        .src_endpoint  = SHARED_DATA_UNUSED_ENDPOINT,
        .dest_endpoint = SHARED_DATA_UNUSED_ENDPOINT,
        .multicast_cb  = multicast_group_cb,
    }
};


/* ── AppConfig reception ─────────────────────────────────────────────────────── */
/* Called by shared_appconfig for each TLV entry, or with
 * type=SHARED_APP_CONFIG_INCOMPATIBLE_FILTER for raw (non-TLV) data. */
static void appconfig_cb(uint16_t type, uint8_t length, uint8_t * value_p)
{
    if (m_appconfig.pending) return;  /* previous entry not yet printed — drop */
    m_appconfig.type      = type;
    m_appconfig.len       = (length < sizeof(m_appconfig.buf))
                            ? length : (uint8_t)sizeof(m_appconfig.buf);
    if (value_p && length)
        memcpy(m_appconfig.buf, value_p, m_appconfig.len);
    m_appconfig.print_off = 0xFFu;
    m_appconfig.pending   = true;
}

static bool appconfig_log(void)
{
    if (m_appconfig.type != SHARED_APP_CONFIG_INCOMPATIBLE_FILTER)
    {
        /* TLV entry — fits in one line */
        char hex[49] = "(empty)";
        if (m_appconfig.len)
        {
            uint8_t n = (m_appconfig.len < 16u) ? m_appconfig.len : 16u;
            for (uint8_t i = 0; i < n; i++)
            {
                hex[i * 3u]      = "0123456789ABCDEF"[m_appconfig.buf[i] >> 4];
                hex[i * 3u + 1u] = "0123456789ABCDEF"[m_appconfig.buf[i] & 0xFu];
                hex[i * 3u + 2u] = ' ';
            }
            hex[n * 3u] = '\0';
        }
        LOG(LVL_INFO, CYEL "AppConfig TLV: type=0x%04x len=%u data=%s" C0,
            m_appconfig.type, m_appconfig.len, hex);
        return false;
    }

    /* Raw (non-TLV) — paged hex dump, one line per call */
    if (m_appconfig.print_off == 0xFFu)
    {
        LOG(LVL_INFO, CYEL "AppConfig raw: len=%u" C0, m_appconfig.len);
        m_appconfig.print_off = 0u;
        return true;
    }

    uint8_t off = m_appconfig.print_off;
    if (off >= m_appconfig.len) return false;

    uint8_t n = ((uint8_t)(m_appconfig.len - off) < 16u)
                ? (uint8_t)(m_appconfig.len - off) : 16u;
    char hex[49];
    for (uint8_t i = 0; i < n; i++)
    {
        hex[i * 3u]      = "0123456789ABCDEF"[m_appconfig.buf[off + i] >> 4];
        hex[i * 3u + 1u] = "0123456789ABCDEF"[m_appconfig.buf[off + i] & 0xFu];
        hex[i * 3u + 2u] = ' ';
    }
    hex[n * 3u] = '\0';
    LOG(LVL_INFO, CYEL "  [%02u] %s" C0, off, hex);

    m_appconfig.print_off = (uint8_t)(off + 16u);
    return (m_appconfig.print_off < m_appconfig.len);
}

/* ── LED / switch helpers ─────────────────────────────────────────────────────── */

/* Scheduler task: turn red LED off after the blink pulse. */
static uint32_t led_red_off_task(void)
{
    Gpio_outputWrite(BOARD_GPIO_ID_LED_RED, GPIO_LEVEL_LOW);
    return APP_SCHEDULER_STOP_TASK;
}

/* Scheduler task: boot-blink — turn both LEDs off 1 s after boot. */
static uint32_t led_boot_off_task(void)
{
    Gpio_outputWrite(BOARD_GPIO_ID_LED_GREEN, GPIO_LEVEL_LOW);
    Gpio_outputWrite(BOARD_GPIO_ID_LED_RED,   GPIO_LEVEL_LOW);
    return APP_SCHEDULER_STOP_TASK;
}

/* Scheduler task: confirm switch state 20 ms after edge (debounce). */
static uint32_t switch_debounce_task(void)
{
    gpio_level_e level;
    if (Gpio_inputRead(BOARD_GPIO_ID_SWITCH, &level) == GPIO_RES_OK
        && level == GPIO_LEVEL_LOW)
    {
        Gpio_outputToggle(BOARD_GPIO_ID_LED_GREEN);
    }
    return APP_SCHEDULER_STOP_TASK;
}

/* GPIO interrupt callback: VBAT_EXT_nFAULT went low → power fault detected. */
static void vbat_fault_gpio_cb(gpio_id_t id, gpio_in_event_e event)
{
    (void)id;
    if (IS_FALLING_EDGE(event))
    {
        m_vbat_fault = true;   /* log from cooperative context in poll_task */
    }
}

/* GPIO interrupt callback: on falling edge, start/restart 20 ms debounce timer. */
static void switch_gpio_cb(gpio_id_t id, gpio_in_event_e event)
{
    (void)id;
    if (IS_FALLING_EDGE(event))
    {
        App_Scheduler_addTask_execTime(switch_debounce_task, 20U, 100U);
    }
}

/* Diagnostic: log the mesh route state so we can see if the node is joined and
 * routable (downlink needs a VALID route to a sink). */
static uint32_t route_dbg_task(void)
{
    app_lib_state_route_info_t ri;
    if (lib_state->getRouteInfo(&ri) == APP_RES_OK)
    {
        const char * st = (ri.state == APP_LIB_STATE_ROUTE_STATE_VALID)   ? "VALID"
                        : (ri.state == APP_LIB_STATE_ROUTE_STATE_PENDING) ? "PENDING"
                        :                                                   "INVALID";
        LOG(LVL_DEBUG, CYEL "ROUTE: %s cost=%u nexthop=0x%08x sink=0x%08x ch=%u" C0,
            st, ri.cost, ri.next_hop, ri.sink, ri.channel);
    }
    return 5000u;
}

/* ── App entry point ─────────────────────────────────────────────────────────── */
void App_init(const app_global_functions_t * functions)
{
    (void)functions;

    LOG_INIT();
    LOG(LVL_INFO, "RS485 bridge v1.1");

    /* Set AUTOROLE_LL only on first boot (before the address is written by
     * configureNodeFromBuildParameters). Subsequent boots preserve any role
     * configured via Remote API / CSAP. */
    app_addr_t _addr;
    if (lib_settings->getNodeAddress(&_addr) != APP_RES_OK)
    {
        lib_settings->setNodeRole(APP_LIB_SETTINGS_ROLE_AUTOROLE_LE);
    }

    configureNodeFromBuildParameters();

    /* UMB polling config: defaults, then restore from the app_persistent user
     * area (separate from commissioning; survives an app firmware update). */
    umb_config_defaults(&m_umb_cfg);
    if (umb_area_read())
        LOG(LVL_INFO, CCYN "UMB config restored: period=%us devs=%u" C0,
            m_umb_cfg.poll_period_s, m_umb_cfg.num_devs);

    /* Override net/ch/addr with any NFC commissioning staged before reboot.
     * Done after configureNode (which only sets unset values) so NFC wins. */
    apply_pending_commissioning();

    log_node_info();
    nfc_init_address_tag();

    /* Must initialise the GPIO HAL before any Gpio_* call. HAL_Open() (called
     * via LOG_INIT) does not do this — Gpio_init() must be explicit. */
    Gpio_init();

    /* LEDs: push-pull outputs, initially off */
    static const gpio_out_cfg_t led_cfg = {
        .out_mode_cfg  = GPIO_OUT_MODE_PUSH_PULL,
        .level_default = GPIO_LEVEL_LOW,
    };
    Gpio_outputSetCfg(BOARD_GPIO_ID_LED_GREEN, &led_cfg);
    Gpio_outputSetCfg(BOARD_GPIO_ID_LED_RED,   &led_cfg);

    /* Boot blink: both LEDs on for 1 s to confirm GPIO is working. */
    Gpio_outputWrite(BOARD_GPIO_ID_LED_GREEN, GPIO_LEVEL_HIGH);
    Gpio_outputWrite(BOARD_GPIO_ID_LED_RED,   GPIO_LEVEL_HIGH);
    App_Scheduler_addTask_execTime(led_boot_off_task, 1000U, 200U);

    /* Switch: pull-up input, toggle green LED on press (active-low, falling edge) */
    static const gpio_in_cfg_t switch_cfg = {
        .event_cb    = switch_gpio_cb,
        .event_cfg   = GPIO_IN_EVENT_FALLING_EDGE,
        .in_mode_cfg = GPIO_IN_PULL_UP,
    };
    Gpio_inputSetCfg(BOARD_GPIO_ID_SWITCH, &switch_cfg);

#if defined(USE_DAVIS6410)
    /* Wind-vane pot supply: push-pull output, default LOW (powered only during a
     * direction read). */
    static const gpio_out_cfg_t wind_pot_cfg = {
        .out_mode_cfg  = GPIO_OUT_MODE_PUSH_PULL,
        .level_default = GPIO_LEVEL_LOW,
    };
    Gpio_outputSetCfg(BOARD_GPIO_ID_WIND_POT_PWR, &wind_pot_cfg);

    /* Wind-speed reed: pull-up input, NO edge IRQ. The GPIO PORT/sense interrupt
     * is unreliable for P2 on this HAL/nRF54L15, so falling edges (reed closes to
     * GND) are detected by polling in poll_task (every POLL_PERIOD_MS). */
    static const gpio_in_cfg_t wind_speed_cfg = {
        .event_cb    = wind_speed_gpio_cb,       /* kept referenced; never fires  */
        .event_cfg   = GPIO_IN_EVENT_NONE,
        .in_mode_cfg = GPIO_IN_PULL_UP,
    };
    Gpio_inputSetCfg(BOARD_GPIO_ID_WIND_SPEED, &wind_speed_cfg);

    Davis6410_init(BOARD_GPIO_ID_WIND_POT_PWR);
#endif

    /* RS485 DE: push-pull output, initially LOW (receive mode) */
    static const gpio_out_cfg_t de_cfg = {
        .out_mode_cfg  = GPIO_OUT_MODE_PUSH_PULL,
        .level_default = GPIO_LEVEL_LOW,
    };
    Gpio_outputSetCfg(BOARD_GPIO_ID_RS485_DE, &de_cfg);

    /* RS485 UART: UARTE20 on P1.04 / P1.05 at 19200 baud (UMB) */
    rs485_uart_init();
    rs485_uart_rx_arm();   /* start listening immediately */

    /* External VBAT supply: enable at boot */
    static const gpio_out_cfg_t vbat_en_cfg = {
        .out_mode_cfg  = GPIO_OUT_MODE_PUSH_PULL,
        .level_default = GPIO_LEVEL_HIGH,   /* high = supply enabled */
    };
    Gpio_outputSetCfg(BOARD_GPIO_ID_VBAT_EXT_EN, &vbat_en_cfg);
    LOG(LVL_INFO, CGRN "VBAT_EXT enabled" C0);

    /* External VBAT fault: pull-up input, interrupt on falling edge (active-low) */
    static const gpio_in_cfg_t vbat_fault_cfg = {
        .event_cb    = vbat_fault_gpio_cb,
        .event_cfg   = GPIO_IN_EVENT_FALLING_EDGE,
        .in_mode_cfg = GPIO_IN_PULL_UP,
    };
    Gpio_inputSetCfg(BOARD_GPIO_ID_VBAT_EXT_nFAULT, &vbat_fault_cfg);

    /* AppConfig (CDD/shared_appconfig) — log all TLV types and raw data */
    static shared_app_config_filter_t s_appconfig_filter = {
        .type          = SHARED_APP_CONFIG_ALL_TYPE_FILTER,
        .cb            = appconfig_cb,
        .call_cb_always = false,
    };
    uint16_t filter_id;
    Shared_Appconfig_init();
    Shared_Appconfig_addFilter(&s_appconfig_filter, &filter_id);

    /* Wirepas: accept commands from gateway */
    Shared_Data_addDataReceivedCb(&m_downlink_filter);

    /* Poll task for RX forwarding (500 µs exec budget) */
    App_Scheduler_addTask_execTime(poll_task,
                                   APP_SCHEDULER_SCHEDULE_ASAP,
                                   500U);

    /* AEM10900 PMIC is initialised lazily by energy_monitor_task (with retry +
     * visible logging), so its init errors are not lost in the boot log burst. */

    /* Accelerometer (LIS2DW12) init via I2C1 */
    lis2dw_res_e lis_r = LIS2DW_init(&m_lis2dw_cfg);
    if (lis_r == LIS2DW_RES_WRONG_ID) {
        LOG(LVL_ERROR, CRED "LIS2DW WHO_AM_I mismatch (expected 0x44)" C0);
    } else if (lis_r != LIS2DW_RES_OK) {
        LOG(LVL_ERROR, CRED "LIS2DW init error %d" C0, (int)lis_r);
    } else {
        LOG(LVL_INFO, CGRN "LIS2DW OK (25 Hz LP2 ±2g)" C0);
    }

#if USE_ADS1220
    /* ADC (ADS1220) init via SPI3 */
    ads1220_res_e ads_r = ADS1220_init(BOARD_GPIO_ID_SPI_CS_ADS1220,
                                        BOARD_GPIO_ID_ADS1220_DRDY,
                                        &m_ads_cfg_ctn);
    if (ads_r != ADS1220_RES_OK) {
        LOG(LVL_ERROR, CRED "ADS1220 init error %d" C0, (int)ads_r);
    } else {
        LOG(LVL_INFO, CGRN "ADS1220 OK (AIN0 20 SPS int.ref)" C0);
        /* Read back registers to verify SPI write path.
         * Expected with current config: 80 00 00 00
         * If all FF: MISO stuck high.  If all 00: MISO stuck low. */
        ads1220_regs_t rb;
        if (ADS1220_read_regs(&rb) == ADS1220_RES_OK)
        {
            LOG(LVL_INFO, CCYN "ADS1220 regs: %02x %02x %02x %02x (exp 80 00 00 00)" C0,
                rb.reg[0], rb.reg[1], rb.reg[2], rb.reg[3]);
        }
        else
        {
            LOG(LVL_WARNING, CRED "ADS1220 reg readback failed" C0);
        }
    }
#endif /* USE_ADS1220 */

    /* Sensor read task: LIS2DW + ADS1220 every 2 s */
    App_Scheduler_addTask_execTime(sensor_read_task,
                                   APP_SCHEDULER_SCHEDULE_ASAP,
                                   SENSOR_READ_EXEC_US);

#if defined(USE_AEM10900)
    /* Periodic PMIC read + log (storage/source voltage, temperature, power). */
    App_Scheduler_addTask_execTime(energy_monitor_task,
                                   APP_SCHEDULER_SCHEDULE_ASAP,
                                   ENERGY_MONITOR_EXEC_US);
#endif

#if defined(USE_DAVIS6410)
    /* Davis wind: sample speed every WIND_SAMPLE_MS, uplink every WIND_REPORT_MS. */
    App_Scheduler_addTask_execTime(wind_task, WIND_SAMPLE_MS, WIND_EXEC_US);
#endif

    /* Diagnostic: periodic mesh route state (joined? cost? next hop?). */
    App_Scheduler_addTask_execTime(route_dbg_task, 3000U, 300U);

    /* UMB sensor polling: runs from the persisted/uploaded config (loaded above).
     * The task self-reschedules; it is (re)armed with the new cadence on config. */
    App_Scheduler_addTask_execTime(umb_poll_task, 2000U, UMB_EXEC_US);

    /* BLE beacon TX — Wirepas network info, always on (independent of the PMIC). */
    App_Scheduler_addTask_execTime(beacon_tx_task,
                                   APP_SCHEDULER_SCHEDULE_ASAP,
                                   BEACON_TX_EXEC_US);

    /* BLE beacon RX — callback registered before startStack */
    lib_beacon_rx->setBeaconReceivedCb(beacon_rx_cb);

    lib_state->startStack();   /* never returns */
}
