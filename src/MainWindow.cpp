#include "MainWindow.h"
#include "./ui_mainwindow.h"
#include <nabto_client.hpp>
#include <nabto/nabto_client_experimental.h>


#include "pairing.hpp"
#include "config.hpp"
#include "timestamp.hpp"
#include "iam.hpp"
#include "iam_interactive.hpp"
#include "version.hpp"

#include <3rdparty/cxxopts.hpp>
#include <iostream>
#include <3rdparty/nlohmann/json.hpp>

#include <QtConcurrent>
#include <QFutureWatcher>
#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>

#include <map>
#include <list>
#include <vector>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <thread>
#include <future>

using json = nlohmann::json;

const std::string appName = "edge_tunnel_client";

enum {
  COAP_CONTENT_FORMAT_APPLICATION_CBOR = 60
};


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

static nlohmann::json get_service(std::shared_ptr<nabto::client::Connection> connection, const std::string& service);
static void print_service(const nlohmann::json& service);
static std::string extractId(const std::string& input);
static std::string extractNumericPart(const std::string& input);

std::map<std::string, nlohmann::json> list_services(std::shared_ptr<nabto::client::Connection> connection)
{   
    std::map<std::string, nlohmann::json> services;
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
                    nlohmann::json test = get_service(connection, s);
                    if (!test.is_null()) {
                        services.insert({s.get<std::string>(), test});
                    }
                }
            } catch(std::exception& e) {
                std::cerr << "Failed to get services: " << e.what() << std::endl;
                return {};
            }
        }
    }
    return services;;
}

