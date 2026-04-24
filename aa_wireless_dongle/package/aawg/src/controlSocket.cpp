#include "controlSocket.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <sstream>
#include <string>
#include <vector>

#include "bluetoothHandler.h"
#include "common.h"

static constexpr const char* CONTROL_SOCK_PATH = "/run/aawgd-control.sock";

/*static*/ ControlSocketServer& ControlSocketServer::instance() {
    static ControlSocketServer s_instance;
    return s_instance;
}

void ControlSocketServer::start() {
    if (m_thread.has_value()) {
        return;
    }
    m_thread = std::thread(&ControlSocketServer::run, this);
}

static std::string jsonEscape(const std::string& in) {
    std::ostringstream os;
    for (char c : in) {
        switch (c) {
            case '\\': os << "\\\\"; break;
            case '"': os << "\\\""; break;
            case '\n': os << "\\n"; break;
            case '\r': os << "\\r"; break;
            case '\t': os << "\\t"; break;
            default: os << c; break;
        }
    }
    return os.str();
}

static bool writeAll(int fd, const std::string& s) {
    const char* p = s.c_str();
    size_t remaining = s.size();
    while (remaining > 0) {
        ssize_t w = write(fd, p, remaining);
        if (w <= 0) {
            return false;
        }
        p += w;
        remaining -= (size_t)w;
    }
    return true;
}

static std::string handleCommand(const std::string& line) {
    // Commands:
    //   LIST
    //   DISCONNECT
    //   SWITCH <selector>
    // Response is single JSON object (with trailing newline).
    if (line == "LIST") {
        std::vector<BluetoothDeviceInfo> devices = BluetoothHandler::instance().listDevices();
        std::ostringstream os;
        os << "{\"ok\":true,\"devices\":[";
        for (size_t i = 0; i < devices.size(); i++) {
            const auto& d = devices[i];
            if (i) os << ",";
            os << "{"
               << "\"objectPath\":\"" << jsonEscape(d.objectPath) << "\""
               << ",\"address\":\"" << jsonEscape(d.address) << "\""
               << ",\"name\":\"" << jsonEscape(d.name) << "\""
               << ",\"paired\":" << (d.paired ? "true" : "false")
               << ",\"trusted\":" << (d.trusted ? "true" : "false")
               << ",\"connected\":" << (d.connected ? "true" : "false")
               << "}";
        }
        os << "]}\n";
        return os.str();
    }

    if (line == "DISCONNECT") {
        BluetoothHandler::instance().disconnectAll();
        return std::string("{\"ok\":true}\n");
    }

    static constexpr const char* SWITCH_PREFIX = "SWITCH ";
    if (line.rfind(SWITCH_PREFIX, 0) == 0) {
        const std::string selector = line.substr(strlen(SWITCH_PREFIX));
        const bool ok = BluetoothHandler::instance().switchToDevice(selector);
        std::ostringstream os;
        os << "{\"ok\":" << (ok ? "true" : "false") << "}\n";
        return os.str();
    }

    return std::string("{\"ok\":false,\"error\":\"unknown command\"}\n");
}

void ControlSocketServer::run() {
    // Best-effort: recreate socket on boot.
    unlink(CONTROL_SOCK_PATH);

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        Logger::instance()->info("controlSocket: socket() failed: %s\n", strerror(errno));
        return;
    }

    sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CONTROL_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        Logger::instance()->info("controlSocket: bind(%s) failed: %s\n", CONTROL_SOCK_PATH, strerror(errno));
        close(server_fd);
        return;
    }

    // Allow anyone to connect (the device is usually single-user; adjust if needed).
    chmod(CONTROL_SOCK_PATH, 0666);

    if (listen(server_fd, 5) < 0) {
        Logger::instance()->info("controlSocket: listen() failed: %s\n", strerror(errno));
        close(server_fd);
        return;
    }

    Logger::instance()->info("controlSocket: listening on %s\n", CONTROL_SOCK_PATH);

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            Logger::instance()->info("controlSocket: accept() failed: %s\n", strerror(errno));
            continue;
        }

        // Read a single line command.
        std::string line;
        char buf[256];
        while (true) {
            ssize_t r = read(client_fd, buf, sizeof(buf));
            if (r <= 0) {
                break;
            }
            line.append(buf, buf + r);
            size_t nl = line.find('\n');
            if (nl != std::string::npos) {
                line = line.substr(0, nl);
                break;
            }
            if (line.size() > 4096) {
                break;
            }
        }

        // Trim trailing \r.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const std::string response = handleCommand(line);
        writeAll(client_fd, response);
        close(client_fd);
    }
}

