#include <stdio.h>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <arpa/inet.h>

#include "common.h"
#include "bluetoothHandler.h"
#include "bluetoothProfiles.h"

#include <google/protobuf/message_lite.h>
#include "proto/WifiStartRequest.pb.h"
#include "proto/WifiInfoResponse.pb.h"

static constexpr const char* INTERFACE_BLUEZ_PROFILE = "org.bluez.Profile1";

static ssize_t readFully(int fd, unsigned char *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r < 0) {
            return r;
        }
        if (r == 0) {
            return (ssize_t)got;
        }
        got += (size_t)r;
    }
    return (ssize_t)n;
}

#pragma region BluezProfile
BluezProfile::BluezProfile(DBus::Path path): DBus::Object(path) {
    this->create_method<void(void)>(INTERFACE_BLUEZ_PROFILE, "Release", sigc::mem_fun(*this, &BluezProfile::Release));
    this->create_method<void(DBus::Path, std::shared_ptr<DBus::FileDescriptor>, DBus::Properties)>(INTERFACE_BLUEZ_PROFILE ,"NewConnection", sigc::mem_fun(*this, &BluezProfile::NewConnection));
    this->create_method<void(DBus::Path)>(INTERFACE_BLUEZ_PROFILE, "RequestDisconnection", sigc::mem_fun(*this, &BluezProfile::RequestDisconnection));
}
#pragma endregion BluezProfile


#pragma region AAWirelessLauncher
class AAWirelessLauncher {
public:
    AAWirelessLauncher(int fd): m_fd(fd) {};

    void launch() {
        // Make fd blocking
        int fd_flags = fcntl(m_fd, F_GETFL);
        fcntl(m_fd, F_SETFL, fd_flags & ~O_NONBLOCK);

        WifiInfo wifiInfo = Config::instance()->getWifiInfo();

        Logger::instance()->info("Sending WifiStartRequest (ip: %s, port: %d)\n", wifiInfo.ipAddress.c_str(), wifiInfo.port);
        WifiStartRequest wifiStartRequest;
        wifiStartRequest.set_ip_address(wifiInfo.ipAddress);
        wifiStartRequest.set_port(wifiInfo.port);

        SendMessage(MessageId::WifiStartRequest, &wifiStartRequest);

        MessageId messageId = ReadMessage();

        if (messageId != MessageId::WifiInfoRequest) {
            Logger::instance()->info("Expected WifiInfoRequest, got %s (%d). Abort.\n", MessageName(messageId), messageId);
            return;
        }

        Logger::instance()->info("Sending WifiInfoResponse (ssid: %s, bssid: %s)\n", wifiInfo.ssid.c_str(), wifiInfo.bssid.c_str());
        WifiInfoResponse wifiInfoResponse;
        wifiInfoResponse.set_ssid(wifiInfo.ssid);
        wifiInfoResponse.set_key(wifiInfo.key);
        wifiInfoResponse.set_bssid(wifiInfo.bssid);
        wifiInfoResponse.set_security_mode(wifiInfo.securityMode);
        wifiInfoResponse.set_access_point_type(wifiInfo.accessPointType);

        SendMessage(MessageId::WifiInfoResponse, &wifiInfoResponse);

        // After WifiInfoResponse the phone may send several messages before WifiStartResponse (7):
        // - WifiVersionRequest / WifiVersionResponse exchange (4, 5)
        // - One or more WifiConnectStatus (6) while Wi‑Fi is still associating (see upstream
        //   WirelessAndroidAutoDongle issue #349 — multiple status updates are normal).
        // Read and discard until WifiStartResponse. A fixed second ReadMessage() would block
        // forever when only (7) is sent, and would fail if (6) appears zero or many times.
        while (true) {
            MessageId postInfoId = ReadMessage();
            if (postInfoId == MessageId::Invalid) {
                return;
            }
            if (postInfoId == MessageId::WifiStartResponse) {
                break;
            }
        }
    }

private:
    enum class MessageId {
        Invalid = -1,
        WifiStartRequest = 1,
        WifiInfoRequest = 2,
        WifiInfoResponse = 3,
        WifiVersionRequest = 4,
        WifiVersionResponse = 5,
        WifiConnectStatus = 6,
        WifiStartResponse = 7,
    };
    std::string MessageName(MessageId messageId) {
        switch (messageId) {
            case MessageId::WifiStartRequest:
                return "WifiStartRequest";
            case MessageId::WifiInfoRequest:
                return "WifiInfoRequest";
            case MessageId::WifiInfoResponse:
                return "WifiInfoResponse";
            case MessageId::WifiVersionRequest:
                return "WifiVersionRequest";
            case MessageId::WifiVersionResponse:
                return "WifiVersionResponse";
            case MessageId::WifiConnectStatus:
                return "WifiConnectStatus";
            case MessageId::WifiStartResponse:
                return "WifiStartResponse";
            default:
                return "UNKNOWN";
        }
    }