nlohmann::json get_service(std::shared_ptr<nabto::client::Connection> connection, const std::string& service)
{
    std::cout<<service<<std::endl;
    auto coap = connection->createCoap("GET", "/tcp-tunnels/services/" + service);
    coap->execute()->waitForResult();
    if (coap->getResponseStatusCode() == 205 &&
        coap->getResponseContentFormat() == COAP_CONTENT_FORMAT_APPLICATION_CBOR)
    {
        auto cbor = coap->getResponsePayload();
        auto data = json::from_cbor(cbor);
        return data;
    }
    return nullptr;
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

std::string MainWindow::tcptunnel(std::vector<std::string> services)
{   
    std::string str="";
    for (auto serviceAndPort : services) {
        std::string service;
        uint16_t localPort;
        if (!split_in_service_and_port(serviceAndPort, service, localPort)) {
            return str;
        }
        
        std::shared_ptr<nabto::client::TcpTunnel> tunnel;
        try {
            tunnel = connection->createTcpTunnel();
            tunnel->open(service, localPort)->waitForResult();
        } catch (std::exception& e) {
            return "Failed to open a tunnel to " + serviceAndPort + " error: " + e.what();
        }

        str = "Tunnel opened for '" + service + "' -> local port " + std::to_string(tunnel->getLocalPort());
        tunnels.push_back(tunnel);
        ui -> listWidget_3 -> addItem(QString::fromStdString(str));
    }
    return str;
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


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow),
    context(),
    Device(),
    SelectedBookmark(),
    connection(),
    tunnels()
{
    ui->setupUi(this);
    update_bookmarks();

}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_pushButton_clicked()
{
    std::string sct = ui->lineEdit->text().toStdString();
    std::string str = string_pair(context, sct, "users");
    ui->label->setText(QString::fromStdString(str));
    update_bookmarks();
}



void MainWindow::update_bookmarks() {
    std::map<int, Configuration::DeviceInfo> services = Configuration::PrintBookmarks();
    ui -> listWidget-> clear();

    auto ctx = nabto::client::Context::create();

    auto connectToDevice = [this, ctx](const std::pair<int, Configuration::DeviceInfo>& bookmark) {
        try {
            auto d = Configuration::GetPairedDevice(bookmark.first);
            auto c = createConnection(ctx, *d);
            if (c != nullptr) {
                IAM::IAMError ec; 
                std::shared_ptr<IAM::PairingInfo> pi;
                std::tie(ec, pi) = IAM::get_pairing_info(c);
                auto str = "Name: " + pi -> getFriendlyName() + "\tId:" + bookmark.second.deviceId_;
                QMetaObject::invokeMethod(ui->listWidget, [this, str]() {
                    ui -> listWidget -> addItem(QString::fromStdString(str));
                }, Qt::QueuedConnection);
            }
        } catch (std::exception& e) {
            std::cout << "Failed to open a tunnel to " << bookmark.second.deviceId_.c_str() << std::endl;
        }
    };

    QList<QFuture<void>> futures;
    for (const auto& bookmark : services) {
        futures.append(QtConcurrent::run(connectToDevice, bookmark));
    }
}



void MainWindow::on_pushButton_2_clicked()
{   
    context = nabto::client::Context::create();
    try {
        if (ui -> listWidget -> selectedItems().size() != 0) {
            std::map<int, Configuration::DeviceInfo> services = Configuration::PrintBookmarks();
            std::string text = ui -> listWidget-> currentItem() -> text().toStdString();
            std::string id = extractId(text);
            for (const auto& pair : services) {
                std::string deviceInfo = pair.second.deviceId_.c_str();
                if (id == deviceInfo) {
                    Device = Configuration::GetPairedDevice(pair.first);
                    if (!Device) {
                        std::cerr << "The bookmark " << text << " does not exist" << std::endl;
                    } else {
                        std::cout << "Device Selected " << Device -> getFriendlyName()<<std::endl;;
                    }
                    connection = createConnection(context, *Device);

                    if (!connection) {
                        return ;
                    }
                
                    std::cout << "Connected to the device " << Device->getFriendlyName() << std::endl;
                    tunnels.clear();
                    auto servs = list_services(connection);
                    update_services_list(servs);;
                    ui -> listWidget_3 -> clear();
                }
            }
        }
    } catch (std::exception e) {
        std::cerr<<"Error " << e.what() << std::endl;
    }
}

void MainWindow::update_services_list(const std::map<std::string, nlohmann::json> servs) {
    ui->listWidget_2->clear();
    for (const auto& x : servs) {
        QString itemText;
        itemText += "Id: " + QString::fromStdString(x.first) + "\t";
        
        if (x.second.contains("Type")) {
            itemText += "Type: " + QString::fromStdString(x.second["Type"]);
        } else {
            itemText += "Type: Unknown";
        }
        
        itemText += "\tPort: ";
        if (x.second.contains("Port") && x.second["Port"].is_number_integer()) {
            auto port = x.second["Port"].get<uint16_t>();
            itemText += QString::fromStdString(std::to_string(port));
        } else {
            itemText += "Unknown";
        }
        
        ui-> listWidget_2->addItem(itemText);
    }
}


void MainWindow::on_pushButton_3_clicked(){
    if (ui -> listWidget_2 -> selectedItems().size() == 0) {
        return ;
    }
    std::string text = ui -> listWidget_2-> currentItem() -> text().toStdString();
    std::string ser = extractId(text);
    std::vector<std::string> services;
    services.push_back(ser);
    std::string string_tcptunnel = tcptunnel(services);
}


void MainWindow::on_pushButton4_clicked()
{
    if (ui -> listWidget_3 -> selectedItems().size() == 0) {
        return ;
    }
    QString val = ui -> listWidget_3 -> currentItem() -> text();;
    std::string port = extractNumericPart(val.toStdString());
    for (size_t i = 0; i < tunnels.size(); i++){
        if (std::to_string(tunnels[i] -> getLocalPort()) == port) {
            std::cout << "erfdeadfriend";
            tunnels[i] -> close() -> waitForResult();
            QListWidgetItem* itemToRemove = ui->listWidget_3->takeItem(ui->listWidget_3 -> currentRow());
            ui -> listWidget_3 -> removeItemWidget(itemToRemove);
            tunnels.erase(tunnels.begin() + i);
            std::cout << "erfdead";;
        } else {
            std::cout << "else";;
        }
    }
}

std::string extractId(const std::string& input) {
    size_t pos = input.find("Id:");
    
    if (pos != std::string::npos) {
        size_t start = pos + 3;
        while (start < input.size() && input[start] == ' ') {
            ++start;
        }
        
        size_t end = input.find_first_of(" \t", start);
        
        if (end == std::string::npos) {
            end = input.size();
        }
        
        return input.substr(start, end - start);
    }
    return "";
}


std::string extractNumericPart(const std::string& input) {
    std::regex re("\\d+");
    std::smatch match;
    if (std::regex_search(input, match, re)) {
        return match.str(0);
    }
    return "";
}







