#pragma once

#include <QVector>
#include <QWidget>

class HistoryChart : public QWidget {
public:
    explicit HistoryChart(QWidget *parent = nullptr);
    void appendSample(double cpuPercent, double memoryPercent);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<double> cpu_;
    QVector<double> memory_;
};
