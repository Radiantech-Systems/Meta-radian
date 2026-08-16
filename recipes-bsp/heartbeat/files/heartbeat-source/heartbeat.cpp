#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>

#define UART_DEVICE "/dev/ttyTHS1"
#define BAUDRATE B115200

int main()
{
    int uart_fd = open(UART_DEVICE, O_RDWR | O_NOCTTY);

    if (uart_fd < 0)
    {
        std::cerr << "Failed to open " << UART_DEVICE << std::endl;
        return 1;
    }

    struct termios tty{};

    if (tcgetattr(uart_fd, &tty) != 0)
    {
        std::cerr << "Failed to get UART attributes" << std::endl;
        close(uart_fd);
        return 1;
    }

    cfsetispeed(&tty, BAUDRATE);
    cfsetospeed(&tty, BAUDRATE);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_oflag &= ~OPOST;

    if (tcsetattr(uart_fd, TCSANOW, &tty) != 0)
    {
        std::cerr << "Failed to configure UART" << std::endl;
        close(uart_fd);
        return 1;
    }

    std::cout << "Heartbeat started on "
              << UART_DEVICE << std::endl;

    while (true)
    {
        const char *message = "HEARTBEAT\n";

        write(uart_fd, message, strlen(message));

        std::cout << "Sent: HEARTBEAT" << std::endl;

        sleep(1);
    }

    close(uart_fd);

    return 0;
}
