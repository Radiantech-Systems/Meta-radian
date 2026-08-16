#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

#define BAUDRATE B115200

void send_uart(const char *device, const char *message)
{
    int fd;
    struct termios options;

    fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);

    if (fd < 0)
    {
        perror(device);
        return;
    }

    if (tcgetattr(fd, &options) != 0)
    {
        perror("tcgetattr");
        close(fd);
        return;
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

    options.c_cc[VMIN] = 1;
    options.c_cc[VTIME] = 0;

    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &options) != 0)
    {
        perror("tcsetattr");
        close(fd);
        return;
    }
    int bytes = write(fd, message, strlen(message));

    if (bytes < 0)
    {
        perror("write");
    }
    else
    {
        printf("----------------------------------------\n");
        printf("UART Device : %s\n", device);
        printf("Message Sent: \"%s\"\n", message);
        printf("Bytes Sent  : %d\n", bytes);
        printf("----------------------------------------\n\n");
    }

    tcdrain(fd);
    close(fd);
}
int main(void)
{
    send_uart("/dev/ttyTHS1", "Hello from THS1\r\n");

    sleep(1);

    send_uart("/dev/ttyTHS2", "Hello from THS2\r\n");

    printf("UART transmission completed.\n");

    return 0;
}
