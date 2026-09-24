/* UMB polling configuration wire codec (multi-device) — see umb_config.h */

#include <stddef.h>
#include "umb_config.h"

#define UMB_CFG_FLAG_ENABLED  0x01u

void umb_config_defaults(umb_config_t * c)
{
    if (c == NULL) return;
    c->enabled       = false;
    c->poll_period_s = 60u;
    c->num_devs      = 0u;
    for (uint8_t d = 0; d < UMB_CFG_MAX_DEVS; d++)
    {
        c->dev[d].dev_addr     = 0u;
        c->dev[d].num_channels = 0u;
        for (uint8_t i = 0; i < UMB_CFG_MAX_CH_PER_DEV; i++)
            c->dev[d].channel[i] = 0u;
    }
}

bool umb_config_parse(const uint8_t * buf, uint8_t len, umb_config_t * out)
{
    if (buf == NULL || out == NULL)      return false;
    if (len < UMB_CFG_WIRE_MIN)          return false;
    if (buf[0] != UMB_CFG_WIRE_VERSION)  return false;

    uint8_t ndev = buf[4];
    if (ndev > UMB_CFG_MAX_DEVS)         return false;

    umb_config_t tmp;
    umb_config_defaults(&tmp);
    tmp.enabled       = (buf[1] & UMB_CFG_FLAG_ENABLED) != 0u;
    tmp.poll_period_s = (uint16_t)(buf[2] | ((uint16_t)buf[3] << 8));
    tmp.num_devs      = ndev;

    uint8_t pos = UMB_CFG_WIRE_MIN;
    for (uint8_t d = 0; d < ndev; d++)
    {
        if ((uint8_t)(pos + 3u) > len)   return false;   /* addr + num_ch */
        tmp.dev[d].dev_addr = (uint16_t)(buf[pos] | ((uint16_t)buf[pos + 1u] << 8));
        uint8_t nch = buf[pos + 2u];
        if (nch > UMB_CFG_MAX_CH_PER_DEV) return false;
        pos = (uint8_t)(pos + 3u);
        if ((uint8_t)(pos + 2u * nch) > len) return false;
        tmp.dev[d].num_channels = nch;
        for (uint8_t i = 0; i < nch; i++)
        {
            tmp.dev[d].channel[i] =
                (uint16_t)(buf[pos] | ((uint16_t)buf[pos + 1u] << 8));
            pos = (uint8_t)(pos + 2u);
        }
    }

    /* Range guard: a 0 s period would busy-loop the scheduler. */
    if (tmp.enabled && tmp.poll_period_s == 0u) return false;

    *out = tmp;
    return true;
}

uint8_t umb_config_serialize(const umb_config_t * c, uint8_t * buf, uint8_t bufsize)
{
    if (c == NULL || buf == NULL || bufsize < UMB_CFG_WIRE_MIN) return 0u;

    uint8_t ndev = (c->num_devs > UMB_CFG_MAX_DEVS) ? UMB_CFG_MAX_DEVS : c->num_devs;
    uint8_t pos  = UMB_CFG_WIRE_MIN;

    buf[0] = UMB_CFG_WIRE_VERSION;
    buf[1] = c->enabled ? UMB_CFG_FLAG_ENABLED : 0u;
    buf[2] = (uint8_t)(c->poll_period_s & 0xFFu);
    buf[3] = (uint8_t)(c->poll_period_s >> 8);
    buf[4] = ndev;

    for (uint8_t d = 0; d < ndev; d++)
    {
        uint8_t nch = (c->dev[d].num_channels > UMB_CFG_MAX_CH_PER_DEV)
                          ? UMB_CFG_MAX_CH_PER_DEV : c->dev[d].num_channels;
        if ((uint8_t)(pos + 3u + 2u * nch) > bufsize) return 0u;
        buf[pos++] = (uint8_t)(c->dev[d].dev_addr & 0xFFu);
        buf[pos++] = (uint8_t)(c->dev[d].dev_addr >> 8);
        buf[pos++] = nch;
        for (uint8_t i = 0; i < nch; i++)
        {
            buf[pos++] = (uint8_t)(c->dev[d].channel[i] & 0xFFu);
            buf[pos++] = (uint8_t)(c->dev[d].channel[i] >> 8);
        }
    }
    return pos;
}
