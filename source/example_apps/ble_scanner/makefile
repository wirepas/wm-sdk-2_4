# Adding ble scanner module
SRCS += $(SRCS_PATH)ble_scanner.c
INCLUDES += -I$(SRCS_PATH)

# Two tasks are needed for the scanner part
APP_SCHEDULER=yes
APP_SCHEDULER_TASKS=2

SHARED_DATA=yes
STACK_STATE_LIB=yes

HAL_LED=yes

ifeq ($(dualmcu_interface), yes)
    # Enable dualmcu with UART and dma to support high baudrate
    HAL_UART=yes
    DUALMCU_LIB=yes
    CFLAGS += -DDUALMCU_INTERFACE

    # Check if multi-instance UART driver should be used
    ifeq ($(uart_multi_instance),yes)
        # Multi-instance driver mode
        UART_MULTI_INSTANCE=yes
        # These options are for single-instance driver only, so set them to no
        UART_USE_DMA=no
        UART_USE_USB=no
    else
        # Single-instance driver mode (legacy)
        UART_USE_DMA=yes
    endif
else
    APP_PRINTING=yes
endif
