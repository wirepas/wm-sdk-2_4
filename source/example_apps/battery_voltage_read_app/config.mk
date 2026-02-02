# Boards compatible with this app 
TARGET_BOARDS := pca10040 pca10056 pca10100 pca10156 ruuvitag silabs_brd2601b silabs_brd2703a silabs_brd4180b silabs_brd4181b silabs_brd4184a silabs_brd4187c silabs_brd4253a silabs_brd4254a tbsense2 
#
# Network default settings configuration
#

# This is only implemented for couple of boards

# If this section is removed, node has to be configured in
# a different way
default_network_address ?= 0x2ebe92
default_network_channel ?= 27
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
app_specific_area_id=0x0dc9ed

# App version
app_major=1
app_minor=0
app_maintenance=0
app_development=1
