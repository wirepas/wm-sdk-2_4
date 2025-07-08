# You can add more sources here and libraries if needed
SRCS +=
INCLUDES +=
LIBS +=
LDFLAGS +=

# Example:
# SRCS += $(APP_SRCS_PATH)my_driver.c
# LIBS += my_lib.a
# include math.h lib
# LDFLAGS += -lm

#
# App setup and its dependencies for setting and managing encryption keys
#
APP_PERSISTENT=yes
APP_SETUP=yes
ifneq (,$(filter $(MCU_SUB), xg12 xg13 xg21 xg22 xg23))
PROVISIONING=no
else
PROVISIONING=yes
endif
