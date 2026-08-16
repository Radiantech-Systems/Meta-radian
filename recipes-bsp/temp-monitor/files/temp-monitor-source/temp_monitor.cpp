#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

constexpr int FAN_PWM_50  = 128;
constexpr int FAN_PWM_MAX = 220;

// =============================================================================
// UART (over USB CDC-ACM, e.g. STM32 exposed as /dev/ttyACM0)
//
// RAII wrapper around a termios-configured serial port, plus a fixed-size
// binary packet format:
//
//   Offset  Size  Field
//   ------  ----  ----------------------------------------------------------
//   0       1     START_BYTE   (0xAA)
//   1       1     LENGTH       (always PACKET_LENGTH, sent for validation)
//   2       1     SEQUENCE     (caller-assigned rolling counter)
//   3       1     TYPE         (discriminates how PAYLOAD should be read)
//   4       8     PAYLOAD      (raw bytes, meaning depends on TYPE)
//   12      2     CRC16-CCITT  (over LENGTH + SEQUENCE + TYPE + PAYLOAD)
//   14      1     RESERVED     (currently unused; not covered by CRC)
//   15      1     END_BYTE     (0x55)
//   ------  ----  ----------------------------------------------------------
//   Total = 16 bytes
//
// PAYLOAD for PacketType::Temperature (TYPE = 1):
//   [0] CPU  (deg C, single byte, 0-255, truncated/clamped)
//   [1] GPU  (deg C)
//   [2] SOC  (deg C)
//   [3] CV   (deg C)
//   [4] TJ   (deg C)
//   [5] Reserved (0x00)
//   [6] Reserved (0x00)
//   [7] Reserved (0x00)
//
// One byte per sensor instead of floats: no memcpy/endianness concerns on
// the STM32 side, no float decoding, smaller/simpler wire format, and
// temperature to +-1 degree resolution is plenty for this use case.
//
// This must match the STM32 receiver's PKT_OFF_* offsets exactly.
//
// Transport: this build talks to the STM32 over its USB virtual COM port
// (CDC-ACM), i.e. a plain USB cable rather than the Jetson's dedicated
// UART header pins. On the Jetson side this shows up as /dev/ttyACM0
// (not /dev/ttyTHS1), and the termios setup below is the raw 8N1/no-flow-
// control configuration a CDC-ACM device expects.
// =============================================================================
class UART
{
public:
    static constexpr uint8_t START_BYTE    = 0xAA;
    static constexpr uint8_t END_BYTE      = 0x55;
    static constexpr uint8_t PACKET_LENGTH = 16;
    static constexpr size_t  PAYLOAD_SIZE  = 8;

    // Bytes covered by the CRC: LENGTH(1) + SEQUENCE(1) + TYPE(1) + PAYLOAD(8).
    static constexpr size_t CRC_LENGTH = 1 + 1 + 1 + PAYLOAD_SIZE;

    enum class PacketType : uint8_t
    {
        Temperature = 1
    };

    // Payload byte offsets for PacketType::Temperature, named so call
    // sites don't use magic indices like payload[2].
    enum TempPayloadOffset : size_t
    {
        TEMP_OFF_CPU = 0,
        TEMP_OFF_GPU = 1,
        TEMP_OFF_SOC = 2,
        TEMP_OFF_CV  = 3,
        TEMP_OFF_TJ  = 4
        // 5,6,7 reserved
    };

    struct Packet
    {
        uint8_t                           start    = START_BYTE;
        uint8_t                           length   = PACKET_LENGTH;
        uint8_t                           sequence = 0;
        uint8_t                           type     = 0;
        std::array<uint8_t, PAYLOAD_SIZE> payload{};
        uint16_t                          crc      = 0;
        uint8_t                           reserved = 0;
        uint8_t                           end      = END_BYTE;
    };

    explicit UART(const std::string &device)
    {
        fd_ = open(device.c_str(), O_RDWR | O_NOCTTY);
        if (fd_ < 0)
            throw std::system_error(errno, std::generic_category(), "open()");
    }

