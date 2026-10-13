#include "application.h"

#include "db/database.h"
#include "repositories/document_repository.h"
#include "repositories/inventory_repository.h"
#include "repositories/master_data_repository.h"
#include "services/inbound_service.h"
#include "services/inventory_query_service.h"
#include "services/logistics_service.h"
#include "services/master_data_service.h"
#include "ui/main_window.h"

#include <QCoreApplication>
#include <QDir>
#include <QUuid>

Application::Application() = default;
Application::~Application() = default;

bool Application::initialize(QString *error)
{
    const QString directory = QCoreApplication::applicationDirPath();
    if (directory.isEmpty()) {
        if (error) *error = QStringLiteral("无法确定应用程序所在目录");
        return false;
    }
    database_ = std::make_unique<Database>(QStringLiteral("application-%1").arg(
        QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!database_->open(QDir(directory).filePath(QStringLiteral("logistics.sqlite")), error)
        || !database_->migrate(error)) return false;
    masterRepository_ = std::make_unique<MasterDataRepository>(*database_);
    documentRepository_ = std::make_unique<DocumentRepository>(*database_);
    inventoryRepository_ = std::make_unique<InventoryRepository>(*database_);
    masterService_ = std::make_unique<MasterDataService>(*masterRepository_);
    inboundService_ = std::make_unique<InboundService>(*database_, *masterRepository_,
        *documentRepository_, *inventoryRepository_);
    logisticsService_ = std::make_unique<LogisticsService>(*database_, *masterRepository_,
        *documentRepository_, *inventoryRepository_);
    queryService_ = std::make_unique<InventoryQueryService>(*database_);
    mainWindow_ = std::make_unique<MainWindow>(*masterService_, *queryService_, *inboundService_, *logisticsService_);
    return true;
}

MainWindow *Application::mainWindow() const { return mainWindow_.get(); }
