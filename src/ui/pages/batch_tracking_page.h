#pragma once

#include "core/models.h"
#include "core/result.h"

#include <QWidget>

class InventoryQueryService;
class QComboBox;
class QLabel;
class QTableWidget;
class QShowEvent;
class QSplitter;

class BatchTrackingPage final : public QWidget {
    Q_OBJECT
public:
    explicit BatchTrackingPage(InventoryQueryService &service, QWidget *parent=nullptr);
    Result<bool> refresh();
    Result<bool> selectBatch(EntityId batchId);

protected:
    void showEvent(QShowEvent *event) override;

private:
    Result<bool> loadSelected();
    void showError(const ServiceError &error);
    InventoryQueryService &service_;
    QComboBox *selector_=nullptr;
    QLabel *summary_=nullptr;
    QSplitter *splitter_=nullptr;
    bool splitterInitialized_=false;
    QTableWidget *distribution_=nullptr;
    QTableWidget *timeline_=nullptr;
    QLabel *error_=nullptr;
};
