#pragma once

#include "core/result.h"
#include "services/report_service.h"

#include <QWidget>
#include <functional>

class InventoryQueryService;
class ReportService;
class QComboBox;
class QDateEdit;
class QLabel;

class ReportsPage final : public QWidget {
    Q_OBJECT
public:
    using SavePathCallback=std::function<QString(QWidget *)>;
    ReportsPage(InventoryQueryService &queries,ReportService &reports,QWidget *parent=nullptr);
    Result<bool> refresh();
    void setSavePathCallback(SavePathCallback callback);
    bool exportCurrent();
private:
    Result<CsvReport> buildReport() const;
    void showError(const ServiceError &error);
    InventoryQueryService &queries_;
    ReportService &reports_;
    QComboBox *report_=nullptr;
    QDateEdit *from_=nullptr;
    QDateEdit *to_=nullptr;
    QLabel *error_=nullptr;
    SavePathCallback savePath_;
};
