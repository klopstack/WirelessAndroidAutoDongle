#pragma once

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "bluetoothCommon.h"

class BluezAdapterProxy;
class AAWirelessProfile;
class HSPHSProfile;
class BLEAdvertisement;

struct BluetoothDeviceInfo {
    std::string objectPath;   // e.g. /org/bluez/hci0/dev_XX_XX_XX_XX_XX_XX
    std::string address;      // e.g. XX:XX:XX:XX:XX:XX
    std::string name;         // Friendly name (may be empty)
    bool paired = false;
    bool trusted = false;
    bool connected = false;
};

class BluetoothHandler {
public:
    static BluetoothHandler& instance();

    void init();
    void powerOn();
    void powerOff();

    std::optional<std::thread> connectWithRetry();
    void stopConnectWithRetry();

    // Control-plane helpers (used by local control socket / UI / button hooks)
    std::vector<BluetoothDeviceInfo> listDevices();
    bool switchToDevice(const std::string& selector);
    void disconnectAll();

private:
    BluetoothHandler() {};
    BluetoothHandler(BluetoothHandler const&);
    BluetoothHandler& operator=(BluetoothHandler const&);

    DBus::ManagedObjects getBluezObjects();

    void initAdapter();
    void setPower(bool on);
    void setPairable(bool pairable);
    void exportProfiles();
    void connectDevice();
    bool connectDeviceByObjectPath(const std::string& objectPath);
    void disconnectAllConnectedDevices();

    void startAdvertising();
    void stopAdvertising();

    void retryConnectLoop();

    std::shared_ptr<std::promise<void>> connectWithRetryPromise;

    std::shared_ptr<DBus::Dispatcher> m_dispatcher;
    std::shared_ptr<DBus::Connection> m_connection;
    std::shared_ptr<BluezAdapterProxy> m_adapter;

    std::shared_ptr<AAWirelessProfile> m_aawProfile;
    std::shared_ptr<HSPHSProfile> m_hspProfile;

    std::shared_ptr<BLEAdvertisement> m_leAdvertisement;

    std::string m_adapterAlias;

    // If set, connect retry loop prioritizes this device.
    std::optional<std::string> m_preferredDeviceObjectPath;
};
