/* Copyright 2025 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include "hal_api.h"

bool Usart_init(uint32_t baudrate, uart_flow_control_e flow_control)
{
    (void) baudrate;
    (void) flow_control;

    return false;
}


void Usart_setEnabled(bool enabled)
{
    (void) enabled;
}

void Usart_receiverOn(void)
{
}

void Usart_receiverOff(void)
{
}

bool Usart_setFlowControl(uart_flow_control_e flow)
{
    (void) flow;

    return false;
}

uint32_t Usart_sendBuffer(const void * buf, uint32_t len)
{
    (void) buf;
    (void) len;

    return 0;
}

void Usart_enableReceiver(serial_rx_callback_f callback)
{
    (void) callback;
}

uint32_t Usart_getMTUSize(void)
{
    return 0;
}

void Usart_flush(void)
{
}
