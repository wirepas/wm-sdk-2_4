# Copyright 2026 Wirepas Ltd. All Rights Reserved.
#
# See file LICENSE.txt for full license details.

#
# EFR32FG23 (xG23) Platform-Specific Makefile
#
# Adds hardware configuration files for xG23 with 3 EUSART instances
#

# Platform-specific directory
EUSART_XG23_DIR := $(EUSART_MULTI_DIR)/xg23

# Platform-specific source files
EUSART_PLATFORM_SRCS := \
    $(EUSART_XG23_DIR)/eusart_multi_hw_config.c \
    $(EUSART_XG23_DIR)/eusart_multi_platform.c

# Add platform sources
SRCS += $(EUSART_PLATFORM_SRCS)

# Add platform-specific include path
INCLUDES += -I$(EUSART_XG23_DIR)
