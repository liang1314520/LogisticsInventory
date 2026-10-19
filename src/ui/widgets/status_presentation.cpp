#include "status_presentation.h"

#include "services/batch_workflow_service.h"

#include <QBrush>
#include <QColor>
#include <QLabel>
#include <QTableWidgetItem>

namespace StatusPresentation {
namespace {
struct Colors {
    QColor text;
    QColor background;
};

Colors colors(Tone tone)
{
    switch (tone) {
    case Tone::Success: return {QColor("#166534"), QColor("#dcfce7")};
    case Tone::Active: return {QColor("#1e40af"), QColor("#dbeafe")};
    case Tone::Attention: return {QColor("#92400e"), QColor("#fef3c7")};
    case Tone::Error: return {QColor("#991b1b"), QColor("#fee2e2")};
    case Tone::Neutral: return {QColor("#374151"), QColor("#e5e7eb")};
    }
    return {QColor("#374151"), QColor("#e5e7eb")};
}
}

Tone tone(DocumentStatus status)
{
    switch (status) {
    case DocumentStatus::Confirmed: return Tone::Success;
    case DocumentStatus::Draft: return Tone::Active;
    case DocumentStatus::Cancelled: return Tone::Neutral;
    }
    return Tone::Neutral;
}

Tone tone(WorkflowStepStatus status)
{
    switch (status) {
    case WorkflowStepStatus::Completed: return Tone::Success;
    case WorkflowStepStatus::InProgress: return Tone::Active;
    case WorkflowStepStatus::NeedsAction: return Tone::Attention;
    case WorkflowStepStatus::NotStarted:
    case WorkflowStepStatus::NotApplicable: return Tone::Neutral;
    }
    return Tone::Neutral;
}

QString documentText(DocumentStatus status)
{
    switch (status) {
    case DocumentStatus::Confirmed: return QStringLiteral("已确认");
    case DocumentStatus::Cancelled: return QStringLiteral("已取消");
    case DocumentStatus::Draft: return QStringLiteral("草稿");
    }
    return {};
}

void apply(QTableWidgetItem *item, Tone tone)
{
    const auto palette = colors(tone);
    item->setForeground(QBrush(palette.text));
    item->setBackground(QBrush(palette.background));
}

void apply(QLabel *label, Tone tone)
{
    const auto palette = colors(tone);
    label->setStyleSheet(QStringLiteral("color:%1; background-color:%2; padding:4px 8px; border-radius:4px;")
                            .arg(palette.text.name(), palette.background.name()));
}
}
