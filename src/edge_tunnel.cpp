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
