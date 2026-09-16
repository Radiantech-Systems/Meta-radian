#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

constexpr int CHECK_INTERVAL_SEC = 1;
constexpr int PING_COUNT = 3;
constexpr int PING_TIMEOUT_SEC = 1;

// How much higher than LAN's own metric to make the 5G default route.
// Higher metric = LOWER priority in the Linux routing table, so this
// guarantees 5G never outranks LAN even if LAN's metric changes.
constexpr int FIVE_G_METRIC_MARGIN = 500;
constexpr int FALLBACK_LAN_METRIC = 100; // assumed if LAN metric can't be read

// Debounce thresholds: require this many CONSECUTIVE results before
// acting, so a single transient ping failure or a single lucky ping
// doesn't cause an unnecessary switch. Applies only to 5G, since LAN
// state (interface + IPv4 present) is not flaky the way a cellular
// ping is.
constexpr int FIVE_G_CONFIRM_COUNT = 3; // consecutive PASS before switching TO 5G
constexpr int FIVE_G_DROP_COUNT = 3;    // consecutive FAIL before switching AWAY from 5G

// Set true to get the old noisy per-second "5G health: PASS/FAIL |
// loss=.. | latency=.." line on every single check. Default false so
// steady-state test runs produce a readable log: only counter
// progress, resets, and actual switch events get printed.
constexpr bool VERBOSE_PING_LOG = false;

const std::string LAN_INTERFACE = "eth0";
const std::string PING_TARGET = "8.8.8.8";

// --------------------------------------------------
// Timestamp
// --------------------------------------------------

std::string timestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    return buffer;
}

// --------------------------------------------------
// Logging
// --------------------------------------------------

void log(const std::string& message)
{
    std::cout << "[" << timestamp() << "] " << message << std::endl;
}

// --------------------------------------------------
// Execute command and return first line
// --------------------------------------------------

std::optional<std::string> commandOutput(const std::string& command)
{
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe)
        return std::nullopt;

    char buffer[256];
    if (!fgets(buffer, sizeof(buffer), pipe))
    {
        pclose(pipe);
        return std::nullopt;
    }
    pclose(pipe);

    std::string result(buffer);
    while (!result.empty() &&
           (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
    {
        result.pop_back();
    }

    if (result.empty())
        return std::nullopt;

    return result;
}

// --------------------------------------------------
// Run command and report whether it succeeded, with stderr captured
// for logging (instead of silently swallowing failures).
// --------------------------------------------------

struct CommandResult
{
    bool success;
    std::string output;
};

CommandResult runCommandChecked(const std::string& command)
{
    std::string fullCommand = command + " 2>&1";
    FILE* pipe = popen(fullCommand.c_str(), "r");
    if (!pipe)
        return {false, "popen failed"};

    char buffer[512];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe))
        output += buffer;

    int status = pclose(pipe);
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
        output.pop_back();

    return {status == 0, output};
}

// --------------------------------------------------
// Find 5G interface
// --------------------------------------------------

std::optional<std::string> find5GInterface()
{
    return commandOutput(
        "ip -o link show | "
        "awk -F': ' '$2 ~ /^wwu/ {print $2; exit}'");
}

// --------------------------------------------------
// Check IPv4 address
// --------------------------------------------------

bool hasIPv4(const std::string& interface)
{
    std::string command =
        "ip -4 addr show dev " + interface + " | grep -q 'inet '";
    return std::system(command.c_str()) == 0;
}

// --------------------------------------------------
// Check interface state
// --------------------------------------------------

bool interfaceExists(const std::string& interface)
{
    std::string command = "ip link show " + interface + " >/dev/null 2>&1";
    return std::system(command.c_str()) == 0;
}

// --------------------------------------------------
// Get gateway
// --------------------------------------------------

std::optional<std::string> getGateway(const std::string& interface)
{
    return commandOutput(
        "ip -4 route show dev " + interface +
        " | awk '/default/ {"
        "for(i=1;i<=NF;i++) "
        "if($i==\"via\") print $(i+1);"
        "exit}'");
}

// --------------------------------------------------
// Get LAN's own default-route metric, so we can always
// place the 5G route below it. Falls back to a safe
// assumed value if it can't be read (e.g. LAN is down).
// --------------------------------------------------

