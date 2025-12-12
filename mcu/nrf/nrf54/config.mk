# Mcu instruction set
ARCH = armv8-m.main
CFLAGS += -mfloat-abi=hard -mfpu=fpv5-sp-d16

# Libraries to be build for Cortex-M33
CM33 := yes
CFLAGS += -DARM_MATH_ARMV8MML

# This mcu has a bootloader (enough memory)
HAS_BOOTLOADER=yes
# This MCU is supported starting from bootloader version 11
MIN_BOOTLOADER_VERSION := 11

LDFLAGS += -L$(MCU_PATH)nrf/nrf54/linker
CFLAGS += -DNRF54_PLATFORM
ifeq ($(MCU_SUB), l)
    # Hardware magic used for this architecture
    HW_MAGIC=13
    ifeq ($(MCU_MEM_VAR), 15)
        HW_VARIANT_ID=20
        CFLAGS += -DNRF54L15_XXAA -DNRF_APPLICATION
    else ifeq ($(MCU_MEM_VAR), 10)
        HW_VARIANT_ID=21
        # TODO: Change to NRF54L10 when available
        CFLAGS += -DNRF54L15_XXAA -DNRF_APPLICATION
    else ifeq ($(MCU_MEM_VAR), 05)
        HW_VARIANT_ID=22
        # TODO: Change to NRF54L05 when available
        CFLAGS += -DNRF54L15_XXAA -DNRF_APPLICATION
    else
         $(error "Invalid memory variant $(MCU)$(MCU_SUB)$(MCU_MEM_VAR)!")
    endif
else
    $(error "Invalid MCU_SUB for nrf54! For $(MCU_SUB) only l is supported.")
endif

# Base address of non-volatile memory (RRAM)
FLASH_BASE_ADDR=0x00000000

# Different bootloader sizes available: size=mem_variant_byte,...
BOOTLOADER_SIZES="32k=0x01"
