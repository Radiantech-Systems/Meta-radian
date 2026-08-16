CC ?= $(CROSS_COMPILE)gcc
CFLAGS += -Wall

TARGET = p2p_uart

all: $(TARGET)

$(TARGET): p2p_uart.c
	$(CC) $(CFLAGS) -o $(TARGET) p2p_uart.c

clean:
	rm -f $(TARGET)
