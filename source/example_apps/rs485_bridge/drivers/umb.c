/* UMB binary protocol codec — see umb.h */

#include <string.h>
#include "umb.h"

#define UMB_SOH   0x01u
#define UMB_STX   0x02u
#define UMB_ETX   0x03u
#define UMB_EOT   0x04u
#define UMB_VER   0x10u   /* protocol version 1.0 */
#define UMB_VERC  0x10u   /* command version 1.0  */
#define UMB_CMD_ONLINE 0x23u
#define UMB_CMD_MULTI  0x2Fu

uint16_t umb_crc16(const uint8_t * p, uint16_t n)
{
    uint16_t crc = 0xFFFFu;
    while (n--)
    {
        uint8_t b = *p++;
        for (uint8_t i = 0; i < 8u; i++)
        {
            crc ^= (uint16_t)(b & 0x01u);
            crc = (crc & 0x01u) ? (uint16_t)((crc >> 1) ^ 0x8408u)
                                : (uint16_t)(crc >> 1);
            b >>= 1;
        }
    }
    return crc;
}

uint8_t umb_build_online_request(uint8_t * buf, uint8_t bufsize,
                                 uint16_t to, uint16_t from, uint16_t channel)
{
    if (buf == NULL || bufsize < 16u) return 0u;

    uint8_t i = 0;
    buf[i++] = UMB_SOH;
    buf[i++] = UMB_VER;
    buf[i++] = (uint8_t)(to & 0xFFu);
    buf[i++] = (uint8_t)(to >> 8);
    buf[i++] = (uint8_t)(from & 0xFFu);
    buf[i++] = (uint8_t)(from >> 8);
    buf[i++] = 0x04u;                 /* len = cmd + verc + channel(2) = 4 */
    buf[i++] = UMB_STX;
    buf[i++] = UMB_CMD_ONLINE;
    buf[i++] = UMB_VERC;
    buf[i++] = (uint8_t)(channel & 0xFFu);
    buf[i++] = (uint8_t)(channel >> 8);
    buf[i++] = UMB_ETX;

    uint16_t crc = umb_crc16(buf, i);  /* SOH..ETX inclusive */
    buf[i++] = (uint8_t)(crc & 0xFFu);
    buf[i++] = (uint8_t)(crc >> 8);
    buf[i++] = UMB_EOT;
    return i;                          /* 16 */
}

/* Expected value length (bytes) for a UMB data type, 0 if unknown. */
static uint8_t umb_type_len(uint8_t type)
{
    switch (type)
    {
        case 0x10: case 0x11: return 1u;  /* u/s char  */
        case 0x12: case 0x13: return 2u;  /* u/s short */
        case 0x14: case 0x15: return 4u;  /* u/s long  */
        case 0x16:            return 4u;  /* float     */
        case 0x17:            return 8u;  /* double    */
        default:              return 0u;
    }
}

static float umb_decode_value(uint8_t type, const uint8_t * v)
{
    switch (type)
    {
        case 0x10: return (float)(uint8_t)v[0];
        case 0x11: return (float)(int8_t)v[0];
        case 0x12: return (float)(uint16_t)(v[0] | ((uint16_t)v[1] << 8));
        case 0x13: return (float)(int16_t)(v[0] | ((uint16_t)v[1] << 8));
        case 0x14: {
            uint32_t u = (uint32_t)v[0] | ((uint32_t)v[1] << 8)
                       | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
            return (float)u;
        }
        case 0x15: {
            uint32_t u = (uint32_t)v[0] | ((uint32_t)v[1] << 8)
                       | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
            return (float)(int32_t)u;
        }
        case 0x16: { float f;  memcpy(&f, v, 4); return f; }
        case 0x17: { double d; memcpy(&d, v, 8); return (float)d; }
        default:   return 0.0f;
    }
}

umb_res_e umb_parse_online_response(const uint8_t * buf, uint8_t len,
                                    umb_value_t * out)
{
    if (buf == NULL || out == NULL)      return UMB_ERR_ARG;
    /* Minimum response (status only, no value) = 12 + len_field, len_field>=3. */
    if (len < 15u)                        return UMB_ERR_FRAME;
    if (buf[0] != UMB_SOH || buf[1] != UMB_VER || buf[7] != UMB_STX)
                                          return UMB_ERR_FRAME;

    uint8_t payload_len = buf[6];                 /* bytes between STX and ETX */
    uint8_t total       = (uint8_t)(12u + payload_len);
    if (total > len)                      return UMB_ERR_FRAME;
    if (buf[total - 1u] != UMB_EOT || buf[total - 4u] != UMB_ETX)
                                          return UMB_ERR_FRAME;

    uint16_t crc_calc = umb_crc16(buf, (uint16_t)(total - 3u));  /* SOH..ETX */
    uint16_t crc_rx   = (uint16_t)(buf[total - 3u] | ((uint16_t)buf[total - 2u] << 8));
    if (crc_calc != crc_rx)               return UMB_ERR_CRC;

    if (buf[8] != UMB_CMD_ONLINE)         return UMB_ERR_CMD;

    out->status  = buf[10];
    out->channel = (uint16_t)(buf[11] | ((uint16_t)buf[12] << 8));
    out->type    = 0u;
    out->value   = 0.0f;
    if (out->status != 0x00u)             return UMB_ERR_STATUS;

    /* payload after status = channel(2) + type(1) + value(n) → n = payload-6. */
    out->type = buf[13];
    uint8_t vlen = umb_type_len(out->type);
    if (vlen == 0u)                       return UMB_ERR_TYPE;
    if ((uint8_t)(payload_len) < (uint8_t)(6u + vlen)) return UMB_ERR_FRAME;

    out->value = umb_decode_value(out->type, &buf[14]);
    return UMB_OK;
}

