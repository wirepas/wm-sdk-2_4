# On-board sensor drivers
SRCS += source/example_apps/nrf54L15_tag/drivers/bme688.c \
        source/example_apps/nrf54L15_tag/drivers/bme68x_api/bme68x.c \
        source/example_apps/nrf54L15_tag/drivers/adxl367.c \
        source/example_apps/nrf54L15_tag/drivers/bmi270.c

# NFC + AEM10900 PMIC (shared with rs485_bridge)
SRCS += source/example_apps/rs485_bridge/drivers/nfc_hw_nrf54l15.c \
        source/unitary_apps/nfc/lib/t2_emulation.c \
        source/unitary_apps/nfc/lib/tlv.c \
        source/unitary_apps/nfc/lib/ndef.c \
        source/example_apps/rs485_bridge/drivers/aem10900.c

INCLUDES += -Isource/example_apps/nrf54L15_tag/drivers \
            -Isource/example_apps/nrf54L15_tag/drivers/bme68x_api \
            -Isource/example_apps/rs485_bridge/drivers
# NFC lib path after util/ to avoid nfc/lib/tlv.h shadowing util/tlv.h
CFLAGS += -Isource/unitary_apps/nfc/lib

LIBS +=
LDFLAGS +=
LDLIBS += -lm

# Wirepas HAL drivers
HAL_GPIO=yes
HAL_I2C=yes
HAL_SPI=yes

# Wirepas libraries used
APP_SCHEDULER=yes
APP_SCHEDULER_TASKS=8
SHARED_DATA=yes

BEACON_TX=yes

# CBOR encoder (util/tinycbor)
TINY_CBOR=yes

# NOTE: the nrf54l15_tag board exposes no console UART (P0.00/P0.01 are buttons),
# so APP_PRINTING is left off. Enable it only after assigning TX/RX pins.

#
# App setup and its dependencies for setting and managing encryption keys
#
APP_PERSISTENT=yes
APP_SETUP=yes
PROVISIONING=yes
