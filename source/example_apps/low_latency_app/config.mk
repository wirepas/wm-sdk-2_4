# Boards compatible with this app 
TARGET_BOARDS := pan1780 pca10040 pca10056 pca10100 tbsense2 
# Boards compatible with this app
#
# Network default settings configuration
#

# If this section is removed, node has to be configured in
# a different way
default_network_address ?= 0x6FCA8
default_network_channel ?= 6
# default_multicast_address ?=
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
APP_PERSISTENT=yes

# Define a specific application area_id
app_specific_area_id=0x80597C

# App version
app_major=1
app_minor=0
app_maintenance=0
app_development=1
