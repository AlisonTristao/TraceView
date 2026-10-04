#pragma once

#include <QString>
#include <QWidget>

class QVBoxLayout;

namespace tracelab {

// One card of the TraceLab tab: the same silhouette as a Devices tab card
// (lib/devices/devicecard.cpp) -- the theme's frame style, a @surface@ body
// and a 24px @surfaceAlt@ header strip with a glyph, an optional status dot
// and a bold title -- holding ordinary widgets in body() instead of painted
// text. The header can also carry a short status on its right ("running").
class LabCard : public QWidget {
    Q_OBJECT

public:
    // How the header's status reads: no dot, or a dot and its text in the
    // theme's success/warning/danger color.
    enum class Tone { None, Success, Warning, Danger };

    // `iconPath`: an SVG resource, tinted to the header's text color.
    LabCard(const QString& iconPath, const QString& title, QWidget* parent = nullptr);

    QVBoxLayout* body() const {
        return m_body;
    }

    void setStatus(Tone tone, const QString& text);
    void setTitle(const QString& title);

    // A card that can be picked (the TraceLab tab's loop): it emits
    // clicked() and, while selected, draws the accent outline.
    void setSelectable(bool selectable);
    void setSelected(bool selected);
    bool isSelected() const {
        return m_selected;
    }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QString m_iconPath;
    QString m_title;
    Tone m_tone = Tone::None;
    QString m_statusText;
    QVBoxLayout* m_body = nullptr;
    bool m_selectable = false;
    bool m_selected = false;
};

}  // namespace tracelab
