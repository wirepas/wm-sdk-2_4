# Mcu of the board
MCU_FAMILY=nrf
MCU=nrf54
MCU_SUB=l
MCU_MEM_VAR=15

# This board "pca10156" has nRF54L15 as MCU.
# Sometimes there is need to test nRF54L05 or nRF54L10.
# Unfortunately (at least at the time of writing this)
# there is no evaluation board available from Nordic for
# nRF54L05 or nRF54L10. By defining build option
# MCU_MEM_VAR=05 or MCU_MEM_VAR=10 it is possible to
# have application memory map for L05 and L10 versions.
# Wirepas Mesh stack will detect the used part from
# HW at run time. That has undesired effect e.g. for
# current consumption measurements: Wirepas Mesh stack
# will keep all RAM sections retained as the chip is
# nRF54L15, although application is implemented for
# nRF54L05 or nRF54L10. With BOARD_HW_FORCE_PART
# it is possible to tell bootloader to tell Wirepas Mesh
# stack that the part information found from HW should
# be overridden. That leads to bit better accuracy
# in current consumption measurements when nRF54L15 HW
# is used instead of real nRF54L05 or nRF54L10 HW.
ifneq ("$(MCU_MEM_VAR)", "15")
    BOARD_HW_FORCE_PART=$(MCU_MEM_VAR)
endif

radio=nrf54l

# Hardware capabilities of the board
## Is 32kHz crystal mounted on the board.
board_hw_crystal_32k=yes

## Is DCDC used on this board.
board_hw_dcdc=yes

## Desired capacitor value in fF (femtofarads) for HFXO internal capacitors.
## The value must be in range [4000, 17000] and adjusted in steps of 250 fF.
## If the value is zero, internal capacitors are disabled.
board_hw_hfxo_int_cap_ff=15000

## Desired capacitor value in fF (femtofarads) for LFXO internal capacitors.
## The value must be in range [4000, 18000] and adjusted in steps of 500 fF.
## If the value is zero, internal capacitors are disabled.
board_hw_lfxo_int_cap_ff=17000

# Set path to custom power table here, if set, the custom power
# table will be set during application startup, and the stack will use that
# instead of the default.
# For convenience, an example of a +8 dBm power table is provided, uncomment the
# line below or use radio_power_table=8 setting as make argument to use that
# instead of the default +7 dBm power table.
#RADIO_CUSTOM_POWER_TABLE=mcu/nrf/nrf54/hal/radio/radio_power_table_nrf54l15_8dBm.h
ifneq ("$(radio_power_table)", "")
    RADIO_CUSTOM_POWER_TABLE=mcu/nrf/nrf54/hal/radio/radio_power_table_nrf54l15_$(radio_power_table)dBm.h
endif

