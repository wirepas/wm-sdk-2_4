# Copyright 2026 Wirepas Ltd. All Rights Reserved.
#
# See file LICENSE.txt for full license details.

#
# Legacy Single-Instance UART Drivers Makefile
#

# Define directory
LEGACY_UART_DIR := $(EFR32_HAL_PREFIX)usart/legacy

ifeq ($(UART_USE_DMA), yes)
    SRCS += $(LEGACY_UART_DIR)/usart_dma.c \
            $(EFR32_VENDOR_PREFIX)em_ldma.c \
            $(EFR32_VENDOR_PREFIX)em_usart.c

else # not DMA
    ifeq ($(MCU_SUB), xg23)
        SRCS += $(LEGACY_UART_DIR)/eusart.c
        SRCS += $(EFR32_VENDOR_PREFIX)em_eusart.c

    else # not xG23
        SRCS += $(LEGACY_UART_DIR)/usart.c
        SRCS += $(EFR32_VENDOR_PREFIX)em_usart.c

    endif 

endif 

SRCS += $(EFR32_VENDOR_PREFIX)$(HAL_SYSTEM_C)
