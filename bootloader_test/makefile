$(eval key_type=$(shell $(BOOT_CONF) --in_file $(BOOTLOADER_CONFIG_INI) --get_key_type))
$(info $********************************** )
$(info $ )
$(info $$key_type is [${key_type}])
$(info $ )
$(info $********************************** )

ifeq ("${key_type}", "OMAC1_AES128CTR")
CFLAGS += -DBOOT_TEST_OMAC1_AES128CTR

    $(info $$key_type is [${key_type}])
endif

SRCS += bootloader_test/tests/test_memory_areas.c
SRCS += bootloader_test/tests/test_info.c
SRCS += bootloader_test/tests/test_timings.c
SRCS += bootloader_test/main.c
SRCS += bootloader_test/print/print.c
SRCS += bootloader_test/print/syscalls.c
SRCS += bootloader_test/timing/timing.c

ifeq ($(EFR32_SERIES),2)
    SRCS += mcu/efr/common/vendor/series$(EFR32_SERIES)/em_usart.c
endif

ifeq ($(MCU)$(MCU_SUB),efr32xg12)
    SRCS += mcu/efr/common/vendor/series$(EFR32_SERIES)/efr32fg12/Source/system_efr32fg12p.c
else ifeq ($(MCU)$(MCU_SUB),efr32xg13)
    SRCS += mcu/efr/common/vendor/series$(EFR32_SERIES)/efr32fg13/Source/system_efr32fg13p.c
else ifeq ($(MCU)$(MCU_SUB),efr32xg21)
    SRCS += mcu/efr/common/vendor/series$(EFR32_SERIES)/efr32mg21/Source/system_efr32mg21.c
    SRCS += mcu/efr/common/vendor/series$(EFR32_SERIES)/em_system.c
else ifeq ($(MCU)$(MCU_SUB),efr32xg22)
    SRCS += mcu/efr/common/vendor/series$(EFR32_SERIES)/efr32mg22/Source/system_efr32mg22.c
else ifeq ($(MCU)$(MCU_SUB),efr32xg23)
    SRCS += mcu/efr/common/vendor/series$(EFR32_SERIES)/efr32fg23/Source/system_efr32fg23.c
else ifeq ($(MCU)$(MCU_SUB),efr32xg24)
    SRCS += mcu/efr/common/vendor/series$(EFR32_SERIES)/efr32mg24/Source/system_efr32mg24.c
else ifeq ($(MCU),nrf52)
    # On nRF52832 TXD.MAXCNT is 8 bits so use that as the maximum for the nRF52 series
    CFLAGS += -DNRF_UARTE0_TX_MAXCNT_MAX=0xFF
else ifeq ($(MCU),nrf54)
    CFLAGS += -DNRF_UARTE0_TX_MAXCNT_MAX=UARTE_DMA_TX_MAXCNT_MAXCNT_Max
else ifeq ($(MCU),nrf91)
    # In the nRF91 series TXD.MAXCNT is 13 bits
    CFLAGS += -DNRF_UARTE0_TX_MAXCNT_MAX=0x1FFF
else
    $(error "Invalid MCU configuration $(MCU)$(MCU_SUB)$(MCU_MEM_VAR)!")
endif

CFLAGS += -Ibootloader_test/api/
CFLAGS += -Ibootloader_test/print/
CFLAGS += -Ibootloader_test/timing/
CFLAGS += -Ibootloader_test/drivers/
CFLAGS += -Ibootloader_test/tests/
CFLAGS += -Ibootloader_test/

ifeq ($(MCU_FAMILY),efr)
    CFLAGS += -Imcu/efr/$(MCU)/hal/usart/series$(EFR32_SERIES)/
    SRCS += bootloader_test/drivers/$(MCU_FAMILY)/usart.c
else ifeq($(MCU_FAMILY),nrf)
    SRCS += bootloader_test/drivers/$(MCU_FAMILY)/usart_dma.c
endif
