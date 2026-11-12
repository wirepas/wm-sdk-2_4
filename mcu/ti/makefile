MCU_PREFIX    := $(MCU_PATH)$(MCU_FAMILY)/$(MCU)
MCU_CONFIG    := $(MCU_PREFIX)/config.mk
MCU_COMMON    := $(MCU_PATH)$(MCU_FAMILY)/common

# Include common MCU implementation
-include $(MCU_COMMON)/makefile

# Include MCU specific configuration
-include $(MCU_CONFIG)

# Include MCU specific HAL drivers
-include $(MCU_PREFIX)/hal/makefile
