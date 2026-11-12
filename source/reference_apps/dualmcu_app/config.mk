# Boards compatible with this app 
TARGET_BOARDS := bg22-ek4108a mdbt50q_rx nrf52832_mdk_v2 pan1780 pca10040 pca10056 pca10059 pca10100 pca10156 promistel_rpi_hat silabs_brd2601b silabs_brd2703a silabs_brd4180b silabs_brd4181b silabs_brd4184a silabs_brd4187c silabs_brd4253a silabs_brd4254a tbsense2 ublox_b204 wuerth_261101102 

# Define a specific application area_id
app_specific_area_id=0x846B74

# App version
app_major=$(sdk_major)
app_minor=$(sdk_minor)
app_maintenance=$(sdk_maintenance)
app_development=$(sdk_development)

# Uncomment to allow reading scratchpad via dual-MCU API
#allow_scratchpad_read=yes

# Enable multi-instance UART driver (set to yes for multi-UART support)
uart_multi_instance?=no

usart_multi_tx_buffer_size?=256
usart_multi_rx_buffer_size?=256
usart_multi_max_instances?=1
