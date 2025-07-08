# Boards compatible with this app 
TARGET_BOARDS := pan1780 pca10040 pca10056 pca10100 ruuvitag tbsense2 
# Boards compatible with this app
#
# Network default settings configuration
#

# If this section is removed, node has to be configured in
# a different way
default_network_address ?= 0xD8D42B
default_network_channel ?= 9

# For running this application with network encryption, the following key definitions may be uncommented and
# filled with random data (exactly 16 bytes each). Also `allow_insecure_key_injection` needs to be set
# to 'yes'. Note that the keys end up as plaintext in device flash with this mechanism. Not for
# production use!
#
# For production use, use a secure provisioning method or refer to the app_setup library in the SDK.

#default_network_cipher_key ?= 0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??
#default_network_authen_key ?= 0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??,0x??
#allow_insecure_key_injection=yes

#
# App specific configuration
#

# Define a specific application area_id
app_specific_area_id=0x80597B

#define node operating mode (i.e low-energy or low-latency : 0 => low-energy / 1 => low-latency)
default_operating_mode ?= 0


# App version
app_major=1
app_minor=1
app_maintenance=1
app_development=0
