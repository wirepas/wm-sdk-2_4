# Boards compatible with this app 
TARGET_BOARDS := pca10040 pca10056 
# Specify supported target boards here

#
# Network default settings configuration
#

# If this section is removed, node has to be configured in
# a different way
default_network_address ?= 0x48bf6c
default_network_channel ?= 3

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
app_specific_area_id=0xb7318d

# App version
app_major=1
app_minor=0
app_maintenance=0
app_development=0
