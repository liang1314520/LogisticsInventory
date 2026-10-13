#include "app/application.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication qtApplication(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("LogisticsInventory"));
    QApplication::setApplicationName(QStringLiteral("LogisticsInventory"));
    Application application;
    QString error;
    if (!application.initialize(&error)) {
        QMessageBox::critical(nullptr, QStringLiteral("启动失败"), error);
        return 1;
    }
    application.mainWindow()->show();
    return qtApplication.exec();
}