    void SendMessage(MessageId messageId, google::protobuf::MessageLite* message) {
        uint16_t messageSize = (uint16_t)message->ByteSizeLong();
        uint16_t length = messageSize + 4;

        unsigned char* buffer = new unsigned char[length];

        uint16_t networkShort = 0;
        networkShort = htons(messageSize);
        memcpy(buffer, &networkShort, sizeof(networkShort));

        networkShort = htons(static_cast<uint16_t>(messageId));
        memcpy(buffer + 2, &networkShort, sizeof(networkShort));

        message->SerializeToArray(buffer + 4, messageSize);

        ssize_t wrote = write(m_fd, buffer, length);
        if (wrote < 0) {
            Logger::instance()->info("Error sending %s, messageId: %d\n", MessageName(messageId).c_str(), messageId);
        }
        else {
            Logger::instance()->info("Sent %s, messageId: %d, wrote %d bytes\n", MessageName(messageId).c_str(), messageId, wrote);
        }

        delete[] buffer;
    }

    MessageId ReadMessage() {
        unsigned char wire[2];
        ssize_t readBytes;

        readBytes = readFully(m_fd, wire, 2);
        if (readBytes != 2) {
            Logger::instance()->info("Error reading length, read bytes: %zd, errno: %s\n", readBytes, strerror(errno));
            return MessageId::Invalid;
        }
        uint16_t lengthRaw = 0;
        memcpy(&lengthRaw, wire, 2);
        uint16_t length = ntohs(lengthRaw);

        readBytes = readFully(m_fd, wire, 2);
        if (readBytes != 2) {
            Logger::instance()->info("Error reading message id, read bytes: %zd, errno: %s\n", readBytes, strerror(errno));
            return MessageId::Invalid;
        }
        uint16_t idRaw = 0;
        memcpy(&idRaw, wire, 2);
        MessageId messageId = static_cast<MessageId>(ntohs(idRaw));

        if (length > 0) {
            unsigned char *buffer = new unsigned char[length];
            readBytes = readFully(m_fd, buffer, length);
            delete[] buffer;
            if (readBytes != (ssize_t)length) {
                Logger::instance()->info("Error reading message body for %s, expected %u got %zd errno: %s\n",
                    MessageName(messageId).c_str(), (unsigned)length, readBytes, strerror(errno));
                return MessageId::Invalid;
            }
        }

        Logger::instance()->info("Read %s. length: %u, messageId: %d\n", MessageName(messageId).c_str(),
            (unsigned)length, static_cast<int>(messageId));

        return messageId;
    }

    int m_fd;
};
#pragma endregion AAWirelessLauncher

#pragma region AAWirelessProfile
void AAWirelessProfile::Release() {
    Logger::instance()->info("AA Wireless Release\n");
}

void AAWirelessProfile::NewConnection(DBus::Path path, std::shared_ptr<DBus::FileDescriptor> fd, DBus::Properties fdProperties) {
    Logger::instance()->info("AA Wireless NewConnection\n");
    Logger::instance()->info("Path: %s, fd: %d\n", path.c_str(), fd->descriptor());

    // BlueZ expects a timely D-Bus method return from NewConnection. Running the RFCOMM
    // handshake here caused org.freedesktop.DBus.Error.NoReply (~25s) while the phone
    // finished Wi‑Fi; use a dedicated fd copy on a worker thread.
    int rawFd = fd->descriptor();
    int fdCopy = dup(rawFd);
    if (fdCopy < 0) {
        Logger::instance()->info("AA Wireless NewConnection: dup failed: %s\n", strerror(errno));
        return;
    }
    std::thread([fdCopy]() {
        AAWirelessLauncher(fdCopy).launch();
        close(fdCopy);
        Logger::instance()->info("Bluetooth launch sequence completed\n");
    }).detach();
}

void AAWirelessProfile::RequestDisconnection(DBus::Path path) {
    Logger::instance()->info("AA Wireless RequestDisconnection\n");
    Logger::instance()->info("Path: %s\n", path.c_str());
}

AAWirelessProfile::AAWirelessProfile(DBus::Path path): BluezProfile(path) {};

/* static */ std::shared_ptr<AAWirelessProfile> AAWirelessProfile::create(DBus::Path path) {
    return std::shared_ptr<AAWirelessProfile>(new AAWirelessProfile(path));
}
#pragma endregion AAWirelessProfile

#pragma region HSPHSProfile
void HSPHSProfile::Release() {
    Logger::instance()->info("HSP HS Release\n");
}

void HSPHSProfile::NewConnection(DBus::Path path, std::shared_ptr<DBus::FileDescriptor> fd, DBus::Properties fdProperties) {
    Logger::instance()->info("HSP HS NewConnection\n");
    Logger::instance()->info("Path: %s, fd: %d\n", path.c_str(), fd->descriptor());
}

void HSPHSProfile::RequestDisconnection(DBus::Path path) {
    Logger::instance()->info("HSP HS RequestDisconnection\n");
    Logger::instance()->info("Path: %s\n", path.c_str());
}

HSPHSProfile::HSPHSProfile(DBus::Path path): BluezProfile(path) {};

/* static */ std::shared_ptr<HSPHSProfile> HSPHSProfile::create(DBus::Path path) {
    return std::shared_ptr<HSPHSProfile>(new HSPHSProfile(path));
}
#pragma endregion HSPHSProfile
