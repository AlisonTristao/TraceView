#include "fontmenuaction.h"

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QStyleOptionMenuItem>
#include <QStylePainter>
#include <QVBoxLayout>

namespace traceview {

namespace {

// Custom widget that renders the font name using the font itself
class FontMenuWidget final : public QWidget {
public:
    explicit FontMenuWidget(const FontOption& font, QWidget* parent = nullptr)
        : QWidget(parent), m_font(font) {
        setContentsMargins(6, 4, 6, 4);
    }

protected:
    QSize sizeHint() const override {
        QFontMetrics fm(fontForDisplay());
        return fm.size(Qt::TextSingleLine, m_font.displayName) + QSize(24, 8);
    }

    void paintEvent(QPaintEvent*) override {
        QStylePainter painter(this);
        QStyleOptionMenuItem option;
        option.initFrom(this);
        option.menuRect = rect();
        option.text = m_font.displayName;
        option.font = fontForDisplay();
        painter.drawControl(QStyle::CE_MenuItem, option);
    }

private:
    QFont fontForDisplay() const {
        QFont f = font();
        if (!m_font.family.isEmpty()) {
            f.setFamily(m_font.family);
        }
        return f;
    }

    FontOption m_font;
};

}  // namespace

FontMenuAction::FontMenuAction(const FontOption& font, QObject* parent)
    : QWidgetAction(parent), m_font(font) {
    setText(font.displayName);
    setCheckable(true);
}

QWidget* FontMenuAction::createWidget(QWidget* parent) {
    return new FontMenuWidget(m_font, parent);
}

}  // namespace traceview
