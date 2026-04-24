#pragma once

#include <string>
#include <cstdint>
#include <optional>

enum SecurityMode: int;
enum AccessPointType: int;

struct WifiInfo {
    std::string ssid;
    std::string key;
    std::string bssid;
    SecurityMode securityMode;
    AccessPointType accessPointType;
    std::string ipAddress;
    int32_t port;
};

enum class ConnectionStrategy {
    DONGLE_MODE = 0,
    PHONE_FIRST = 1,
    USB_FIRST = 2
};

class Config {
public:
    static Config* instance();

    WifiInfo getWifiInfo();
    ConnectionStrategy getConnectionStrategy();

    // Optional: override the BlueZ adapter Alias / advertised name.
    // If empty, the default "WirelessAADongle-<suffix>" / "AndroidAuto-Dongle-<suffix>" is used.
    std::string getBluetoothName();

    std::string getUniqueSuffix();
private:
    Config() = default;

    int32_t getenv(std::string name, int32_t defaultValue);
    std::string getenv(std::string name, std::string defaultValue);

    std::string getInterfaceIpv4Address(std::string interface);
    std::string getMacAddress(std::string interface);

    std::optional<ConnectionStrategy> connectionStrategy;
};

class Logger {
public:
    static Logger* instance();

    void info(const char *format, ...);
private:
    Logger();
    ~Logger();
};