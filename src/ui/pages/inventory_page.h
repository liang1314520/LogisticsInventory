#pragma once

#include "core/result.h"

#include <QWidget>

class InventoryQueryService;
class DateTimeInput;
class QLabel;
class QTableWidget;

class InventoryPage final : public QWidget {
    Q_OBJECT
public:
    explicit InventoryPage(InventoryQueryService &service, QWidget *parent=nullptr);
    Result<bool> refresh();
private:
    Result<bool> refreshUsage();
    void showError(const ServiceError &error);
    InventoryQueryService &service_;
    QTableWidget *batches_=nullptr;
    QTableWidget *locations_=nullptr;
    QTableWidget *shipments_=nullptr;
    DateTimeInput *from_=nullptr;
    DateTimeInput *to_=nullptr;
    QLabel *error_=nullptr;
};
