#include <stdio.h>
#include <algorithm>

#include "common.h"
#include "bluetoothHandler.h"
#include "bluetoothProfiles.h"
#include "bluetoothAdvertisement.h"

static constexpr const char* ADAPTER_ALIAS_PREFIX = "WirelessAADongle-";
static constexpr const char* ADAPTER_ALIAS_DONGLE_PREFIX = "AndroidAuto-Dongle-";

static constexpr const char* BLUEZ_BUS_NAME = "org.bluez";
static constexpr const char* BLUEZ_ROOT_OBJECT_PATH = "/";
static constexpr const char* BLUEZ_OBJECT_PATH = "/org/bluez";

static constexpr const char* INTERFACE_BLUEZ_ADAPTER = "org.bluez.Adapter1";
static constexpr const char* INTERFACE_BLUEZ_LE_ADVERTISING_MANAGER = "org.bluez.LEAdvertisingManager1";

static constexpr const char* INTERFACE_BLUEZ_DEVICE = "org.bluez.Device1";
static constexpr const char* INTERFACE_BLUEZ_PROFILE_MANAGER = "org.bluez.ProfileManager1";

static constexpr const char* LE_ADVERTISEMENT_OBJECT_PATH = "/com/aawgd/bluetooth/advertisement";

static constexpr const char* AAWG_PROFILE_OBJECT_PATH = "/com/aawgd/bluetooth/aawg";
static constexpr const char* AAWG_PROFILE_UUID = "4de17a00-52cb-11e6-bdf4-0800200c9a66";

static constexpr const char* HSP_HS_PROFILE_OBJECT_PATH = "/com/aawgd/bluetooth/hsp";
static constexpr const char* HSP_AG_UUID = "00001112-0000-1000-8000-00805f9b34fb";
static constexpr const char* HSP_HS_UUID = "00001108-0000-1000-8000-00805f9b34fb";


class BluezAdapterProxy: private DBus::ObjectProxy {
    BluezAdapterProxy(std::shared_ptr<DBus::Connection> conn, DBus::Path path): DBus::ObjectProxy(conn, BLUEZ_BUS_NAME, path) {
        alias = this->create_property<std::string>(INTERFACE_BLUEZ_ADAPTER, "Alias");
        powered = this->create_property<bool>(INTERFACE_BLUEZ_ADAPTER, "Powered");
        discoverable = this->create_property<bool>(INTERFACE_BLUEZ_ADAPTER, "Discoverable");
        pairable = this->create_property<bool>(INTERFACE_BLUEZ_ADAPTER, "Pairable");

        registerAdvertisement = this->create_method<void(DBus::Path, DBus::Properties)>(INTERFACE_BLUEZ_LE_ADVERTISING_MANAGER, "RegisterAdvertisement");
        unregisterAdvertisement = this->create_method<void(DBus::Path)>(INTERFACE_BLUEZ_LE_ADVERTISING_MANAGER, "UnregisterAdvertisement");
    }

public:
    static std::shared_ptr<BluezAdapterProxy> create(std::shared_ptr<DBus::Connection> conn, DBus::Path path)
    {
      return std::shared_ptr<BluezAdapterProxy>(new BluezAdapterProxy(conn, path));
    }

    std::shared_ptr<DBus::PropertyProxy<std::string>> alias;
    std::shared_ptr<DBus::PropertyProxy<bool>> powered;
    std::shared_ptr<DBus::PropertyProxy<bool>> discoverable;
    std::shared_ptr<DBus::PropertyProxy<bool>> pairable;

    std::shared_ptr<DBus::MethodProxy<void(DBus::Path, DBus::Properties)>> registerAdvertisement;
    std::shared_ptr<DBus::MethodProxy<void(DBus::Path)>> unregisterAdvertisement;
};


BluetoothHandler& BluetoothHandler::instance() {
    static BluetoothHandler instance;
    return instance;
}

DBus::ManagedObjects BluetoothHandler::getBluezObjects() {
    std::shared_ptr<DBus::ObjectProxy> m_bluezRootObject = m_connection->create_object_proxy(BLUEZ_BUS_NAME, BLUEZ_ROOT_OBJECT_PATH);
    DBus::MethodProxy getManagedObjects = *(m_bluezRootObject->create_method<DBus::ManagedObjects(void)>("org.freedesktop.DBus.ObjectManager", "GetManagedObjects"));

    return getManagedObjects();
}

