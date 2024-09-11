#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <3rdparty/nlohmann/json.hpp>
#include "pairing.hpp"
#include "config.hpp"
#include "timestamp.hpp"
#include "iam.hpp"
#include "iam_interactive.hpp"
#include <nabto_client.hpp>
#include <nabto/nabto_client_experimental.h>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_pushButton_clicked();
    void on_pushButton_2_clicked();
    void update_bookmarks();
    void update_services_list(std::map<std::string, nlohmann::json> servs);
    void on_pushButton_3_clicked();
    void on_pushButton4_clicked();
    std::string tcptunnel(std::vector<std::string> services);

private:
    Ui::MainWindow *ui;
    std::shared_ptr<nabto::client::Context> context;
    std::unique_ptr<Configuration::DeviceInfo> Device;
    int SelectedBookmark;
    std::shared_ptr<nabto::client::Connection> connection;
    std::vector<std::shared_ptr<nabto::client::TcpTunnel> > tunnels;

};
#endif // MAINWINDOW_H
