#pragma once

#include <QFrame>
#include <QString>

class QBoxLayout;
class QHBoxLayout;
class QLabel;
class QVBoxLayout;

namespace traceview {
struct ThemePalette;
}

namespace tracelab {

// The look the TraceLab tab and the code windows share: sections (a titled
// panel on the surface tone), the page header, the primary button, notes
// and field labels in the secondary text color.
namespace ui {

// The style sheet for a top-level widget holding these (the tab's panel,
// a code window).
QString styleSheet(const traceview::ThemePalette& palette);

QLabel* note(const QString& text, QWidget* parent);
QLabel* fieldLabel(const QString& text, QWidget* parent);

// A primary action (accent filled): one a page.
void makePrimary(QWidget* button);

}  // namespace ui

// A titled panel. Its body is a vertical layout below the title; a widget
// on the right of the title goes in titleRow().
class LabSection : public QFrame {
    Q_OBJECT

public:
    explicit LabSection(const QString& title, QWidget* parent = nullptr);

    QVBoxLayout* body() const {
        return m_body;
    }
    QHBoxLayout* titleRow() const {
        return m_titleRow;
    }
    void setTitle(const QString& title);

private:
    QLabel* m_title = nullptr;
    QHBoxLayout* m_titleRow = nullptr;
    QVBoxLayout* m_body = nullptr;
};

// The top of a page: what the block is (a small kind line, the name, its
// status in its tone) on the left; figures (extras) and the page's actions
// on the right.
class LabPageHeader : public QWidget {
    Q_OBJECT

public:
    explicit LabPageHeader(QWidget* parent = nullptr);

    void setKind(const QString& text);
    void setTitle(const QString& text);
    // tone: 0 neutral, 1 good, 2 warning, 3 bad.
    void setStatus(const QString& text, int tone);
    void addExtra(QWidget* widget);
    void addAction(QWidget* widget);

private:
    void applyStatusColor();

    QLabel* m_kind = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_status = nullptr;
    int m_tone = 0;
    QHBoxLayout* m_extras = nullptr;
    QHBoxLayout* m_actions = nullptr;
};

}  // namespace tracelab
