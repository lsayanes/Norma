#include <QApplication>
#include <QStyleFactory>
#include <QIcon>
#include <QFileInfo>
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

    // Icono de aplicacion y dock (bundle macOS: Contents/Resources; build local: ./resources)
    const QStringList iconCandidates = {
        QApplication::applicationDirPath() + "/../Resources/resources/Norma.png",
        QApplication::applicationDirPath() + "/../Resources/Norma.png",
        QApplication::applicationDirPath() + "/resources/Norma.png",
    };
    for (const QString &iconPath : iconCandidates)
    {
        if (QFileInfo::exists(iconPath))
        {
            app.setWindowIcon(QIcon(iconPath));
            break;
        }
    }
    Norma NormaApp;
    bool bCreated = NormaApp.create(appName);

    if(bCreated)
    {
        NormaApp.show();
        return app.exec();
    }

    return -1;
}
