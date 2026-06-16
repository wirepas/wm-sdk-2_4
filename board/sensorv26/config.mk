# Carte master Bienesis – nRF54L15
MCU_FAMILY=nrf
MCU=nrf54
MCU_SUB=l
MCU_MEM_VAR=15

radio=nrf54l

board_hw_crystal_32k=yes
board_hw_dcdc=yes

# Adjust to match actual crystal load capacitors on PCB
board_hw_hfxo_int_cap_ff=15000
board_hw_lfxo_int_cap_ff=17000
