/* Copyright 2019 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

#include "uart_print.h"

// Storage for debug messages.
#define UART_PRINT_BUF_SIZE     256ul
static uint8_t          m_buffer[UART_PRINT_BUF_SIZE];

#include "hal_api.h"

static ssize_t _write(char * ptr, size_t len)
{
    // Check that size is not too large for the ring buffer in usart-driver
    if(len > Usart_getMTUSize())
    {
        return 0;
    }
    // Write data to the usart port and return number of bytes written
    return Usart_sendBuffer(ptr, len);
}

void UartPrint_init(uint32_t baudrate)
{
    HAL_Open();
    Usart_init(baudrate, UART_FLOW_CONTROL_NONE);
    Usart_setEnabled(true);
}

int UartPrint_printf(const char * fmt, ...)
{
    int len;
    va_list args;

    va_start(args, fmt);

    len = vsnprintf((char *)&m_buffer[0], UART_PRINT_BUF_SIZE, fmt, args);
    // Try sending
    len = _write((char *)m_buffer, len);

    va_end(args);

    return len;
}
