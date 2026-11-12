# Enable CBOR library
TINY_CBOR=yes

# Enable the debug logs (has no effect on board without serial port)
APP_PRINTING=yes

# UART Configuration
# Set HAL_UART=yes if your application needs UART/serial communication
HAL_UART=no

# UART driver configuration (only applies when HAL_UART=yes)
ifeq ($(HAL_UART),yes)

    # Check if multi-instance UART driver should be used
    # (Set uart_multi_instance=yes in config.mk to enable multi-UART support)
    ifeq ($(uart_multi_instance),yes)
        # Multi-instance driver mode (supports multiple UART instances)
        UART_MULTI_INSTANCE=yes
        # These options are for single-instance driver only, so set them to no
        UART_USE_DMA=no
        UART_USE_USB=no
    else
        # Single-instance driver mode (legacy)
        # Uncomment and configure if needed:
        # UART_USE_DMA=yes  # Use DMA-based UART (default for most platforms)
        # UART_USE_USB=yes  # Use USB-based UART (alternative option)
    endif
endif
