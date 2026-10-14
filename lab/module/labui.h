#pragma once

#include <QGroupBox>
#include <QLabel>
#include <QString>

class QVBoxLayout;

namespace traceview {
struct ThemePalette;
}

namespace tracelab {

// The look the TraceLab tab and the code windows share: the tab's pages laid
// out like Settings categories (a title, what the page is for, then its
// sections), the primary button, notes and field labels in the secondary
// text color.
namespace ui {

// The style sheet for a top-level widget holding these (the tab's panel,
// a code window).
QString styleSheet(const traceview::ThemePalette& palette);

QLabel* note(const QString& text, QWidget* parent);
QLabel* fieldLabel(const QString& text, QWidget* parent);

// A primary action (accent filled): one a page.
void makePrimary(QWidget* button);

// Lays `page` out as a Settings category page -- its title, then a line on
// what it is for -- and returns the layout its sections go into. The
// description's label in `descriptionLabel`, for a page whose text changes.
QVBoxLayout* categoryPage(QWidget* page, const QString& title, const QString& description,
                          QLabel** descriptionLabel = nullptr);

}  // namespace ui

// A titled section of a page: a Settings group box, its body a vertical
// layout.
class LabSection : public QGroupBox {
    Q_OBJECT

public:
    explicit LabSection(const QString& title, QWidget* parent = nullptr);

    QVBoxLayout* body() const {
        return m_body;
    }

private:
    QVBoxLayout* m_body = nullptr;
};

// How a block or the run stands, in one line: a dot and the text in the
// theme's tone color.
class LabStatusLine : public QLabel {
    Q_OBJECT

public:
    explicit LabStatusLine(QWidget* parent = nullptr);

    // tone: 0 neutral, 1 good, 2 warning, 3 bad.
    void setStatus(const QString& text, int tone);

private:
    void applyColor();

    int m_tone = 0;
};

}  // namespace tracelab