void BluetoothHandler::initAdapter() {
    DBus::ManagedObjects objects = getBluezObjects();

    std::string adapter_path;
    for (auto const& [path, interfaces]: objects) {
        for (auto const& [interface, properties]: interfaces) {
            if (interface == INTERFACE_BLUEZ_ADAPTER) {
                adapter_path = path;
                Logger::instance()->info("Using bluetooth adapter at path: %s\n", path.c_str());
                break;
            }
        }
        if (!adapter_path.empty()) {
            break;
        }
    }

    if (adapter_path.empty()) {
        Logger::instance()->info("Did not find any bluetooth adapters\n");
    }
    else {
        m_adapter = BluezAdapterProxy::create(m_connection, adapter_path);
        m_adapter->alias->set_value(m_adapterAlias);
        Logger::instance()->info("Bluetooth adapter alias: %s\n", m_adapterAlias.c_str());
    }
}

void BluetoothHandler::setPower(bool on) {
    if (!m_adapter) {
        return;
    }

    m_adapter->powered->set_value(on);
    Logger::instance()->info("Bluetooth adapter was powered %s\n", on ? "on" : "off");
}

void BluetoothHandler::setPairable(bool pairable) {
    if (!m_adapter) {
        return;
    }

    m_adapter->discoverable->set_value(pairable);
    m_adapter->pairable->set_value(pairable);
    Logger::instance()->info("Bluetooth adapter is now discoverable and pairable\n");
}

void BluetoothHandler::exportProfiles() {
    std::shared_ptr<DBus::ObjectProxy> bluezObject = m_connection->create_object_proxy(BLUEZ_BUS_NAME, BLUEZ_OBJECT_PATH);
    DBus::MethodProxy registerProfile = *(bluezObject->create_method<void(DBus::Path, std::string, DBus::Properties)>(INTERFACE_BLUEZ_PROFILE_MANAGER, "RegisterProfile"));

    // Register AA Wireless Profile
    m_aawProfile = AAWirelessProfile::create(AAWG_PROFILE_OBJECT_PATH);
    if (m_connection->register_object(m_aawProfile, DBus::ThreadForCalling::DispatcherThread) != DBus::RegistrationStatus::Success) {
        Logger::instance()->info("Failed to register AA Wireless profile\n");
    }

    registerProfile(AAWG_PROFILE_OBJECT_PATH, AAWG_PROFILE_UUID, {
        {"Name", DBus::Variant("AA Wireless")},
        {"Role", DBus::Variant("server")},
        {"Channel", DBus::Variant(uint16_t(8))},
    });
    Logger::instance()->info("Bluetooth AA Wireless profile active\n");

    if (Config::instance()->getConnectionStrategy() != ConnectionStrategy::DONGLE_MODE) {
        // Register HSP Handset profile
        m_hspProfile = HSPHSProfile::create(HSP_HS_PROFILE_OBJECT_PATH);
        if (m_connection->register_object(m_hspProfile, DBus::ThreadForCalling::DispatcherThread) != DBus::RegistrationStatus::Success) {
            Logger::instance()->info("Failed to register HSP Handset profile\n");
        }
        registerProfile(HSP_HS_PROFILE_OBJECT_PATH, HSP_HS_UUID, {
            {"Name", DBus::Variant("HSP HS")},
        });
        Logger::instance()->info("HSP Handset profile active\n");
    }
}

void BluetoothHandler::startAdvertising() {
    if (!m_adapter) {
        return;
    }

    // Register Advertisement Object
    m_leAdvertisement = BLEAdvertisement::create(LE_ADVERTISEMENT_OBJECT_PATH);

    m_leAdvertisement->type->set_value("peripheral");
    m_leAdvertisement->serviceUUIDs->set_value(std::vector<std::string>{AAWG_PROFILE_UUID});
    m_leAdvertisement->localName->set_value(m_adapterAlias);

    if (m_connection->register_object(m_leAdvertisement, DBus::ThreadForCalling::DispatcherThread) != DBus::RegistrationStatus::Success) {
        Logger::instance()->info("Failed to register BLE Advertisement\n");
    }

    (*m_adapter->registerAdvertisement)(LE_ADVERTISEMENT_OBJECT_PATH, {});
    Logger::instance()->info("BLE Advertisement started\n");
}

void BluetoothHandler::stopAdvertising() {
    if (!m_adapter) {
        return;
    }

    (*m_adapter->unregisterAdvertisement)(LE_ADVERTISEMENT_OBJECT_PATH);
    Logger::instance()->info("BLE Advertisement stopped\n");
}

