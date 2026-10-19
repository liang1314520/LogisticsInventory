#include "layout_utils.h"

#include <QAbstractItemDelegate>
#include <QBoxLayout>
#include <QComboBox>
#include <QDialog>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QScreen>
#include <QSplitter>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStyleOptionFrame>
#include <QStyleOptionViewItem>
#include <QTableWidget>

#include <algorithm>

namespace {
int inputContentWidth(QWidget *widget)
{
    const QFontMetrics metrics(widget->font());
    if (auto *combo = qobject_cast<QComboBox *>(widget)) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(1);
        combo->setSizePolicy(QSizePolicy::Expanding, combo->sizePolicy().verticalPolicy());
        combo->view()->setTextElideMode(Qt::ElideNone);
        combo->view()->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        int width = metrics.horizontalAdvance(combo->currentText());
        for (int index = 0; index < combo->count(); ++index) {
            const QString text = combo->itemText(index);
            const int iconWidth = combo->itemIcon(index).isNull() ? 0 : combo->iconSize().width() + 4;
            width = std::max(width, metrics.horizontalAdvance(text) + iconWidth);
            if (combo->itemData(index, Qt::ToolTipRole).toString() != text)
                combo->setItemData(index, text, Qt::ToolTipRole);
        }
        combo->setToolTip(combo->currentText());
        QStyleOptionComboBox option;
        option.initFrom(combo);
        option.editable = combo->isEditable();
        option.frame = combo->hasFrame();
        option.currentText = combo->currentText();
        option.iconSize = combo->iconSize();
        return combo->style()->sizeFromContents(QStyle::CT_ComboBox, &option,
            QSize(width + 8, metrics.height()), combo).width();
    }
    if (auto *edit = qobject_cast<QLineEdit *>(widget)) {
        const QString text = edit->text().isEmpty() ? edit->placeholderText() : edit->text();
        const QMargins margins = edit->textMargins();
        QStyleOptionFrame option;
        option.initFrom(edit);
        option.lineWidth = edit->hasFrame()
            ? edit->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, &option, edit) : 0;
        return edit->style()->sizeFromContents(QStyle::CT_LineEdit, &option,
            QSize(metrics.horizontalAdvance(text) + margins.left() + margins.right() + 8,
                  metrics.height()), edit).width();
    }
    return widget->sizeHint().width();
}

int contentWidth(QLayoutItem *item)
{
    if (!item)
        return 0;
    if (auto *layout = item->layout()) {
        int width = 0;
        if (auto *form = qobject_cast<QFormLayout *>(layout)) {
            int labelWidth = 0;
            int fieldWidth = 0;
            for (int row = 0; row < form->rowCount(); ++row) {
                if (auto *spanning = form->itemAt(row, QFormLayout::SpanningRole)) {
                    width = std::max(width, contentWidth(spanning));
                } else {
                    labelWidth = std::max(labelWidth, contentWidth(form->itemAt(row, QFormLayout::LabelRole)));
                    fieldWidth = std::max(fieldWidth, contentWidth(form->itemAt(row, QFormLayout::FieldRole)));
                }
            }
            width = std::max(width, labelWidth + fieldWidth + std::max(0, form->horizontalSpacing()));
        } else {
            const auto *box = qobject_cast<QBoxLayout *>(layout);
            const bool horizontal = box && (box->direction() == QBoxLayout::LeftToRight ||
                                             box->direction() == QBoxLayout::RightToLeft);
            for (int index = 0; index < layout->count(); ++index) {
                const int childWidth = contentWidth(layout->itemAt(index));
                width = horizontal ? width + childWidth : std::max(width, childWidth);
            }
            if (horizontal)
                width += std::max(0, layout->count() - 1) * std::max(0, layout->spacing());
        }
        const QMargins margins = layout->contentsMargins();
        return width + margins.left() + margins.right();
    }
    auto *widget = item->widget();
    if (!widget)
        return item->sizeHint().width();
    if (item->isEmpty())
        return 0;
    if (auto *table = qobject_cast<QTableWidget *>(widget)) {
        return table->horizontalHeader()->length() + table->verticalHeader()->sizeHint().width() +
            table->frameWidth() * 2 + table->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    }
    return inputContentWidth(widget);
}
}

