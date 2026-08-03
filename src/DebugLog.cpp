#include "DebugLog.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <sys/syscall.h>

static std::mutex               g_mutex;
static std::ofstream            g_file;
static std::thread              g_heartbeat;
static std::atomic<bool>        g_running{false};
static std::atomic<bool>        g_enabled{false};
static long                     g_mainTid = 0; // tid of the thread that called init()

static std::string logPath() {
    // Home dir (survives a machine reboot, unlike /tmp) so a freeze that forces
    // a restart doesn't destroy the evidence we need.
    const char* home = getenv("HOME");
    return std::string(home ? home : "/tmp") + "/.local/share/hyprland/focusz-debug.log";
}

static std::string nowMillis() {
    using namespace std::chrono;
    return std::to_string(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

// Read the main thread's kernel state + cumulative CPU ticks from /proc.
static bool readMainCpu(unsigned long long& ticks, std::string& state) {
    const std::string path = "/proc/self/task/" + std::to_string(g_mainTid) + "/stat";
    std::ifstream f(path);
    if (!f)
        return false;
    std::string line;
    std::getline(f, line);
    f.close();

    // /proc/pid/task/tid/stat: "pid (comm) state ppid ..." — comm may contain
    // spaces/parens, so tokenize only after the last ')'.
    const size_t closeParen = line.rfind(')');
    if (closeParen == std::string::npos || closeParen + 2 >= line.size())
        return false;

    std::istringstream rest(line.substr(closeParen + 2));
    std::string tok;
    std::vector<std::string> toks;
    while (rest >> tok)
        toks.push_back(tok);

    // toks[0]=state(3) toks[1]=ppid(4) ... toks[11]=utime(14) toks[12]=stime(15)
    if (toks.size() < 13)
        return false;
    state = toks[0];
    ticks = strtoull(toks[11].c_str(), nullptr, 10) + strtoull(toks[12].c_str(), nullptr, 10);
    return true;
}

static void heartbeat() {
    unsigned long long prevTicks = 0;
    bool               havePrev  = false;
    while (g_running.load()) {
        unsigned long long ticks = 0;
        std::string        state;
        if (readMainCpu(ticks, state)) {
            std::string line = "HB main state=" + state;
            if (havePrev)
                line += " cpu_delta=" + std::to_string(ticks - prevTicks);
            DebugLog::log(line);
            prevTicks = ticks;
            havePrev  = true;
        } else {
            DebugLog::log("HB main ?? (cannot read /proc)");
        }
        for (int i = 0; i < 20 && g_running.load(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void DebugLog::init() {
    const char* env = getenv("FOCUSZ_DEBUG");
    setEnabled(env != nullptr && std::strlen(env) > 0);
}

void DebugLog::setEnabled(bool on) {
    if (on == g_enabled.load())
        return;

    if (on) {
        g_enabled = true;
        g_mainTid = (long)syscall(SYS_gettid);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_file.open(logPath(), std::ios::app);
        }
        log("=== focusZ debug start pid=" + std::to_string(getpid()) + " tid=" + std::to_string(g_mainTid) + " ===");
        g_running   = true;
        g_heartbeat = std::thread(heartbeat);
    } else {
        g_running = false;
        if (g_heartbeat.joinable())
            g_heartbeat.join();
        log("=== focusZ debug end ===");
        std::lock_guard<std::mutex> lock(g_mutex);
        g_file.close();
        g_enabled = false;
    }
}

void DebugLog::shutdown() {
    setEnabled(false);
}

bool DebugLog::isEnabled() {
    return g_enabled.load();
}

void DebugLog::log(const std::string& msg) {
    if (!g_enabled.load())
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_file.is_open())
        return;
    g_file << nowMillis() << " " << msg << "\n";
    g_file.flush();
}
