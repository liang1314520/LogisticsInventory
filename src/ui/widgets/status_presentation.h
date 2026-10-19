#pragma once

#include "core/models.h"

class QLabel;
class QTableWidgetItem;
enum class WorkflowStepStatus;

namespace StatusPresentation {
enum class Tone { Success, Active, Attention, Neutral, Error };

Tone tone(DocumentStatus status);
Tone tone(WorkflowStepStatus status);
QString documentText(DocumentStatus status);
void apply(QTableWidgetItem *item, Tone tone);
void apply(QLabel *label, Tone tone);
}
