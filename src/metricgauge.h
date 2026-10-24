#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

class MetricGauge : public QWidget {
public:
    explicit MetricGauge(QString title, QColor accent, QWidget *parent = nullptr);
    void setReading(double percent, const QString &detail);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString title_;
    QString detail_;
    QColor accent_;
    double percent_ = -1;
};