void LayoutUtils::fitTableColumns(QTableWidget *table)
{
    auto *header = table->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    header->setResizeContentsPrecision(-1);
    table->resizeColumnsToContents();
    const int limit = std::max(header->minimumSectionSize(),
                              table->screen()->availableGeometry().width() * 2 / 3);
    QStyleOptionViewItem option;
    option.initFrom(table);
    option.font = table->font();
    for (int column = 0; column < table->columnCount(); ++column) {
        int width = header->sectionSizeHint(column);
        bool hasCellWidget = false;
        for (int row = 0; row < table->rowCount(); ++row) {
            if (auto *item = table->item(row, column)) {
                item->setToolTip(item->text());
                const QModelIndex index = table->model()->index(row, column);
                width = std::max(width, table->itemDelegateForIndex(index)->sizeHint(option, index).width() + 2);
            }
            if (auto *widget = table->cellWidget(row, column)) {
                hasCellWidget = true;
                width = std::max(width, inputContentWidth(widget) + 4);
            }
        }
        if (!hasCellWidget)
            width = std::max(width, table->columnWidth(column));
        table->setColumnWidth(column, std::clamp(width, header->minimumSectionSize(), limit));
    }
}

void LayoutUtils::fitDialogToContents(QDialog *dialog, const QSize &preferredSize)
{
    auto *layout = dialog->layout();
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    const int measuredWidth = contentWidth(layout);
    layout->activate();
    const QRect available = dialog->screen()->availableGeometry();
    const int frameWidth = std::max(0, dialog->frameGeometry().width() - dialog->width());
    const int frameHeight = std::max(0, dialog->frameGeometry().height() - dialog->height());
    const int maximumWidth = available.width() * 9 / 10 - frameWidth;
    const int maximumHeight = available.height() * 9 / 10 - frameHeight;
    const bool visible = dialog->isVisible();
    const int contentWidth = std::min(maximumWidth,
        std::max(measuredWidth, visible ? 0 : preferredSize.width()));
    const int width = std::min(visible ? available.width() - frameWidth : maximumWidth,
                               std::max(dialog->width(), contentWidth));
    const int height = visible
        ? std::min(available.height() - frameHeight, dialog->height())
        : std::min(maximumHeight, std::max({dialog->height(), preferredSize.height(),
            dialog->sizeHint().height(), layout->totalHeightForWidth(width)}));
    dialog->resize(width, height);
    if (dialog->isVisible()) {
        const QRect frame = dialog->frameGeometry();
        dialog->move(std::clamp(frame.x(), available.left(),
                               std::max(available.left(), available.right() - frame.width() + 1)),
                     std::clamp(frame.y(), available.top(),
                               std::max(available.top(), available.bottom() - frame.height() + 1)));
    }
}

void LayoutUtils::configureVerticalSplitter(QSplitter *splitter, const QString &hint)
{
    splitter->setOrientation(Qt::Vertical);
    splitter->setHandleWidth(10);
    splitter->setChildrenCollapsible(false);
    splitter->setOpaqueResize(true);
    splitter->setStyleSheet(QStringLiteral(
        "QSplitter#%1::handle:vertical { background: #d4dce5; border-top: 1px solid #aab6c3; "
        "border-bottom: 1px solid #aab6c3; } "
        "QSplitter#%1::handle:vertical:hover { background: #a8c7e8; }").arg(splitter->objectName()));
    splitter->handle(1)->setCursor(Qt::SplitVCursor);
    splitter->handle(1)->setToolTip(hint);
}
