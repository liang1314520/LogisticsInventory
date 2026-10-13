#pragma once

#include "core/models.h"
#include "core/result.h"

#include <functional>
#include <QWidget>

class MasterDataService;
class QLabel;
class QPushButton;
class QTabWidget;
class QTableWidget;

class MasterDataPage final : public QWidget {
    Q_OBJECT
public:
    explicit MasterDataPage(MasterDataService &service, QWidget *parent = nullptr);
    Result<bool> refresh();

    void setConfirmationCallback(std::function<bool(const QString &)> callback);

private slots:
    void addSelectedType();
    void editSelected();
    void toggleSelected();
    void deleteSelected();

private:
    QTableWidget *currentTable() const;
    EntityId selectedId() const;
    void showError(const ServiceError &error);
    bool confirmAction(const QString &text) const;

    MasterDataService &service_;
    QTabWidget *tabs_ = nullptr;
    QTableWidget *categories_ = nullptr;
    QTableWidget *productTypes_ = nullptr;
    QTableWidget *locations_ = nullptr;
    QLabel *error_ = nullptr;
    QPushButton *toggle_ = nullptr;
    std::function<bool(const QString &)> confirmation_;
};