    ~UART()
    {
        if (fd_ >= 0)
            close(fd_);
    }

    // Non-copyable (single owner of one fd), movable.
    UART(const UART &)            = delete;
    UART &operator=(const UART &) = delete;

    UART(UART &&other) noexcept : fd_(other.fd_)
    {
        other.fd_ = -1;
    }

    UART &operator=(UART &&other) noexcept
    {
        if (this != &other)
        {
            if (fd_ >= 0)
                close(fd_);
            fd_       = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    // termios setup mirrors the STM32 USB CDC-ACM reference exactly
    // (memset-cleared struct, explicit CLOCAL|CREAD, canonical/echo/
    // signal processing and all output/input post-processing disabled
    // via c_lflag/c_iflag/c_oflag = 0, non-blocking-with-timeout reads
    // via VMIN=0/VTIME=1) rather than cfmakeraw(), since that's the
    // known-working configuration for this USB link.
    void configure()
    {
        termios tty;
        memset(&tty, 0, sizeof(tty));

        if (tcgetattr(fd_, &tty) != 0)
            throw std::system_error(errno, std::generic_category(), "tcgetattr()");

        if (cfsetispeed(&tty, B115200) < 0 || cfsetospeed(&tty, B115200) < 0)
            throw std::system_error(errno, std::generic_category(), "cfsetspeed()");

        tty.c_cflag |= (CLOCAL | CREAD);
        tty.c_cflag &= ~PARENB;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CSIZE;
        tty.c_cflag |= CS8;

        tty.c_lflag = 0;
        tty.c_iflag = 0;
        tty.c_oflag = 0;

        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = 1;

        if (tcsetattr(fd_, TCSANOW, &tty) < 0)
            throw std::system_error(errno, std::generic_category(), "tcsetattr()");

        if (tcflush(fd_, TCIOFLUSH) < 0)
            throw std::system_error(errno, std::generic_category(), "tcflush()");

        verifyConfiguration();
    }

    // Returns void, not bool: failure is always reported via exception
    // (system_error from write()/tcdrain()), so a boolean return value
    // would only ever be "true" on the success path - the exception
    // already carries all the failure information a caller needs.
    //
    // Takes a raw pointer + length (instead of std::vector) so it works
    // directly with the stack-allocated std::array that serialize() now
    // returns, with no implicit copy into a heap-backed container.
    void writeAll(const uint8_t *data, size_t len)
    {
        size_t written = 0;

        while (written < len)
        {
            ssize_t n = write(fd_, data + written, len - written);

            if (n > 0)
            {
                written += static_cast<size_t>(n);
                continue;
            }

            if (n < 0 && errno == EINTR)
                continue;

            throw std::system_error(errno, std::generic_category(), "write()");
        }

        if (tcdrain(fd_) < 0)
            throw std::system_error(errno, std::generic_category(), "tcdrain()");
    }

    void sendText(const std::string &text)
    {
        writeAll(reinterpret_cast<const uint8_t *>(text.c_str()), text.length());
    }

    // Reads a single newline-terminated line from the STM32 (e.g. a
    // FAN30 / FAN50 / FAN100 command), stripping the trailing \r\n.
    // With VMIN=0/VTIME=1 each read() call blocks for at most ~100ms and
    // returns 0 on timeout, so this returns whatever partial/empty line
    // it has as soon as no more bytes are available rather than
    // blocking forever when the STM32 hasn't sent anything.
    std::string readLine()
    {
        std::string line;
        char ch;

        while (true)
        {
            ssize_t n = read(fd_, &ch, 1);

            if (n > 0)
            {
                if (ch == '\n')
                    break;

                if (ch != '\r')
                    line += ch;
            }
            else
            {
                break;
            }
        }

        return line;
    }

    static uint16_t crc16(const uint8_t *data, size_t len)
    {
        uint16_t crc = 0xFFFF;

        for (size_t i = 0; i < len; ++i)
        {
            crc ^= static_cast<uint16_t>(data[i]) << 8;

            for (int b = 0; b < 8; ++b)
            {
                if (crc & 0x8000)
                    crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
                else
                    crc = static_cast<uint16_t>(crc << 1);
            }
        }

        return crc;
    }

    // Returns a stack-allocated fixed-size array rather than a
    // std::vector. PACKET_LENGTH is a compile-time constant (16 bytes),
    // so there's no reason to pay for a heap allocation here - this
    // avoids one malloc/free per packet, which matters more as a matter
    // of embedded-adjacent hygiene than raw performance at 1 packet/sec,
    // but costs nothing to do correctly from the start.
    static std::array<uint8_t, PACKET_LENGTH> serialize(Packet pkt)
    {
        std::array<uint8_t, PACKET_LENGTH> out{};

        out[0] = pkt.start;
        out[1] = pkt.length;
        out[2] = pkt.sequence;
        out[3] = pkt.type;

        std::copy(pkt.payload.begin(), pkt.payload.end(), out.begin() + 4);

        pkt.crc = crc16(out.data() + 1, CRC_LENGTH);
        out[12] = static_cast<uint8_t>(pkt.crc >> 8);
        out[13] = static_cast<uint8_t>(pkt.crc & 0xFF);
        out[14] = pkt.reserved;
        out[15] = pkt.end;

        return out;
    }

private:
    void verifyConfiguration() const
    {
        termios verify{};
        if (tcgetattr(fd_, &verify) < 0)
            throw std::system_error(errno, std::generic_category(), "tcgetattr() [verify]");

        if (cfgetispeed(&verify) != B115200 || cfgetospeed(&verify) != B115200)
            throw std::runtime_error("UART: baud rate verification failed");

        if ((verify.c_cflag & CSIZE) != CS8 ||
            (verify.c_cflag & PARENB) ||
            (verify.c_cflag & CSTOPB))
            throw std::runtime_error("UART: frame format verification failed (expected 8N1)");

        if (!(verify.c_cflag & CLOCAL) || !(verify.c_cflag & CREAD))
            throw std::runtime_error("UART: CLOCAL/CREAD not set as expected");
    }

    int fd_{-1};
};

// =============================================================================
// Thermal sensor acquisition
// =============================================================================

struct ThermalZone
{
    std::string kernel_type;
    std::string sysfs_path;
};

std::vector<ThermalZone> discover_thermal_zones()
{
    std::vector<ThermalZone> zones;

    DIR *dir = opendir("/sys/class/thermal");
    if (dir == nullptr)
    {
        perror("discover_thermal_zones: opendir() failed");
        return zones;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (strncmp(entry->d_name, "thermal_zone", 12) != 0)
            continue;

        std::string type_path = std::string("/sys/class/thermal/") + entry->d_name + "/type";
        std::string temp_path = std::string("/sys/class/thermal/") + entry->d_name + "/temp";

        std::ifstream type_fp(type_path);
        if (!type_fp.is_open())
            continue;

        std::string type;
        if (!std::getline(type_fp, type))
            continue;

        while (!type.empty() && (type.back() == '\n' || type.back() == '\r'))
            type.pop_back();

        zones.push_back({type, temp_path});
    }

    closedir(dir);

    std::cout << "Detected " << zones.size() << " thermal zone(s):\n";
    for (const auto &z : zones)
        std::cout << "  " << z.kernel_type << " -> " << z.sysfs_path << "\n";

    return zones;
}

bool read_temperature(const std::string &path, float &out_value)
{
    if (path.empty())
        return false;

    std::ifstream fp(path);
    if (!fp.is_open())
        return false;

    std::string line;
    if (!std::getline(fp, line))
        return false;

    try
    {
        out_value = std::stof(line) / 1000.0f;
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
}

// Returns the sysfs path for the Nth (0-indexed) zone whose kernel-
// reported type starts with `prefix`, e.g. prefix="gpu" matches
// "gpu-thermal". Jetson boards expose slightly different zone
// names/counts across BSP versions (e.g. separate "cv0-thermal" /
// "cv1-thermal" / "cv2-thermal", or "soc0/1/2-thermal"), so matching by
// prefix instead of one exact name is more robust than requiring a
// single specific zone name to exist.
//
// `occurrence` defaults to 0 (first match), which is fine when a board
// only exposes one zone per category, or when "any one representative
// reading" is good enough. If you later need a *specific* SOC/CV core
// rather than whichever one happens to enumerate first (e.g. always CV0
// specifically, not CV1 or CV2), pass the desired occurrence index here,
// or switch to matching on the full exact zone name instead of a prefix.
std::string find_zone_path_by_prefix(const std::vector<ThermalZone> &zones,
                                      const std::string &prefix,
                                      size_t occurrence = 0)
{
    size_t seen = 0;
    for (const auto &z : zones)
    {
        if (z.kernel_type.rfind(prefix, 0) == 0) // starts_with(prefix)
        {
            if (seen == occurrence)
                return z.sysfs_path;
            ++seen;
        }
    }
    return "";
}

// Clamps a float Celsius reading into a single byte (0-255). Sub-degree
// precision is not needed for this telemetry link, and this keeps the
// wire payload at 1 byte/sensor instead of 4 (float) - see class-level
// comment on UART::Packet for the rationale.
uint8_t temp_to_byte(float temp_c)
{
    if (temp_c < 0.0f)
        return 0;
    if (temp_c > 255.0f)
        return 255;
    return static_cast<uint8_t>(temp_c + 0.5f); // round to nearest
}

void setFanPWM(int pwm)
{
    if (pwm < 0)
        pwm = 0;

    if (pwm > FAN_PWM_MAX)
        pwm = FAN_PWM_MAX;

    std::ofstream fan("/sys/devices/platform/pwm-fan/hwmon/hwmon6/pwm1");

    if (!fan.is_open())
    {
        std::cerr << "Failed to open PWM control file!" << std::endl;
        return;
    }

    fan << pwm;
    fan.close();

    std::cout << "Fan PWM set to " << pwm << std::endl;
}

int getFanPWM()
{
    std::ifstream fan("/sys/devices/platform/pwm-fan/hwmon/hwmon6/pwm1");

    if (!fan.is_open())
        return -1;

    int pwm;
    fan >> pwm;

    return pwm;
}

// =============================================================================
// main
// =============================================================================
int main()
{
    std::vector<ThermalZone> zones = discover_thermal_zones();

    std::string cpu_path = find_zone_path_by_prefix(zones, "cpu");
    std::string gpu_path = find_zone_path_by_prefix(zones, "gpu");
    std::string soc_path = find_zone_path_by_prefix(zones, "soc");
    std::string cv_path  = find_zone_path_by_prefix(zones, "cv");
    std::string tj_path  = find_zone_path_by_prefix(zones, "tj");

    if (cpu_path.empty()) std::cerr << "Warning: no CPU thermal zone found; CPU field will read 0\n";
    if (gpu_path.empty()) std::cerr << "Warning: no GPU thermal zone found; GPU field will read 0\n";
    if (soc_path.empty()) std::cerr << "Warning: no SOC thermal zone found; SOC field will read 0\n";
    if (cv_path.empty())  std::cerr << "Warning: no CV thermal zone found; CV field will read 0\n";
    if (tj_path.empty())  std::cerr << "Warning: no TJ thermal zone found; TJ field will read 0\n";

    try
    {
        // USB CDC-ACM device (STM32 connected over a USB cable), not the
        // Jetson's onboard UART header (/dev/ttyTHS1).
        UART uart("/dev/ttyACM0");
        uart.configure();
        std::cout << "USB serial (CDC-ACM) opened and configured (115200 8N1)\n";

        uint8_t sequence = 0;
        auto next_tick = std::chrono::steady_clock::now();

        while (true)
        {
            float cpu_temp = 0.0f, gpu_temp = 0.0f, soc_temp = 0.0f,
                  cv_temp  = 0.0f, tj_temp  = 0.0f;

            read_temperature(cpu_path, cpu_temp); // leaves 0.0f untouched on failure
            read_temperature(gpu_path, gpu_temp);
            read_temperature(soc_path, soc_temp);
            read_temperature(cv_path, cv_temp);
            read_temperature(tj_path, tj_temp);

            UART::Packet pkt;
            pkt.sequence = sequence;
            pkt.type     = static_cast<uint8_t>(UART::PacketType::Temperature);

            pkt.payload[UART::TEMP_OFF_CPU] = temp_to_byte(cpu_temp);
            pkt.payload[UART::TEMP_OFF_GPU] = temp_to_byte(gpu_temp);
            pkt.payload[UART::TEMP_OFF_SOC] = temp_to_byte(soc_temp);
            pkt.payload[UART::TEMP_OFF_CV]  = temp_to_byte(cv_temp);
            pkt.payload[UART::TEMP_OFF_TJ]  = temp_to_byte(tj_temp);
            // payload[5..7] left as default-initialized 0x00 (reserved)

            std::array<uint8_t, UART::PACKET_LENGTH> wire = UART::serialize(pkt);
            uint16_t crc_sent = (static_cast<uint16_t>(wire[12]) << 8) | wire[13];

            try
            {
                uart.writeAll(wire.data(), wire.size());

                std::string cmd = uart.readLine();

                if (!cmd.empty())
                {
                    std::cout << "STM32 CMD : " << cmd << std::endl;

                    if (cmd == "FAN30")
                    {
                        setFanPWM(FAN_PWM_50);

                        int pwm = getFanPWM();

                        if (pwm >= 0)
                        {
                            uart.sendText("FANPWM=" + std::to_string(pwm) + "\r\n");
                        }
                        else
                        {
                            uart.sendText("FANPWM=ERROR\r\n");
                        }

                        std::cout << "Fan Speed : 50%" << std::endl;
                    }
                    else if (cmd == "FAN50")
                    {
                        setFanPWM(FAN_PWM_MAX);

                        int pwm = getFanPWM();

                        if (pwm >= 0)
                        {
                            uart.sendText("FANPWM=" + std::to_string(pwm) + "\r\n");
                        }
                        else
                        {
                            uart.sendText("FANPWM=ERROR\r\n");
                        }

                        std::cout << "Fan Speed : 100%" << std::endl;
                    }
                    else if (cmd == "FAN100")
                    {
                        setFanPWM(FAN_PWM_MAX);

                        int pwm = getFanPWM();

                        if (pwm >= 0)
                        {
                            uart.sendText("FANPWM=" + std::to_string(pwm) + "\r\n");
                        }
                        else
                        {
                            uart.sendText("FANPWM=ERROR\r\n");
                        }

                        std::cout << "Fan Speed : MAX" << std::endl;
                    }
                }

                std::cout << "SEQ : " << static_cast<int>(sequence) << "\n"
                          << "CPU : " << static_cast<int>(pkt.payload[UART::TEMP_OFF_CPU]) << (char)0xB0 << "C\n"
                          << "GPU : " << static_cast<int>(pkt.payload[UART::TEMP_OFF_GPU]) << (char)0xB0 << "C\n"
                          << "SOC : " << static_cast<int>(pkt.payload[UART::TEMP_OFF_SOC]) << (char)0xB0 << "C\n"
                          << "CV  : " << static_cast<int>(pkt.payload[UART::TEMP_OFF_CV])  << (char)0xB0 << "C\n"
                          << "TJ  : " << static_cast<int>(pkt.payload[UART::TEMP_OFF_TJ])  << (char)0xB0 << "C\n"
                          << "CRC : 0x" << std::hex << std::uppercase << std::setw(4)
                          << std::setfill('0') << crc_sent << std::dec << "\n"
                          << "----------------------------------------\n";
            }
            catch (const std::exception &e)
            {
                // Don't let one bad transmit kill the whole process -- log
                // it and try again next cycle.
                std::cerr << "Transmit failed for seq " << static_cast<int>(sequence)
                          << ": " << e.what() << "\n";
            }

            ++sequence; // uint8_t wraps 255 -> 0 naturally

            next_tick += std::chrono::seconds(1);
            std::this_thread::sleep_until(next_tick);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
