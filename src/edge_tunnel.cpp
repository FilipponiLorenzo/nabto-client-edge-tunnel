#include <nabto_client.hpp>
#include <nabto/nabto_client_experimental.h>
#include <map>

#include "pairing.hpp"
#include "config.hpp"
#include "timestamp.hpp"
#include "iam.hpp"
#include "iam_interactive.hpp"
#include "version.hpp"
#include <list>
#include <vector>
#include <3rdparty/cxxopts.hpp>
#include <3rdparty/nlohmann/json.hpp>
#include <iostream>

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <thread>
#include <future>
#include "MainWindow.h"

using json = nlohmann::json;

const std::string appName = "edge_tunnel_client";

enum {
  COAP_CONTENT_FORMAT_APPLICATION_CBOR = 60
};

// TODO reconnect when connection is closed.

std::string generalHelp = R"(This client application is designed to be used with a tcp tunnel
device application. The functionality of the system is to enable
tunnelling of TCP connections over the internet. The system allows a
TCP client on the client side to connect to a TCP service on the
device side. On the client side a TCP listener is created which
listens for connections to localhost:<local-port>, when a TCP
connection is made from an application on the client side to
localhost:<local-port> the TCP connection is tunnelled to the service
on the device.

Example usage based on ssh:

 0. Run a tcp tunnel device on a system with an ssh server.
 1. Pair the client with the device. edge_tunnel_client --pair
 2. Create a tunnel to the SSH service on the device. edge_tunnel_client --service ssh --local-port <port>
 3. Connect to the SSH server through the tunnel. On the client side: ssh 127.0.0.1 -p <port>.
    A SSH connection is now opened to the ssh server running on the device.
)";

void PrintGeneralHelp()
{
    std::cout << generalHelp << std::endl;
}

class MyLogger : public nabto::client::Logger
{
 public:
    void log(nabto::client::LogMessage message) {
        std::cout << time_in_HH_MM_SS_MMM() << " [" << message.getSeverity() << "] - " << message.getMessage() << std::endl;
    }
};

std::shared_ptr<nabto::client::Connection> connection_;

void signalHandler(int s){
    printf("Caught signal %d\n",s);
    if (connection_) {
        connection_->close()->waitForResult();
    }
}

static void printMissingClientConfig(const std::string& filename)
{
    std::cerr
        << "The example is missing the client configuration file (" << filename << ")." << std::endl
        << "The client configuration file is a json file which can be" << std::endl
        << "used to change the server URL used for remote connections." << std::endl
        << "In normal scenarios, the file should simply contain an" << std::endl
        << "empty json document:"
        << "{" << std::endl
        << "}" <<std::endl;

}

class CloseListener : public nabto::client::ConnectionEventsCallback {
 public:

    CloseListener() {
    }
    void onEvent(int event) {
        if (event == NABTO_CLIENT_CONNECTION_EVENT_CLOSED) {
            std::cout << "Connection closed, closing application" << std::endl;
            promise_.set_value();
            return;
        }
    }

    void waitForClose() {
        auto future = promise_.get_future();
        future.get();
    }

 private:
    std::promise<void> promise_;
};

void handleFingerprintMismatch(std::shared_ptr<nabto::client::Connection> connection, Configuration::DeviceInfo device)
{
    IAM::IAMError ec;
    std::unique_ptr<IAM::PairingInfo> pairingInfo;
    std::tie(ec, pairingInfo) = IAM::get_pairing_info(connection);
    if (ec.ok()) {
        if (pairingInfo->getProductId() != device.getProductId()) {
            std::cerr << "The Product ID of the connected device (" <<  pairingInfo->getProductId() << ") does not match the Product ID for the bookmark " << device.getFriendlyName() << std::endl;
        } else if (pairingInfo->getDeviceId() != device.getDeviceId()) {
            std::cerr << "The Device ID of the connected device (" <<  pairingInfo->getDeviceId() << ") does not match the Device ID for the bookmark " << device.getFriendlyName() << std::endl;
        } else {
            std::cerr << "The public key of the device does not match the public key in the pairing. Repair the device with the client." << std::endl;
        }
    } else {
        // should not happen
        ec.printError();
    }
}

