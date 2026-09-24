/* OTT HydroMet / Lufft UMB binary protocol (v1.0) — codec.
 *
 * Transport-agnostic: build a request into a buffer, send it over any RS485
 * link, then parse the response buffer. Physical layer is typically 19200 8N1.
 *
 * Frame:  SOH ver <to>2 <from>2 len STX <cmd> <verc> <payload> ETX <crc>2 EOT
 * CRC:    CRC16-MCRF4XX (poly 0x8408, init 0xFFFF), over SOH..ETX, little-endian.
 * Words:  little-endian (Intel). Addresses = (classID<<12) | deviceID.
 */
#ifndef UMB_H_
#define UMB_H_

#include <stdint.h>
#include <stddef.h>

/* Address helpers. Controller (this node) uses class 15 ("controller"). */
#define UMB_ADDR(cls, dev)   ((uint16_t)(((uint16_t)((cls) & 0x0Fu) << 12) | ((dev) & 0xFFu)))
#define UMB_CONTROLLER_ADDR  UMB_ADDR(15u, 1u)   /* 0xF001 */

/* Smallest frame that still carries a float value = 22 bytes; give margin. */
#define UMB_MAX_FRAME        64u

typedef enum {
    UMB_OK = 0,
    UMB_ERR_ARG,       /* bad argument / buffer too small          */
    UMB_ERR_FRAME,     /* bad SOH/STX/ETX/EOT/len/ver              */
    UMB_ERR_CRC,       /* CRC mismatch                             */
    UMB_ERR_CMD,       /* response cmd does not match request      */
    UMB_ERR_STATUS,    /* device returned a non-zero status byte   */
    UMB_ERR_TYPE,      /* unknown measurement data type            */
} umb_res_e;

typedef struct {
    uint8_t  status;   /* device status byte (0 = OK)              */
    uint16_t channel;  /* channel number                           */
    uint8_t  type;     /* UMB data type (0x10..0x17)               */
    float    value;    /* decoded measurement value                */
} umb_value_t;

/* Build an "Online Data Request" (23h) for one channel. Returns the frame
 * length in bytes written to buf (16), or 0 on error. */
uint8_t umb_build_online_request(uint8_t * buf, uint8_t bufsize,
                                 uint16_t to, uint16_t from, uint16_t channel);

/* Parse an Online Data Request response frame of `len` bytes. On UMB_OK the
 * decoded value is in *out; UMB_ERR_STATUS still fills out->status/channel. */
umb_res_e umb_parse_online_response(const uint8_t * buf, uint8_t len,
                                    umb_value_t * out);

/* Build a Multi-Channel Online Data Request (2Fh) for up to `count` channels in
 * one frame. Returns the frame length, or 0 on error (buffer too small). */
uint8_t umb_build_multi_request(uint8_t * buf, uint8_t bufsize,
                                uint16_t to, uint16_t from,
                                const uint16_t * channels, uint8_t count);

/* Parse a Multi-Channel (2Fh) response into out[0..max-1]. Returns the number of
 * sub-telegrams decoded (each out[i].status carries the per-channel status; a
 * non-zero status leaves out[i].type/value at 0). Returns 0 on frame/CRC error. */
uint8_t umb_parse_multi_response(const uint8_t * buf, uint8_t len,
                                 umb_value_t * out, uint8_t max);

/* CRC16-MCRF4XX over n bytes (init 0xFFFF). Exposed for tests / multi-frame. */
uint16_t umb_crc16(const uint8_t * p, uint16_t n);

#endif /* UMB_H_ */
