#include "MainWindow.h"
#include "./ui_mainwindow.h"
#include <iostream>
#include <nabto_client.hpp>
#include <nabto/nabto_client_experimental.h>
#include <map>

#include "pairing.hpp"
#include "config.hpp"
#include "timestamp.hpp"
#include "iam.hpp"
#include "iam_interactive.hpp"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
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
    auto context = nabto::client::Context::create();
    std::string sct = ui -> lineEdit -> text().toStdString();
    std::string str = string_pair(context, sct);
    ui->label->setText(QString::fromStdString(str));
    update_bookmarks();

}


void MainWindow::update_bookmarks() {
    std::map<int, Configuration::DeviceInfo> services;
    services = Configuration::PrintBookmarks();
    int index = 0;
    ui -> listWidget-> clear();
    for (const auto& bookmark : services) {
        ui -> listWidget -> addItem(bookmark.second.deviceId_.c_str());
    }
}


void MainWindow::on_pushButton_2_clicked()
{
    try {
        if (ui -> listWidget -> selectedItems().size() != 0) {
            std::string text = ui -> listWidget-> currentItem() -> text().toStdString();
            std::cout << text << std::endl;
        }
    } catch (std::exception e) {
        std::cerr<<"Error";
    }
}