std::shared_ptr<nabto::client::Connection> createConnection(std::shared_ptr<nabto::client::Context> context, Configuration::DeviceInfo device)
{
    auto Config = Configuration::GetConfigInfo();
    if (!Config) {
        printMissingClientConfig(Configuration::GetConfigFilePath());
        return nullptr;
    }

    auto connection = context->createConnection();
    connection->setProductId(device.getProductId());
    connection->setDeviceId(device.getDeviceId());
    connection->setApplicationName(appName);
    connection->setApplicationVersion(edge_tunnel_client_version());

    if (!device.getDirectCandidate().empty()) {
        connection->enableDirectCandidates();
        connection->addDirectCandidate(device.getDirectCandidate(), 5592);
        connection->endOfDirectCandidates();
    }

    std::string privateKey;
    if(!Configuration::GetPrivateKey(context, privateKey)) {
        return nullptr;
    }
    connection->setPrivateKey(privateKey);


    if (!Config->getServerUrl().empty()) {
        connection->setServerUrl(Config->getServerUrl());
    }

    connection->setServerConnectToken(device.getSct());

    try {
        connection->connect()->waitForResult();
    } catch (nabto::client::NabtoException& e) {
        if (e.status().getErrorCode() == nabto::client::Status::NO_CHANNELS) {
            auto localStatus = nabto::client::Status(connection->getLocalChannelErrorCode());
            auto remoteStatus = nabto::client::Status(connection->getRemoteChannelErrorCode());
            std::cerr << "Not Connected." << std::endl;
            std::cerr << " The Local status is: " << localStatus.getDescription() << std::endl;
            std::cerr << " The Remote status is: " << remoteStatus.getDescription() << std::endl;
        } else {
            std::cerr << "Connect failed " << e.what() << std::endl;
        }
        return nullptr;
    }

    try {
        if (connection->getDeviceFingerprint() != device.getDeviceFingerprint()) {
            handleFingerprintMismatch(connection, device);
            return nullptr;
        }
    } catch (...) {
        std::cerr << "Missing device fingerprint in state, pair with the device again" << std::endl;
        return nullptr;
    }

    // we are paired if the connection has a user in the device
    IAM::IAMError ec;
    std::unique_ptr<IAM::User> user;
    std::tie(ec, user) = IAM::get_me(connection);

    if (!user) {
        std::cerr << "The client is not paired with device, do the pairing again" << std::endl;
        return nullptr;
    }
    return connection;
}

static void get_service(std::shared_ptr<nabto::client::Connection> connection, const std::string& service);
static void print_service(const nlohmann::json& service);

bool list_services(std::shared_ptr<nabto::client::Connection> connection)
{
    auto coap = connection->createCoap("GET", "/tcp-tunnels/services");
    coap->execute()->waitForResult();
    if (coap->getResponseStatusCode() == 205 &&
        coap->getResponseContentFormat() == COAP_CONTENT_FORMAT_APPLICATION_CBOR)
    {
        auto cbor = coap->getResponsePayload();
        auto data = json::from_cbor(cbor);
        if (data.is_array()) {
            std::cout << "Available services ..." << std::endl;
            try {
                for (auto s : data) {
                    get_service(connection, s.get<std::string>());
                }
            } catch(std::exception& e) {
                std::cerr << "Failed to get services: " << e.what() << std::endl;
                return false;
            }
        }
        return true;
    } else {
        std::cerr << "could not get list of services" << std::endl;
        return false;
    }
}

