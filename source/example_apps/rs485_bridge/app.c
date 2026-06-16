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

#include "api.h"
#include "node_configuration.h"
#include "shared_data.h"
#include "app_scheduler.h"
#include "gpio.h"
#include "usart.h"

#include "board.h"

#define DEBUG_LOG_MODULE_NAME "RS485_BRIDGE"
#define DEBUG_LOG_MAX_LEVEL LVL_INFO
#include "debug_log.h"

/* ── Wirepas endpoints ──────────────────────────────────────────────────────── */
#define EP_RS485_DOWN   1   /* gateway → bridge → motor (commands) */
#define EP_RS485_UP     2   /* motor → bridge → gateway (replies)  */

/* ── Protocol constants ─────────────────────────────────────────────────────── */
#define FRAME_STX           0x02U
#define FRAME_END           0x03U
#define FRAME_MAX_LEN       20U     /* STX+ADDR+CMD+NBR+16data+END */

/* ── RS485 / UART ───────────────────────────────────────────────────────────── */
#define RS485_BAUD_RATE     115200U

/* Poll task period in ms. Reply timeout = RS485_REPLY_TIMEOUT_TICKS × period. */
#define POLL_PERIOD_MS              5U
#define RS485_REPLY_TIMEOUT_TICKS   (50U / POLL_PERIOD_MS)  /* 50 ms */

/* ── Internal RX parser ─────────────────────────────────────────────────────── */
typedef enum {
    RX_IDLE,
    RX_ADDR,
    RX_CMD,
    RX_NBR,
    RX_DATA,
    RX_END,
} rx_state_e;

static struct {
    rx_state_e  state;
    uint8_t     buf[FRAME_MAX_LEN];
    uint8_t     len;
    uint8_t     nbr_data;
    volatile bool frame_ready;
} m_rx;

/* Ticks remaining before giving up waiting for a motor reply */
static uint8_t m_reply_ticks_left = 0;

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
    Usart_sendBuffer(data, len);
    Usart_flush();          /* waits for DMA to finish shifting all bits out */
    de_rx();

    LOG(LVL_DEBUG, "RS485 TX %u bytes", len);
}

/* ── UART RX callback (ISR context) ─────────────────────────────────────────── */
static void uart_rx_cb(uint8_t * data, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        uint8_t b = data[i];
        switch (m_rx.state)
        {
            case RX_IDLE:
                if (b == FRAME_STX)
                {
                    m_rx.len = 0;
                    m_rx.buf[m_rx.len++] = b;
                    m_rx.state = RX_ADDR;
                }
                break;
            case RX_ADDR:
                m_rx.buf[m_rx.len++] = b;
                m_rx.state = RX_CMD;
                break;
            case RX_CMD:
                m_rx.buf[m_rx.len++] = b;
                m_rx.state = RX_NBR;
                break;
            case RX_NBR:
                m_rx.nbr_data = b;
                m_rx.buf[m_rx.len++] = b;
                m_rx.state = (b > 0U) ? RX_DATA : RX_END;
                break;
            case RX_DATA:
                if (m_rx.len < FRAME_MAX_LEN - 1U)
                    m_rx.buf[m_rx.len++] = b;
                if (m_rx.len >= 4U + m_rx.nbr_data)
                    m_rx.state = RX_END;
                break;
            case RX_END:
                m_rx.buf[m_rx.len++] = b;
                if (b == FRAME_END)
                    m_rx.frame_ready = true;
                m_rx.state = RX_IDLE;
                break;
        }
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
    LOG(LVL_INFO, "Wirepas UP %u bytes", len);
}

/* ── Poll task: forward completed frames, handle reply timeout ───────────────── */
static uint32_t poll_task(void)
{
    if (m_rx.frame_ready)
    {
        m_rx.frame_ready    = false;
        m_reply_ticks_left  = 0;
        forward_to_sink(m_rx.buf, m_rx.len);
    }
    else if (m_reply_ticks_left > 0U)
    {
        m_reply_ticks_left--;
        if (m_reply_ticks_left == 0U)
            LOG(LVL_WARNING, "RS485 reply timeout");
    }

    return POLL_PERIOD_MS;
}

/* ── Wirepas downlink: receive command from gateway, put it on RS485 bus ─────── */
static app_lib_data_receive_res_e downlink_cb(
    const shared_data_item_t * item,
    const app_lib_data_received_t * data)
{
    (void)item;

    if (data->num_bytes == 0U || data->num_bytes > FRAME_MAX_LEN)
        return APP_LIB_DATA_RECEIVE_RES_NOT_FOR_APP;

    LOG(LVL_INFO, "Wirepas DOWN → RS485 (%u B)", data->num_bytes);

    m_reply_ticks_left = RS485_REPLY_TIMEOUT_TICKS;
    rs485_send(data->bytes, (uint8_t)data->num_bytes);

    return APP_LIB_DATA_RECEIVE_RES_HANDLED;
}

static shared_data_item_t m_downlink_filter =
{
    .cb = downlink_cb,
    .filter = {
        .mode          = SHARED_DATA_NET_MODE_ALL,
        .src_endpoint  = EP_RS485_DOWN,
        .dest_endpoint = EP_RS485_DOWN,
        .multicast_cb  = NULL,
    }
};

/* ── App entry point ─────────────────────────────────────────────────────────── */
void App_init(const app_global_functions_t * functions)
{
    (void)functions;

    LOG_INIT();
    LOG(LVL_INFO, "RS485 bridge v1.0");

    if (configureNodeFromBuildParameters() != APP_RES_OK)
        return;

    /* RS485 DE: push-pull output, initially LOW (receive mode) */
    gpio_out_cfg_t de_cfg = {
        .out_mode_cfg  = GPIO_OUT_MODE_PUSH_PULL,
        .level_default = GPIO_LEVEL_LOW,
    };
    Gpio_outputSetCfg(BOARD_GPIO_ID_RS485_DE, &de_cfg);

    /* UART — no HW flow control (RS485 is half-duplex via DE) */
    Usart_init(RS485_BAUD_RATE, UART_FLOW_CONTROL_NONE);
    Usart_enableReceiver(uart_rx_cb);
    Usart_setEnabled(true);
    Usart_receiverOn();

    /* Wirepas: accept commands from gateway */
    Shared_Data_addDataReceivedCb(&m_downlink_filter);

    /* Poll task for RX forwarding (500 µs exec budget) */
    App_Scheduler_addTask_execTime(poll_task,
                                   APP_SCHEDULER_SCHEDULE_ASAP,
                                   500U);

    lib_state->startStack();
}
