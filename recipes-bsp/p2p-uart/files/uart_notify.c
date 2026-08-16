#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

#define BAUDRATE B115200

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Usage:\n");
        printf("  uart_notify <UART_DEVICE> <MESSAGE>\n");
        printf("\nExample:\n");
        printf("  uart_notify /dev/ttyTHS1 JETSON_BOOTED\n");
        printf("  uart_notify /dev/ttyTHS1 JETSON_SHUTDOWN\n");
        return 1;
    }

    const char *uart_device = argv[1];
    const char *message = argv[2];

    int fd;
    struct termios options;

    fd = open(uart_device, O_RDWR | O_NOCTTY | O_SYNC);

    if (fd < 0)
    {
        perror("Failed to open UART");
        return 1;
    }
    if (tcgetattr(fd, &options) != 0)
    {
        perror("tcgetattr");
        close(fd);
        return 1;
    }

    cfmakeraw(&options);

    cfsetispeed(&options, BAUDRATE);
    cfsetospeed(&options, BAUDRATE);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag &= ~CRTSCTS;

    options.c_cc[VMIN]  = 1;
    options.c_cc[VTIME] = 0;

    tcflush(fd, TCIOFLUSH);
    if (tcsetattr(fd, TCSANOW, &options) != 0)
    {
        perror("tcsetattr");
        close(fd);
        return 1;
    }

    char tx_buffer[128];

    snprintf(tx_buffer, sizeof(tx_buffer), "%s\r\n", message);

    int bytes = write(fd, tx_buffer, strlen(tx_buffer));

    if (bytes < 0)
    {
        perror("write");
        close(fd);
        return 1;
    }

    tcdrain(fd);
    printf("\n========================================\n");
    printf("UART Notification Successful\n");
    printf("----------------------------------------\n");
    printf("UART Device : %s\n", uart_device);
    printf("Message     : %s\n", message);
    printf("Bytes Sent  : %d\n", bytes);
    printf("========================================\n");

    close(fd);

    return 0;
}
