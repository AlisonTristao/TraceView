#include "theme/doublespinbox.h"

namespace traceview {

DoubleSpinBox::DoubleSpinBox(QWidget* parent) : QDoubleSpinBox(parent) {
    setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    setKeyboardTracking(false);
}

QValidator::State DoubleSpinBox::validate(QString& input, int& pos) const {
    // Same length in and out, so `pos` stays where the user left it.
    input = normalizedDecimalSeparator(input);
    return QDoubleSpinBox::validate(input, pos);
}

double DoubleSpinBox::valueFromText(const QString& text) const {
    return QDoubleSpinBox::valueFromText(normalizedDecimalSeparator(text));
}

QString DoubleSpinBox::normalizedDecimalSeparator(const QString& text) const {
    const QString decimalPoint = locale().decimalPoint();
    if (decimalPoint.size() != 1) {
        return text;
    }
    const QChar point = decimalPoint.front();
    const QChar other = point == QLatin1Char('.') ? QLatin1Char(',') : QLatin1Char('.');
    // Only the number itself -- a prefix/suffix (a unit) keeps its own dots.
    const qsizetype start = text.startsWith(prefix()) ? prefix().size() : 0;
    const qsizetype end = text.endsWith(suffix()) ? text.size() - suffix().size() : text.size();
    QString normalized = text;
    for (qsizetype i = start; i < end; ++i) {
        if (normalized[i] == other) {
            normalized[i] = point;
        }
    }
    return normalized;
}

}  // namespace traceview