void BluetoothHandler::connectDevice() {
    DBus::ManagedObjects objects = getBluezObjects();

    std::vector<std::string> device_paths;
    for (auto const& [path, interfaces]: objects) {
        for (auto const& [interface, properties]: interfaces) {
            if (interface == INTERFACE_BLUEZ_DEVICE) {
                device_paths.push_back(path);
            }
        }
    }

    if (!device_paths.size()) {
        Logger::instance()->info("Did not find any bluetooth devices\n");
        return;
    }

    if (m_preferredDeviceObjectPath.has_value()) {
        const std::string preferred = m_preferredDeviceObjectPath.value();
        std::stable_sort(device_paths.begin(), device_paths.end(), [&](const std::string& a, const std::string& b) {
            const bool aPref = (a == preferred);
            const bool bPref = (b == preferred);
            return aPref > bPref;
        });
    }

    const bool isDongleMode = (Config::instance()->getConnectionStrategy() == ConnectionStrategy::DONGLE_MODE);
    Logger::instance()->info("Found %d bluetooth devices\n", device_paths.size());

    for (const std::string &device_path: device_paths) {
        if (connectDeviceByObjectPath(device_path)) {
            if (!isDongleMode) {
                return;
            }
        }
    }

    if (!isDongleMode) {
        Logger::instance()->info("Failed to connect to any known bluetooth device\n");
    }
}

bool BluetoothHandler::connectDeviceByObjectPath(const std::string& objectPath) {
    const bool isDongleMode = (Config::instance()->getConnectionStrategy() == ConnectionStrategy::DONGLE_MODE);
    Logger::instance()->info("Trying to connect bluetooth device at path: %s\n", objectPath.c_str());

    std::shared_ptr<DBus::ObjectProxy> bluezDevice = m_connection->create_object_proxy(BLUEZ_BUS_NAME, objectPath);
    DBus::MethodProxy connectProfile = *(bluezDevice->create_method<void(std::string)>(INTERFACE_BLUEZ_DEVICE, "ConnectProfile"));
    DBus::MethodProxy disconnect = *(bluezDevice->create_method<void()>(INTERFACE_BLUEZ_DEVICE, "Disconnect"));

    std::shared_ptr<DBus::PropertyProxy<bool>> deviceConnected = bluezDevice->create_property<bool>(INTERFACE_BLUEZ_DEVICE, "Connected");

    try {
        if (deviceConnected && deviceConnected->value()) {
            Logger::instance()->info("Bluetooth device already connected, disconnecting\n");
            disconnect();
        }
        connectProfile(isDongleMode ? "" : HSP_AG_UUID);
        Logger::instance()->info("Bluetooth connected to the device\n");
        return true;
    } catch (DBus::Error& e) {
        if (!isDongleMode) {
            Logger::instance()->info("Failed to connect device at path: %s\n", objectPath.c_str());
        }
        return false;
    }
}

void BluetoothHandler::disconnectAllConnectedDevices() {
    DBus::ManagedObjects objects = getBluezObjects();

    for (auto const& [path, interfaces]: objects) {
        auto deviceIt = interfaces.find(INTERFACE_BLUEZ_DEVICE);
        if (deviceIt == interfaces.end()) {
            continue;
        }

        try {
            std::shared_ptr<DBus::ObjectProxy> bluezDevice = m_connection->create_object_proxy(BLUEZ_BUS_NAME, path);
            DBus::MethodProxy disconnect = *(bluezDevice->create_method<void()>(INTERFACE_BLUEZ_DEVICE, "Disconnect"));
            std::shared_ptr<DBus::PropertyProxy<bool>> deviceConnected = bluezDevice->create_property<bool>(INTERFACE_BLUEZ_DEVICE, "Connected");

            if (deviceConnected && deviceConnected->value()) {
                Logger::instance()->info("Disconnecting currently connected device at path: %s\n", path.c_str());
                disconnect();
            }
        } catch (DBus::Error& e) {
            // Best-effort.
        }
    }
}

std::vector<BluetoothDeviceInfo> BluetoothHandler::listDevices() {
    std::vector<BluetoothDeviceInfo> devices;
    if (!m_connection) {
        return devices;
    }

    DBus::ManagedObjects objects = getBluezObjects();
    for (auto const& [path, interfaces]: objects) {
        auto deviceIt = interfaces.find(INTERFACE_BLUEZ_DEVICE);
        if (deviceIt == interfaces.end()) {
            continue;
        }

        BluetoothDeviceInfo info;
        info.objectPath = path;

        try {
            std::shared_ptr<DBus::ObjectProxy> bluezDevice = m_connection->create_object_proxy(BLUEZ_BUS_NAME, path);
            auto addr = bluezDevice->create_property<std::string>(INTERFACE_BLUEZ_DEVICE, "Address");
            auto name = bluezDevice->create_property<std::string>(INTERFACE_BLUEZ_DEVICE, "Name");
            auto paired = bluezDevice->create_property<bool>(INTERFACE_BLUEZ_DEVICE, "Paired");
            auto trusted = bluezDevice->create_property<bool>(INTERFACE_BLUEZ_DEVICE, "Trusted");
            auto connected = bluezDevice->create_property<bool>(INTERFACE_BLUEZ_DEVICE, "Connected");

            if (addr) info.address = addr->value();
            if (name) info.name = name->value();
            if (paired) info.paired = paired->value();
            if (trusted) info.trusted = trusted->value();
            if (connected) info.connected = connected->value();
        } catch (DBus::Error& e) {
            // Skip unreadable device.
        }

        devices.push_back(info);
    }

    // Prefer paired devices first, then connected, then stable by path.
    std::stable_sort(devices.begin(), devices.end(), [](const BluetoothDeviceInfo& a, const BluetoothDeviceInfo& b) {
        if (a.paired != b.paired) return a.paired > b.paired;
        if (a.connected != b.connected) return a.connected > b.connected;
        return a.objectPath < b.objectPath;
    });

    return devices;
}

