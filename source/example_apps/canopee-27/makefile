SRCS += source/example_apps/canopee-27/drivers/nfc_hw_nrf54l15.c \
        source/unitary_apps/nfc/lib/t2_emulation.c \
        source/unitary_apps/nfc/lib/tlv.c \
        source/unitary_apps/nfc/lib/ndef.c \
        source/example_apps/canopee-27/drivers/lis2dw.c \
        source/example_apps/canopee-27/drivers/max17261.c \
        source/example_apps/canopee-27/drivers/rs485_uart.c \
        source/example_apps/canopee-27/drivers/i2c_bitbang.c

INCLUDES += -Isource/example_apps/canopee-27/drivers
# nrf_gpio HAL (used by the bit-bang I2C for nRF54L-correct pad config)
INCLUDES += -Imcu/nrf/common/vendor/hal
# NFC lib path goes in CFLAGS so it comes after util/ in the search order,
# preventing nfc/lib/tlv.h from shadowing util/tlv.h (used by shared_appconfig).
CFLAGS += -Isource/unitary_apps/nfc/lib
LIBS +=
LDFLAGS +=
LDLIBS += -lm

# Wirepas HAL drivers
HAL_GPIO=yes
# I2C is bit-banged (drivers/i2c_bitbang.c) because RS485 owns the single
# LP-domain serial (SERIAL30/UARTE30) — the hardware TWIM30 shares it. So the
# hardware I2C HAL must NOT be compiled (it would duplicate I2C_init/transfer).
HAL_I2C=no

BEACON_TX=yes

# CBOR encoder (util/tinycbor) — used for the sensor uplinks
TINY_CBOR=yes

# Wirepas libraries used
SHARED_DATA=yes
APP_SCHEDULER=yes
APP_SCHEDULER_TASKS=8
SHARED_APP_CONFIG=yes
SHARED_APP_CONFIG_FILTERS=1

APP_PRINTING=yes
# Console on P1.08 (TX) / P1.07 (RX) → UARTE20 (peripheral domain). Pre-include
# the canopee_v27 series header so the Wirepas debug HAL uses UARTE20.
CFLAGS += -include board/canopee_v27/usart_dma_series.h

# Node configuration from build parameters (network address, channel, etc.)
# Override on make command line:  make ... node_address=0x...