int getLANMetric()
{
    auto value = commandOutput(
        "ip -4 route show dev " + LAN_INTERFACE +
        " | awk '/default/ {"
        "for(i=1;i<=NF;i++) "
        "if($i==\"metric\") print $(i+1);"
        "exit}'");

    if (!value)
        return FALLBACK_LAN_METRIC;

    try
    {
        return std::stoi(*value);
    }
    catch (...)
    {
        return FALLBACK_LAN_METRIC;
    }
}

// --------------------------------------------------
// Ping test
// --------------------------------------------------

struct PingResult
{
    bool success;
    double packetLoss;
    double averageLatency;
};

PingResult pingTest(const std::string& interface)
{
    std::string command =
        "ping -I " + interface +
        " -c " + std::to_string(PING_COUNT) +
        " -W " + std::to_string(PING_TIMEOUT_SEC) +
        " " + PING_TARGET + " 2>/dev/null";

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe)
        return {false, 100.0, 0.0};

    char buffer[512];
    double packetLoss = 100.0;
    double averageLatency = 0.0;

    while (fgets(buffer, sizeof(buffer), pipe))
    {
        std::string line(buffer);

        auto lossPos = line.find("% packet loss");
        if (lossPos != std::string::npos)
        {
            size_t start = line.rfind(' ', lossPos);
            if (start != std::string::npos)
            {
                std::string value = line.substr(start + 1, lossPos - start - 1);
                try { packetLoss = std::stod(value); } catch (...) {}
            }
        }

        auto rttPos = line.find(" = ");
        if (rttPos != std::string::npos && line.find("min/avg/max") != std::string::npos)
        {
            std::string values = line.substr(rttPos + 3);
            size_t firstSlash = values.find('/');
            if (firstSlash != std::string::npos)
            {
                size_t secondSlash = values.find('/', firstSlash + 1);
                if (secondSlash != std::string::npos)
                {
                    std::string avg = values.substr(firstSlash + 1, secondSlash - firstSlash - 1);
                    try { averageLatency = std::stod(avg); } catch (...) {}
                }
            }
        }
    }

    int status = pclose(pipe);
    bool success = status == 0 && packetLoss < 100.0;

    return {success, packetLoss, averageLatency};
}

// --------------------------------------------------
// Check LAN
// --------------------------------------------------

bool lanAvailable()
{
    if (!interfaceExists(LAN_INTERFACE))
        return false;
    if (!hasIPv4(LAN_INTERFACE))
        return false;
    return true;
}

// --------------------------------------------------
// Add/select 5G route
// --------------------------------------------------

bool select5G(const std::string& interface, const std::string& gateway, int metric)
{
    std::string command =
        "ip route replace default via " + gateway +
        " dev " + interface + " metric " + std::to_string(metric);

    auto result = runCommandChecked(command);
    if (!result.success)
        log("ERROR: failed to install 5G default route: " + result.output);

    return result.success;
}

// --------------------------------------------------
// Select LAN: remove the specific 5G default route we
// installed (identified by interface + metric), and
// verify it's actually gone rather than assuming success.
// --------------------------------------------------

bool selectLAN(const std::string& fiveGInterface, int fiveGMetric)
{
    std::string delCommand =
        "ip route del default dev " + fiveGInterface +
        " metric " + std::to_string(fiveGMetric);

    auto result = runCommandChecked(delCommand);

    // "No such process" from `ip route del` just means the route was
    // already gone (e.g. interface disappeared and took it with it) --
    // that's fine, not a failure.
    bool alreadyGone = result.output.find("No such process") != std::string::npos;

    if (!result.success && !alreadyGone)
    {
        log("ERROR: failed to remove 5G default route: " + result.output);
        return false;
    }

    // Verify LAN is genuinely the winning default route now.
    auto egress = commandOutput(
        "ip route get 8.8.8.8 2>/dev/null | awk '{for(i=1;i<=NF;i++) if($i==\"dev\") print $(i+1)}'");

    if (!egress || *egress != LAN_INTERFACE)
    {
        log("ERROR: 5G route removed but LAN is not the active egress (got: " +
            (egress ? *egress : std::string("none")) + ")");
        return false;
    }

    return true;
}

// --------------------------------------------------
// Main
// --------------------------------------------------