bool BluetoothHandler::switchToDevice(const std::string& selector) {
    if (!m_connection) {
        return false;
    }

    // Stop background retry loop to avoid concurrent DBus calls.
    stopConnectWithRetry();

    std::vector<BluetoothDeviceInfo> devices = listDevices();
    auto matches = [&](const BluetoothDeviceInfo& d) {
        if (selector.empty()) return false;
        if (d.objectPath == selector) return true;
        if (!d.address.empty() && d.address == selector) return true;
        if (!d.name.empty() && d.name == selector) return true;

        // Case-insensitive name match (best-effort)
        std::string a = d.name, b = selector;
        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        return !a.empty() && a == b;
    };

    auto it = std::find_if(devices.begin(), devices.end(), matches);
    if (it == devices.end()) {
        Logger::instance()->info("switchToDevice: no matching device for selector: %s\n", selector.c_str());
        return false;
    }

    m_preferredDeviceObjectPath = it->objectPath;

    // Disconnect existing connection(s) first to force handover.
    disconnectAllConnectedDevices();

    const bool ok = connectDeviceByObjectPath(it->objectPath);
    if (!ok) {
        Logger::instance()->info("switchToDevice: connect failed for %s (%s)\n", it->objectPath.c_str(), it->address.c_str());
    }
    return ok;
}

void BluetoothHandler::disconnectAll() {
    if (!m_connection) {
        return;
    }

    stopConnectWithRetry();
    m_preferredDeviceObjectPath.reset();
    disconnectAllConnectedDevices();
}

void BluetoothHandler::retryConnectLoop() {
    bool should_exit = false;
    std::future<void> connectWithRetryFuture = connectWithRetryPromise->get_future();

    while (!should_exit) {
        connectDevice();

        if (connectWithRetryFuture.wait_for(std::chrono::seconds(20)) == std::future_status::ready) {
            should_exit = true;
            connectWithRetryPromise = nullptr;
        }
    }

    if (Config::instance()->getConnectionStrategy() != ConnectionStrategy::DONGLE_MODE) {
        BluetoothHandler::instance().powerOff();
    }
}

void BluetoothHandler::init() {
    // DBus::set_logging_function( DBus::log_std_err );
    // DBus::set_log_level( SL_TRACE );

    m_dispatcher = DBus::StandaloneDispatcher::create();
    m_connection = m_dispatcher->create_connection( DBus::BusType::SYSTEM );

    const std::string configuredBluetoothName = Config::instance()->getBluetoothName();
    if (!configuredBluetoothName.empty()) {
        m_adapterAlias = configuredBluetoothName;
    } else {
        std::string adapterAliasPrefix = (Config::instance()->getConnectionStrategy() == ConnectionStrategy::DONGLE_MODE) ? ADAPTER_ALIAS_DONGLE_PREFIX : ADAPTER_ALIAS_PREFIX;
        m_adapterAlias = adapterAliasPrefix + Config::instance()->getUniqueSuffix();
    }

    initAdapter();
    exportProfiles();
}

void BluetoothHandler::powerOn() {
    if (!m_adapter) {
        return;
    }

    setPower(true);
    setPairable(true);

    if (Config::instance()->getConnectionStrategy() == ConnectionStrategy::DONGLE_MODE) {
        startAdvertising();
    }
}

std::optional<std::thread> BluetoothHandler::connectWithRetry() {
    if (!m_adapter) {
        return std::nullopt;
    }

    connectWithRetryPromise = std::make_shared<std::promise<void>>();
    return std::thread(&BluetoothHandler::retryConnectLoop, this);
}

void BluetoothHandler::stopConnectWithRetry() {
    if (connectWithRetryPromise) {
        connectWithRetryPromise->set_value();
    }
}

void BluetoothHandler::powerOff() {
    if (!m_adapter) {
        return;
    }

    if (Config::instance()->getConnectionStrategy() == ConnectionStrategy::DONGLE_MODE) {
        stopAdvertising();
    }
    setPower(false);
}