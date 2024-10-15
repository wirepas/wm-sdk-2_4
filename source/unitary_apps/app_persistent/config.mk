# Boards compatible with this app 
TARGET_BOARDS := bgm220-ek4314a efr32_template mdbt50q_rx nrf52832_mdk_v2 nrf52_template pan1780 pca10040 pca10056 pca10059 pca10090_nrf52840 pca10100 pca10112 promistel_rpi_hat ruuvitag silabs_brd2601b silabs_brd2703a silabs_brd4180b silabs_brd4184a silabs_brd4187c silabs_brd4253a silabs_brd4254a silabs_brd4312a tbsense2 ublox_b204 wuerth_261101102 
#
# Network default settings configuration
#

# If this section is removed, node has to be configured in
# a different way
default_network_address ?= 0x12A205
default_network_channel ?= 17
# !HIGHLY RECOMMENDED! : To enable security keys please un-comment the lines below and fill with a
#                        randomly generated authentication & encryption keys (exactly 16 bytes)
#default_network_cipher_key ?= 0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??
#default_network_authen_key ?= 0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??

#
# App specific configuration
#

# Define a specific application area_id
app_specific_area_id=0x12A205

# App version
app_major=1
app_minor=0
app_maintenance=0
app_development=0
