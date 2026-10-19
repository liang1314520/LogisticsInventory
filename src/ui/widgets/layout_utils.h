#pragma once

class QDialog;
class QSize;
class QSplitter;
class QString;
class QTableWidget;

namespace LayoutUtils {
void fitTableColumns(QTableWidget *table);
void fitDialogToContents(QDialog *dialog, const QSize &preferredSize);
void configureVerticalSplitter(QSplitter *splitter, const QString &hint);
}
