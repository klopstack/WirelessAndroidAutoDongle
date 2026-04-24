#include <mosquitto.h>

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>

static constexpr const char* CONTROL_SOCK_PATH = "/run/aawgd-control.sock";

static std::string getenvStr(const char* name, const char* def) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string(def ? def : "");
}

static int getenvInt(const char* name, int def) {
    const char* v = std::getenv(name);
    if (!v || !*v) return def;
    try {
        return std::stoi(v);
    } catch (...) {
        return def;
    }
}

static bool unixSockRequest(const std::string& line, std::string& out) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CONTROL_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    std::string req = line;
    if (req.empty() || req.back() != '\n') req.push_back('\n');
    if (write(fd, req.c_str(), req.size()) < 0) {
        close(fd);
        return false;
    }

    out.clear();
    char buf[1024];
    while (true) {
        ssize_t r = read(fd, buf, sizeof(buf));
        if (r <= 0) break;
        out.append(buf, buf + r);
        if (out.find('\n') != std::string::npos) break;
        if (out.size() > 64 * 1024) break;
    }
    close(fd);
    return !out.empty();
}

struct AppCtx {
    std::string topicPrefix;
    std::string topicCmd;
    std::string topicStatus;
    int statusIntervalSec = 5;
};

static void onConnect(struct mosquitto* m, void* obj, int rc) {
    auto* ctx = reinterpret_cast<AppCtx*>(obj);
    if (rc != 0) {
        return;
    }
    mosquitto_subscribe(m, nullptr, ctx->topicCmd.c_str(), 0);
}

static void publishStatus(struct mosquitto* m, AppCtx& ctx) {
    std::string listResp;
    bool ok = unixSockRequest("LIST", listResp);
    if (!ok) {
        const std::string payload = "{\"ok\":false,\"error\":\"control socket unavailable\"}\n";
        mosquitto_publish(m, nullptr, ctx.topicStatus.c_str(), (int)payload.size(), payload.c_str(), 0, false);
        return;
    }

    // `LIST` already returns a JSON object with trailing newline, forward it as-is.
    mosquitto_publish(m, nullptr, ctx.topicStatus.c_str(), (int)listResp.size(), listResp.c_str(), 0, false);
}

static std::optional<std::string> parseCommandLine(const std::string& payload) {
    // Supported:
    //   "disconnect"
    //   "switch <selector>"
    //   {"cmd":"disconnect"}
    //   {"cmd":"switch","selector":"..."}
    std::string s = payload;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());

    if (s == "disconnect") return std::string("DISCONNECT");
    if (s.rfind("switch ", 0) == 0) return std::string("SWITCH ") + s.substr(strlen("switch "));

    // Super-minimal JSON parsing (no deps). Good enough for controlled payloads.
    if (s.find("\"cmd\"") != std::string::npos && s.find("disconnect") != std::string::npos) {
        return std::string("DISCONNECT");
    }
    if (s.find("\"cmd\"") != std::string::npos && s.find("switch") != std::string::npos) {
        auto pos = s.find("\"selector\"");
        if (pos == std::string::npos) return std::nullopt;
        pos = s.find(':', pos);
        if (pos == std::string::npos) return std::nullopt;
        pos++;
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
        if (pos >= s.size() || s[pos] != '"') return std::nullopt;
        pos++;
        std::string sel;
        while (pos < s.size() && s[pos] != '"') {
            // Not handling escapes; keep it simple.
            sel.push_back(s[pos]);
            pos++;
        }
        if (sel.empty()) return std::nullopt;
        return std::string("SWITCH ") + sel;
    }

    return std::nullopt;
}

static void onMessage(struct mosquitto* m, void* obj, const struct mosquitto_message* msg) {
    (void)m;
    auto* ctx = reinterpret_cast<AppCtx*>(obj);
    (void)ctx;

    if (!msg || !msg->payload || msg->payloadlen <= 0) {
        return;
    }

    std::string payload(reinterpret_cast<const char*>(msg->payload), (size_t)msg->payloadlen);
    auto cmd = parseCommandLine(payload);
    if (!cmd.has_value()) {
        return;
    }

    std::string resp;
    unixSockRequest(cmd.value(), resp);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    const std::string host = getenvStr("AAWG_MQTT_HOST", "");
    const int port = getenvInt("AAWG_MQTT_PORT", 1883);
    const std::string username = getenvStr("AAWG_MQTT_USERNAME", "");
    const std::string password = getenvStr("AAWG_MQTT_PASSWORD", "");
    const std::string clientId = getenvStr("AAWG_MQTT_CLIENT_ID", "aawgd");
    const std::string topicPrefix = getenvStr("AAWG_MQTT_TOPIC_PREFIX", "aawgd");
    const int interval = getenvInt("AAWG_MQTT_STATUS_INTERVAL_SEC", 5);

    if (host.empty()) {
        // Misconfigured; nothing to do.
        return 1;
    }

    mosquitto_lib_init();

    AppCtx ctx;
    ctx.topicPrefix = topicPrefix;
    ctx.topicCmd = topicPrefix + "/cmd";
    ctx.topicStatus = topicPrefix + "/status";
    ctx.statusIntervalSec = interval > 0 ? interval : 5;

    struct mosquitto* m = mosquitto_new(clientId.c_str(), true, &ctx);
    if (!m) {
        mosquitto_lib_cleanup();
        return 2;
    }

    if (!username.empty()) {
        mosquitto_username_pw_set(m, username.c_str(), password.empty() ? nullptr : password.c_str());
    }

    mosquitto_connect_callback_set(m, onConnect);
    mosquitto_message_callback_set(m, onMessage);

    const int keepalive = 30;
    if (mosquitto_connect(m, host.c_str(), port, keepalive) != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(m);
        mosquitto_lib_cleanup();
        return 3;
    }

    mosquitto_loop_start(m);

    while (true) {
        publishStatus(m, ctx);
        std::this_thread::sleep_for(std::chrono::seconds(ctx.statusIntervalSec));
    }

    // Unreachable
    mosquitto_loop_stop(m, true);
    mosquitto_disconnect(m);
    mosquitto_destroy(m);
    mosquitto_lib_cleanup();
    return 0;
}

