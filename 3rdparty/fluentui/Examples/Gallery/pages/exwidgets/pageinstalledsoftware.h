#pragma once

#include <QObject>

class QTableWidget;

class PageInstalledSoftware : public QObject
{
    Q_OBJECT

public:
    explicit PageInstalledSoftware(QTableWidget *table, QObject *parent = nullptr);

    void initialize();

private:
    QTableWidget *m_table{nullptr};
};
