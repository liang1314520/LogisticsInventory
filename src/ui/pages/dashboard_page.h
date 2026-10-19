#pragma once

#include "core/result.h"

#include <QWidget>

class InventoryQueryService;
class QLabel;
class QTableWidget;

class DashboardPage final : public QWidget {
    Q_OBJECT
public:
    explicit DashboardPage(InventoryQueryService &service, QWidget *parent = nullptr);
    Result<bool> refresh();
    ServiceError lastError() const;

private:
    InventoryQueryService &service_;
    QLabel *total_ = nullptr;
    QLabel *company_ = nullptr;
    QLabel *factory_ = nullptr;
    QLabel *inbound_ = nullptr;
    QLabel *outbound_ = nullptr;
    QLabel *returned_ = nullptr;
    QTableWidget *recent_ = nullptr;
    ServiceError lastError_;
};
