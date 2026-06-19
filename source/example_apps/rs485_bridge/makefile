SRCS += source/example_apps/rs485_bridge/drivers/nfc_hw_nrf54l15.c \
        source/unitary_apps/nfc/lib/t2_emulation.c \
        source/unitary_apps/nfc/lib/tlv.c \
        source/unitary_apps/nfc/lib/ndef.c \
        source/example_apps/rs485_bridge/drivers/aem10900.c \
        source/example_apps/rs485_bridge/drivers/lis2dw.c \
        source/example_apps/rs485_bridge/drivers/ads1220.c \
        source/example_apps/rs485_bridge/drivers/rs485_uart.c

INCLUDES += -Isource/example_apps/rs485_bridge/drivers
# NFC lib path goes in CFLAGS so it comes after util/ in the search order,
# preventing nfc/lib/tlv.h from shadowing util/tlv.h (used by shared_appconfig).
CFLAGS += -Isource/unitary_apps/nfc/lib
LIBS +=
LDFLAGS +=
LDLIBS += -lm

# Wirepas HAL drivers
HAL_GPIO=yes
HAL_I2C=yes
HAL_SPI=yes

BEACON_TX=yes

# Wirepas libraries used
SHARED_DATA=yes
APP_SCHEDULER=yes
APP_SCHEDULER_TASKS=5
SHARED_APP_CONFIG=yes
SHARED_APP_CONFIG_FILTERS=1

APP_PRINTING=yes
# Force UARTE30 (LP domain) instead of UARTE20: P0.01/P0.00 are LP-domain pins
# and cannot be used with UARTE20 (peripheral domain). This override pre-includes
# board/sensorv26/usart_dma_series.h before the MCU version is found.
CFLAGS += -include board/sensorv26/usart_dma_series.h

# Node configuration from build parameters (network address, channel, etc.)
# Override on make command line:  make ... node_address=0x...
