# Boards compatible with this app 
TARGET_BOARDS := bgm220-ek4314a efr32_template mdbt50q_rx nrf52832_mdk_v2 nrf52_template pan1780 pca10040 pca10056 pca10059 pca10090_nrf52840 pca10100 pca10112 promistel_rpi_hat ruuvitag silabs_brd2601b silabs_brd2703a silabs_brd4180b silabs_brd4184a silabs_brd4187c silabs_brd4253a silabs_brd4254a silabs_brd4312a tbsense2 ublox_b204 wuerth_261101102 
#
# Network default settings configuration
#

# If this section is removed, node has to be configured in
# a different way
default_network_address ?= 0x1012EE
default_network_channel ?= 2
# !HIGHLY RECOMMENDED! : To enable security keys please un-comment the lines below and fill with a
#                        randomly generated authentication & encryption keys (exactly 16 bytes)
default_network_cipher_key ?= 0x10,0x0f,0x0e,0x0d,0x0c,0x0b,0x0a,0x09,0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01
default_network_authen_key ?= 0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10

#
# App specific configuration
#

# Define a specific application area_id
app_specific_area_id=0x8A2336

# App version
app_major=0
app_minor=0
app_maintenance=1
app_development=0
