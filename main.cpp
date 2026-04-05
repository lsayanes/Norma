#include <QApplication>
#include <QStyleFactory>
#include <string>
#include <iostream>

#include "Norma.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const char *appName =  "Norma";
    
    app.setApplicationName(appName);
    app.setApplicationVersion("1.1");
    app.setOrganizationName("iDev - JalaGamaes");

    app.setStyle(QStyleFactory::create("Fusion"));

    Norma NormaApp;
    bool bCreated = NormaApp.create(appName);

    if(bCreated)
    {
        NormaApp.show();
        return app.exec();
    }

    return -1;
}
