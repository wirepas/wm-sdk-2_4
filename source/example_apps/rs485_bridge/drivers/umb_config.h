/* UMB polling configuration — multi-device. Set at runtime by a dedicated
 * downlink command on EP_UMB_CFG, applied live (and persisted when it fits).
 *
 * Up to UMB_CFG_MAX_DEVS UMB sensors can be polled, each with its own address
 * and channel list — e.g. WS501 (0x7001) and WS100 (0x7002), both class 7.
 *
 * WIRE format (downlink payload / NFC), little-endian, versioned:
 *   byte 0      : wire version (= UMB_CFG_WIRE_VERSION)
 *   byte 1      : flags        (bit0 = enabled)
 *   byte 2..3   : poll_period_s (u16)  seconds between polling rounds
 *   byte 4      : num_devs      (0..UMB_CFG_MAX_DEVS)
 *   then per device:
 *     +0..1     : dev_addr      (u16)  class<<12 | device
 *     +2        : num_channels  (0..UMB_CFG_MAX_CH_PER_DEV)
 *     +3..      : channel[num]  (u16 each)
 */
#ifndef UMB_CONFIG_H_
#define UMB_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>

#define UMB_CFG_MAX_DEVS         2u
#define UMB_CFG_MAX_CH_PER_DEV   8u
#define UMB_CFG_WIRE_VERSION     2u
#define UMB_CFG_WIRE_MIN         5u   /* header with 0 devices */
/* Largest possible wire config: header + per-device (addr+count) + channels. */
#define UMB_CFG_WIRE_MAX \
    (UMB_CFG_WIRE_MIN + UMB_CFG_MAX_DEVS * (3u + 2u * UMB_CFG_MAX_CH_PER_DEV))

typedef struct {
    uint16_t dev_addr;       /* UMB sensor address (class<<12 | device) */
    uint8_t  num_channels;
    uint16_t channel[UMB_CFG_MAX_CH_PER_DEV];
} umb_dev_cfg_t;

typedef struct {
    bool          enabled;
    uint16_t      poll_period_s;   /* polling-round cadence (seconds) */
    uint8_t       num_devs;
    umb_dev_cfg_t dev[UMB_CFG_MAX_DEVS];
} umb_config_t;

/* Fill c with compile-time defaults (disabled, no devices). */
void umb_config_defaults(umb_config_t * c);

/* Parse the wire format into *out. Returns false on version/length/range error
 * (leaves *out untouched). */
bool umb_config_parse(const uint8_t * buf, uint8_t len, umb_config_t * out);

/* Serialize c into buf. Returns bytes written, 0 on error (buffer too small). */
uint8_t umb_config_serialize(const umb_config_t * c, uint8_t * buf, uint8_t bufsize);

#endif /* UMB_CONFIG_H_ */