uint8_t umb_build_multi_request(uint8_t * buf, uint8_t bufsize,
                                uint16_t to, uint16_t from,
                                const uint16_t * channels, uint8_t count)
{
    if (buf == NULL || channels == NULL || count == 0u) return 0u;
    /* payload = number(1) + channels(2*count); len = cmd+verc+payload. */
    uint8_t payload = (uint8_t)(1u + 2u * count);
    uint8_t total   = (uint8_t)(12u + 2u + payload);   /* header+STX(8)+len... */
    if (bufsize < total) return 0u;

    uint8_t i = 0;
    buf[i++] = UMB_SOH;
    buf[i++] = UMB_VER;
    buf[i++] = (uint8_t)(to & 0xFFu);
    buf[i++] = (uint8_t)(to >> 8);
    buf[i++] = (uint8_t)(from & 0xFFu);
    buf[i++] = (uint8_t)(from >> 8);
    buf[i++] = (uint8_t)(2u + payload);                /* len = cmd+verc+payload */
    buf[i++] = UMB_STX;
    buf[i++] = UMB_CMD_MULTI;
    buf[i++] = UMB_VERC;
    buf[i++] = count;
    for (uint8_t c = 0; c < count; c++)
    {
        buf[i++] = (uint8_t)(channels[c] & 0xFFu);
        buf[i++] = (uint8_t)(channels[c] >> 8);
    }
    buf[i++] = UMB_ETX;
    uint16_t crc = umb_crc16(buf, i);
    buf[i++] = (uint8_t)(crc & 0xFFu);
    buf[i++] = (uint8_t)(crc >> 8);
    buf[i++] = UMB_EOT;
    return i;
}

uint8_t umb_parse_multi_response(const uint8_t * buf, uint8_t len,
                                 umb_value_t * out, uint8_t max)
{
    if (buf == NULL || out == NULL)      return 0u;
    if (len < 15u)                       return 0u;
    if (buf[0] != UMB_SOH || buf[1] != UMB_VER || buf[7] != UMB_STX) return 0u;

    uint8_t payload_len = buf[6];
    uint8_t total       = (uint8_t)(12u + payload_len);
    if (total > len)                     return 0u;
    if (buf[total - 1u] != UMB_EOT || buf[total - 4u] != UMB_ETX)    return 0u;

    uint16_t crc_calc = umb_crc16(buf, (uint16_t)(total - 3u));
    uint16_t crc_rx   = (uint16_t)(buf[total - 3u] | ((uint16_t)buf[total - 2u] << 8));
    if (crc_calc != crc_rx)              return 0u;
    if (buf[8] != UMB_CMD_MULTI)         return 0u;

    /* buf[10] = top-level status, buf[11] = number of sub-telegrams. */
    uint8_t number = buf[11];
    uint8_t pos    = 12u;                 /* first sub-telegram: <sub-len> ...   */
    uint8_t etx    = (uint8_t)(total - 4u);
    uint8_t n_out  = 0u;

    for (uint8_t s = 0; s < number && n_out < max; s++)
    {
        if (pos >= etx)                  break;
        uint8_t sub_len = buf[pos++];     /* bytes that follow for this channel  */
        if ((uint8_t)(pos + sub_len) > etx) break;

        umb_value_t * o = &out[n_out++];
        o->status  = buf[pos];
        o->channel = (uint16_t)(buf[pos + 1u] | ((uint16_t)buf[pos + 2u] << 8));
        o->type    = 0u;
        o->value   = 0.0f;
        if (o->status == 0x00u && sub_len >= 4u)
        {
            o->type = buf[pos + 3u];
            if (umb_type_len(o->type) != 0u)
                o->value = umb_decode_value(o->type, &buf[pos + 4u]);
        }
        pos = (uint8_t)(pos + sub_len);
    }
    return n_out;
}
