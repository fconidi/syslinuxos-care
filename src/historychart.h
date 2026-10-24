#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class HistoryChart : public QWidget {
public:
    explicit HistoryChart(const QString &cpuLabel, const QString &memoryLabel,
                          QWidget *parent = nullptr);
    void appendSample(double cpuPercent, double memoryPercent);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString cpuLabel_;
    QString memoryLabel_;
    QVector<double> cpu_;
    QVector<double> memory_;
};
