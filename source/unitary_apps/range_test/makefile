SRCS +=$(SRCS_PATH)uart_print.c
SRCS +=$(SRCS_PATH)measurements.c
SRCS +=$(SRCS_PATH)event.c

# Use App Scheduler and configure it
APP_SCHEDULER=yes
APP_SCHEDULER_TASKS=5

# Use Shared Data
SHARED_DATA=yes

# Use Shared App Config
SHARED_APP_CONFIG=yes
SHARED_APP_CONFIG_FILTERS=1

# Enable the debug logs (has no effect on board without serial port)
APP_PRINTING=yes
CFLAGS += -DDEBUG_LOG_UART_BAUDRATE=1000000

# HAL_UART=yes
HAL_LED=yes