int main()
{
    log("========================================");
    log(" Radian Network Resilience Test");
    log(" LAN <-> 5G");
    log("========================================");
    log("LAN priority: ENABLED");
    log("5G health check: PING");
    log("Ping target: " + PING_TARGET);

    enum class Network { NONE, LAN, FIVE_G };

    Network activeNetwork = Network::NONE;

    // Track when each network was last CONFIRMED healthy, so a
    // transition's reported latency is "time since the old path was
    // last known good" instead of a same-iteration instant that's
    // always near zero. Still an approximation bounded by poll
    // interval + ping test duration, but honest about what it means.
    std::optional<std::chrono::steady_clock::time_point> lastLANHealthyTime;
    std::optional<std::chrono::steady_clock::time_point> lastFiveGHealthyTime;

    // Remember exactly which 5G interface + metric we installed, so
    // selectLAN() can remove precisely that route rather than
    // re-deriving it (and possibly getting nullopt) at switch time.
    std::string activeFiveGInterface;
    int activeFiveGMetric = 0;

    // Debounce counters. fiveGConsecutivePass counts up while 5G is
    // being evaluated as a candidate to switch TO (LAN unavailable,
    // 5G not yet active). fiveGConsecutiveFail counts up while 5G IS
    // active and checks start failing. Only one of the two is ever
    // meaningful at a time, given how they're reset below.
    int fiveGConsecutivePass = 0;
    int fiveGConsecutiveFail = 0;

    while (true)
    {
        auto now = std::chrono::steady_clock::now();

        // ----------------------------------------
        // Check LAN
        // ----------------------------------------
        bool lanOK = lanAvailable();
        if (lanOK)
            lastLANHealthyTime = now;

        // ----------------------------------------
        // Find 5G
        // ----------------------------------------
        auto fiveGInterface = find5GInterface();
        bool fiveGInterfaceOK = fiveGInterface.has_value();
        bool fiveGIPv4OK = fiveGInterfaceOK && hasIPv4(*fiveGInterface);

        // ----------------------------------------
        // 5G health test (raw, single-cycle result)
        // ----------------------------------------
        bool fiveGCheckPassed = false;
        PingResult pingResult{false, 100.0, 0.0};

        if (fiveGInterfaceOK && fiveGIPv4OK)
        {
            pingResult = pingTest(*fiveGInterface);
            fiveGCheckPassed = pingResult.success;

            if (VERBOSE_PING_LOG)
            {
                log("5G health: " + std::string(fiveGCheckPassed ? "PASS" : "FAIL") +
                    " | loss=" + std::to_string(pingResult.packetLoss) +
                    "% | latency=" + std::to_string(pingResult.averageLatency) + " ms");
            }

            if (fiveGCheckPassed)
                lastFiveGHealthyTime = std::chrono::steady_clock::now();
        }

        // ----------------------------------------
        // Apply debouncing on top of the raw check.
        // "fiveGHealthy" below means "debounce-confirmed", i.e. the
        // value actually used for switching decisions.
        // ----------------------------------------
        bool fiveGHealthy;

        if (lanOK)
        {
            // LAN is about to take priority regardless of 5G's state --
            // don't let stale counters cause a spurious log or an
            // instant re-switch next time LAN drops. Start fresh.
            fiveGConsecutivePass = 0;
            fiveGConsecutiveFail = 0;
            fiveGHealthy = false; // irrelevant, LAN wins below anyway
        }
        else if (activeFiveGInterface == (fiveGInterface ? *fiveGInterface : "") &&
                 activeFiveGMetric != 0)
        {
            // 5G is currently the active network: watch for sustained failure.
            if (fiveGCheckPassed)
            {
                if (fiveGConsecutiveFail > 0)
                    log("5G recovered after " + std::to_string(fiveGConsecutiveFail) +
                        " failed check(s), staying on 5G");
                fiveGConsecutiveFail = 0;
            }
            else
            {
                fiveGConsecutiveFail++;
                log("5G check failed while active (" + std::to_string(fiveGConsecutiveFail) +
                    "/" + std::to_string(FIVE_G_DROP_COUNT) + ")");
            }
            fiveGHealthy = fiveGConsecutiveFail < FIVE_G_DROP_COUNT;
        }
        else
        {
            // 5G is not currently active: require sustained success
            // before treating it as a real candidate to switch to.
            if (fiveGCheckPassed)
            {
                fiveGConsecutivePass++;
                log("5G check passed (" + std::to_string(fiveGConsecutivePass) +
                    "/" + std::to_string(FIVE_G_CONFIRM_COUNT) + ") - confirming stability");
            }
            else
            {
                if (fiveGConsecutivePass > 0)
                    log("5G check failed, resetting confirmation counter (was " +
                        std::to_string(fiveGConsecutivePass) + "/" +
                        std::to_string(FIVE_G_CONFIRM_COUNT) + ")");
                fiveGConsecutivePass = 0;
            }
            fiveGHealthy = fiveGConsecutivePass >= FIVE_G_CONFIRM_COUNT;
        }

        // ----------------------------------------
        // Decide preferred network (LAN always wins outright;
        // 5G only wins once debounce-confirmed)
        // ----------------------------------------
        Network desiredNetwork = Network::NONE;
        if (lanOK)
            desiredNetwork = Network::LAN;
        else if (fiveGHealthy)
            desiredNetwork = Network::FIVE_G;

        // ----------------------------------------
        // Network transition
        // ----------------------------------------
        if (desiredNetwork != activeNetwork)
        {
            if (desiredNetwork == Network::LAN)
            {
                bool switched = true;

                if (activeNetwork == Network::FIVE_G)
                {
                    log("EVENT: LAN became available");
                    log("SWITCH: 5G -> LAN");

                    switched = selectLAN(activeFiveGInterface, activeFiveGMetric);

                    if (!switched)
                    {
                        log("WARNING: switch to LAN failed, will retry next cycle");
                    }
                }
                else
                {
                    log("LAN available");
                }

                if (switched)
                {
                    activeNetwork = Network::LAN;
                    activeFiveGInterface.clear();
                    activeFiveGMetric = 0;
                    fiveGConsecutivePass = 0;
                    fiveGConsecutiveFail = 0;

                    auto since = lastFiveGHealthyTime.value_or(now);
                    auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       now - since).count();

                    log("LAN ACTIVE");
                    log("Network switch latency (since 5G last confirmed healthy): " +
                        std::to_string(latency) + " ms");
                }
                // else: leave activeNetwork as-is (FIVE_G) and retry the
                // switch on the next loop iteration instead of pretending
                // it succeeded.
            }
            else if (desiredNetwork == Network::FIVE_G)
            {
                auto gateway = getGateway(*fiveGInterface);

                if (gateway)
                {
                    log("EVENT: 5G data path healthy");
                    log("5G ping latency: " + std::to_string(pingResult.averageLatency) + " ms");
                    log("5G packet loss: " + std::to_string(pingResult.packetLoss) + "%");
                    log("SWITCH: LAN -> 5G");

                    int lanMetric = getLANMetric();
                    int fiveGMetric = lanMetric + FIVE_G_METRIC_MARGIN;

                    if (select5G(*fiveGInterface, *gateway, fiveGMetric))
                    {
                        activeNetwork = Network::FIVE_G;
                        activeFiveGInterface = *fiveGInterface;
                        activeFiveGMetric = fiveGMetric;
                        fiveGConsecutivePass = 0;
                        fiveGConsecutiveFail = 0;

                        auto since = lastLANHealthyTime.value_or(now);
                        auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           now - since).count();

                        log("5G ACTIVE: " + *fiveGInterface +
                            " (metric " + std::to_string(fiveGMetric) +
                            ", LAN metric " + std::to_string(lanMetric) + ")");
                        log("Network switch latency (since LAN last confirmed healthy): " +
                            std::to_string(latency) + " ms");
                    }
                    else
                    {
                        log("WARNING: switch to 5G failed, will retry next cycle");
                    }
                }
                else
                {
                    log("WARNING: 5G healthy but no gateway found, cannot install route");
                }
            }
            else // Network::NONE
            {
                log("WARNING: no usable network (LAN down" +
                    std::string(activeNetwork == Network::FIVE_G
                                    ? ", 5G dropped after " + std::to_string(FIVE_G_DROP_COUNT) + " failed checks)"
                                    : ", 5G not yet confirmed)"));

                activeNetwork = Network::NONE;
                activeFiveGInterface.clear();
                activeFiveGMetric = 0;
                fiveGConsecutivePass = 0;
                fiveGConsecutiveFail = 0;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(CHECK_INTERVAL_SEC));
    }

    return 0;
}
