#pragma once

#include <QDoubleSpinBox>

namespace traceview {

// The QDoubleSpinBox every config editor should use for a decimal field.
// Plain QDoubleSpinBox trips over three things there:
//
//  - It only accepts the locale's own decimal separator, so "0,5" typed
//    on an en-US system is rejected (or worse, read as the grouped "05"),
//    and "0.5" on a pt-BR one likewise. Both '.' and ',' are accepted here
//    and turned into the locale's separator as they're typed or pasted.
//  - Its arrows step by a fixed 1.0, useless on a 0..1 range. This one
//    steps adaptively -- one decade below the current value (0.1 at 1.x,
//    10 at 100.x), down to the last shown decimal.
//  - With keyboard tracking on, every keystroke emits valueChanged(); a
//    config editor pushes that through the undo stack, which re-syncs the
//    editor via setValue() and reformats the text mid-typing ("0" becomes
//    "0.00", so the "." typed next is rejected). Here the value only
//    commits on Enter, focus loss or an arrow step.
class DoubleSpinBox : public QDoubleSpinBox {
    Q_OBJECT

public:
    explicit DoubleSpinBox(QWidget* parent = nullptr);

    QValidator::State validate(QString& input, int& pos) const override;
    double valueFromText(const QString& text) const override;

private:
    QString normalizedDecimalSeparator(const QString& text) const;
};

}  // namespace traceview
