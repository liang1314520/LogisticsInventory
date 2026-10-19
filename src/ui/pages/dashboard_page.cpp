#include "dashboard_page.h"

#include "services/inventory_query_service.h"
#include "ui/widgets/layout_utils.h"

#include <QDate>
#include <QFormLayout>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

DashboardPage::DashboardPage(InventoryQueryService &service, QWidget *parent)
    : QWidget(parent), service_(service)
{
    setObjectName(QStringLiteral("dashboardPage"));
    auto *layout = new QVBoxLayout(this);
    auto *summary = new QFormLayout;
    const auto addValue = [summary](const QString &title, const QString &name) {
        auto *label = new QLabel(QStringLiteral("0"));
        label->setObjectName(name);
        summary->addRow(title, label);
        return label;
    };
    total_ = addValue(QStringLiteral("库存总量"), QStringLiteral("totalInventory"));
    company_ = addValue(QStringLiteral("公司库存"), QStringLiteral("companyInventory"));
    factory_ = addValue(QStringLiteral("工厂库存"), QStringLiteral("factoryInventory"));
    inbound_ = addValue(QStringLiteral("本月入库"), QStringLiteral("monthlyInbound"));
    outbound_ = addValue(QStringLiteral("本月对外出库"), QStringLiteral("monthlyOutbound"));
    returned_ = addValue(QStringLiteral("本月工厂退回（内部流转）"), QStringLiteral("monthlyReturned"));
    layout->addLayout(summary);
    recent_ = new QTableWidget(0, 6, this);
    recent_->setObjectName(QStringLiteral("recentRecords"));
    recent_->setHorizontalHeaderLabels({QStringLiteral("单号"), QStringLiteral("业务"),
        QStringLiteral("日期"), QStringLiteral("来源"), QStringLiteral("去向"), QStringLiteral("数量")});
    layout->addWidget(recent_);
    refresh();
}

Result<bool> DashboardPage::refresh()
{
    const auto result = service_.dashboardSummary(QDate::currentDate());
    if (!result.ok()) { lastError_ = result.error(); return Result<bool>::failure(lastError_); }
    lastError_ = {};
    const auto &value = result.value();
    total_->setText(QString::number(value.totalInventory));
    company_->setText(QString::number(value.companyInventory));
    factory_->setText(QString::number(value.factoryInventory));
    inbound_->setText(QString::number(value.monthlyInbound));
    outbound_->setText(QString::number(value.monthlyOutbound));
    returned_->setText(QString::number(value.monthlyReturned));
    recent_->clearContents();
    recent_->setRowCount(static_cast<int>(value.recentRecords.size()));
    for (int row = 0; row < value.recentRecords.size(); ++row) {
        const auto &record = value.recentRecords.at(row);
        const QStringList cells{record.sourceDocument, record.businessType,
            record.businessAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")), record.sourceName,
            record.destinationName, QString::number(record.quantity)};
        for (int column = 0; column < cells.size(); ++column)
            recent_->setItem(row, column, new QTableWidgetItem(cells.at(column)));
    }
    LayoutUtils::fitTableColumns(recent_);
    return Result<bool>::success(true);
}

ServiceError DashboardPage::lastError() const { return lastError_; }