void get_service(std::shared_ptr<nabto::client::Connection> connection, const std::string& service)
{
    auto coap = connection->createCoap("GET", "/tcp-tunnels/services/" + service);
    coap->execute()->waitForResult();
    if (coap->getResponseStatusCode() == 205 &&
        coap->getResponseContentFormat() == COAP_CONTENT_FORMAT_APPLICATION_CBOR)
    {
        auto cbor = coap->getResponsePayload();
        auto data = json::from_cbor(cbor);
        print_service(data);
    }
}

std::string constant_width_string(std::string in) {
    const size_t maxLength = 10;
    if (in.size() > maxLength) {
        return in;
    }
    in.append(maxLength - in.size(), ' ');
    return in;
}

void print_service(const nlohmann::json& service)
{
    std::string id = service["Id"].get<std::string>();
    std::string type = service["Type"].get<std::string>();
    std::string host = service["Host"].get<std::string>();
    uint16_t port = service["Port"].get<uint16_t>();
    std::cout << "Service: " << constant_width_string(id) << " Type: " << constant_width_string(type) << " Host: " << host << "  Port: " << port << std::endl;
}

bool split_in_service_and_port(const std::string& in, std::string& service, uint16_t& port)
{
    std::size_t colon = in.find_first_of(":");
    if (colon != std::string::npos) {
        service = in.substr(0,colon);
        std::string portStr = in.substr(colon+1);
        try {
            port = std::stoi(portStr);
        } catch (std::invalid_argument& e) {
            std::cerr << "the format for the service is not correct the string " << in << " cannot be parsed as service:port" << std::endl;
            return false;
        }
    } else {
        port = 0;
        service = in;
    }

    return true;
}

bool tcptunnel(std::shared_ptr<nabto::client::Connection> connection, std::vector<std::string> services)
{
    std::vector<std::shared_ptr<nabto::client::TcpTunnel> > tunnels;

    for (auto serviceAndPort : services) {
        std::string service;
        uint16_t localPort;
        if (!split_in_service_and_port(serviceAndPort, service, localPort)) {
            return false;
        }

        std::shared_ptr<nabto::client::TcpTunnel> tunnel;
        try {
            tunnel = connection->createTcpTunnel();
            tunnel->open(service, localPort)->waitForResult();
        } catch (std::exception& e) {
            std::cout << "Failed to open a tunnel to " << serviceAndPort << " error: " << e.what() << std::endl;
            return false;
        }

        std::cout << "TCP Tunnel opened for the service " << service << " listening on the local port " << tunnel->getLocalPort() << std::endl;
        tunnels.push_back(tunnel);
    }

    // wait for ctrl c
    signal(SIGINT, &signalHandler);

    auto closeListener = std::make_shared<CloseListener>();
    connection->addEventsListener(closeListener);
    connection_ = connection;

    closeListener->waitForClose();
    connection->removeEventsListener(closeListener);
    connection_.reset();
    return true;
}

void printDeviceInfo(std::shared_ptr<IAM::PairingInfo> pi)
{
    auto ms = pi->getModes();
    std::cout << "Successfully retrieved device info:" << std::endl
              << "# Product ID   : " << pi->getProductId() << std::endl
              << "# Device ID    : " << pi->getDeviceId() << std::endl
              << "# Friendly Name: " << pi->getFriendlyName() << std::endl
              << "# Nabto Version: " << pi->getNabtoVersion() << std::endl;
    if (!pi->getAppName().empty()) {
        std::cout << "# App Name     : " << pi->getAppName() << std::endl;
    }
    if (!pi->getAppVersion().empty()) {
        std::cout << "# App Version  : " << pi->getAppVersion() << std::endl;
    }
    std::cout << "# Pairing modes:" << std::endl;
    for (auto mode : ms) {
        std::cout << "# * " << IAM::pairingModeAsString(mode) << std::endl;
    }
}

int main(int argc, char** argv){

    QApplication a(argc, argv);
    std::string homeDir = Configuration::getDefaultHomeDir();
    Configuration::InitializeWithDirectory(homeDir);
    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "testQt_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    MainWindow w;
    w.show();
    return a.exec();

    return 0;
}
