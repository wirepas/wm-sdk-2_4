NRF_FAMILY_HAL_PREFIX := $(MCU_PATH)$(MCU_FAMILY)/common/hal/
NRF54_HAL_PREFIX := $(MCU_PREFIX)hal/

# Add include path for nrf54 specific HAL header files
INCLUDES += -I$(NRF54_HAL_PREFIX)

# Build nrf54 specific HAL drivers
ifeq ($(HAL_UART), yes)
# Check version
ifeq ($(UART_USE_USB), yes)
        $(error Board $(target_board) doesn't support USB UART)
else
        SRCS += $(NRF54_HAL_PREFIX)usart_dma.c
endif
endif


ifeq ($(HAL_VOLTAGE), yes)
INCLUDES += -I$(NRF54_HAL_PREFIX)54l15/
INCLUDES += -I$(NRF_FAMILY_HAL_PREFIX)
SRCS += $(NRF_FAMILY_HAL_PREFIX)saadc_voltage.c

endif

SRCS += $(NRF54_HAL_PREFIX)power.c
