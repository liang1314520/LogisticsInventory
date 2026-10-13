#pragma once

#include <memory>
#include <QString>

class Database;
class DocumentRepository;
class InventoryRepository;
class MasterDataRepository;
class InboundService;
class InventoryQueryService;
class LogisticsService;
class MasterDataService;
class MainWindow;

class Application final {
public:
    Application();
    ~Application();
    bool initialize(QString *error = nullptr);
    MainWindow *mainWindow() const;

private:
    std::unique_ptr<Database> database_;
    std::unique_ptr<MasterDataRepository> masterRepository_;
    std::unique_ptr<DocumentRepository> documentRepository_;
    std::unique_ptr<InventoryRepository> inventoryRepository_;
    std::unique_ptr<MasterDataService> masterService_;
    std::unique_ptr<InboundService> inboundService_;
    std::unique_ptr<LogisticsService> logisticsService_;
    std::unique_ptr<InventoryQueryService> queryService_;
    std::unique_ptr<MainWindow> mainWindow_;
};
