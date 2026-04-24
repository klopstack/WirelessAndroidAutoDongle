#pragma once

#include <optional>
#include <thread>

class ControlSocketServer {
public:
    static ControlSocketServer& instance();

    // Starts a background thread serving /run/aawgd-control.sock
    void start();

private:
    ControlSocketServer() = default;
    ControlSocketServer(ControlSocketServer const&) = delete;
    ControlSocketServer& operator=(ControlSocketServer const&) = delete;

    void run();

    std::optional<std::thread> m_thread;
};

