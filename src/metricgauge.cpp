#include "metricgauge.h"

#include <QPainter>
#include <QPalette>
#include <QLocale>

#include <algorithm>
#include <utility>

MetricGauge::MetricGauge(QString title, QColor accent, QWidget *parent)
    : QWidget(parent), title_(std::move(title)), accent_(accent) {
    setMinimumSize(150, 170);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setAccessibleName(title_);
}

void MetricGauge::setReading(double percent, const QString &detail) {
    percent_ = percent < 0 ? -1 : std::clamp(percent, 0.0, 100.0);
    detail_ = detail;
    const QString value = percent_ < 0 ? QStringLiteral("—")
        : QLocale().toString(percent_, 'f', 0) + "%";
    setAccessibleName(title_ + ": " + value + (detail_.isEmpty() ? QString{} : ", " + detail_));
    update();
}

QSize MetricGauge::sizeHint() const { return {190, 190}; }

void MetricGauge::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor foreground = palette().color(QPalette::WindowText);
    const QColor background = palette().color(QPalette::Window);
    QColor accent = accent_;
    if (background.lightness() < 120) accent = accent.lighter(125);
    QColor track = foreground;
    track.setAlphaF(0.13);

    const qreal diameter = std::min<qreal>({width() - 30.0, height() - 59.0, 132.0});
    const QRectF ring((width() - diameter) / 2.0, 8.0, diameter, diameter);
    QPen pen(track, 10, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pen);
    painter.drawArc(ring, 0, 360 * 16);
    if (percent_ >= 0) {
        pen.setColor(accent);
        painter.setPen(pen);
        painter.drawArc(ring, 90 * 16, -qRound(percent_ * 360.0 / 100.0 * 16.0));
    }

    QFont valueFont = font();
    valueFont.setPointSize(22);
    valueFont.setWeight(QFont::DemiBold);
    painter.setFont(valueFont);
    painter.setPen(foreground);
    painter.drawText(ring.adjusted(10, 10, -10, -10), Qt::AlignCenter,
                     percent_ < 0 ? QStringLiteral("—") : QLocale().toString(percent_, 'f', 0) + "%");

    QFont labelFont = font();
    labelFont.setPointSize(11);
    labelFont.setWeight(QFont::DemiBold);
    painter.setFont(labelFont);
    painter.drawText(QRectF(4, ring.bottom() + 8, width() - 8, 24), Qt::AlignCenter, title_);

    QFont detailFont = font();
    detailFont.setPointSize(9);
    painter.setFont(detailFont);
    QColor secondary = foreground;
    secondary.setAlphaF(0.72);
    painter.setPen(secondary);
    painter.drawText(QRectF(4, ring.bottom() + 32, width() - 8, 20),
                     Qt::AlignCenter, painter.fontMetrics().elidedText(detail_, Qt::ElideMiddle, width() - 12));
}
