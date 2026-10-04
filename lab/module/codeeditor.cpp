#include "codeeditor.h"

#include <QFontDatabase>
#include <QHelpEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QToolTip>

#include "traceview/thememanager.h"

namespace tracelab {

namespace {

constexpr int kGutterPadding = 8;
constexpr int kTabSpaces = 4;

class Gutter final : public QWidget {
public:
    explicit Gutter(CodeEditor* editor) : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override {
        return QSize(m_editor->gutterWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        m_editor->paintGutter(event);
    }

private:
    CodeEditor* m_editor;
};

QColor mix(const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(float(a.redF() * (1 - t) + b.redF() * t),
                            float(a.greenF() * (1 - t) + b.greenF() * t),
                            float(a.blueF() * (1 - t) + b.blueF() * t));
}

}  // namespace

// ---------------------------------------------------------------------------

CHighlighter::CHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {
    applyTheme();
}

void CHighlighter::applyTheme() {
    const traceview::ThemePalette& palette = traceview::ThemeManager::instance().currentTheme();
    const bool dark = palette.background.lightnessF() < 0.5;
    m_keyword.setForeground(palette.accent);
    m_keyword.setFontWeight(QFont::Bold);
    m_type.setForeground(dark ? QColor(0x4e, 0xc9, 0xb0) : QColor(0x26, 0x7f, 0x99));
    m_tracelab.setForeground(dark ? QColor(0xc5, 0x86, 0xc0) : QColor(0x8e, 0x35, 0xa8));
    m_tracelab.setFontWeight(QFont::Bold);
    m_number.setForeground(dark ? QColor(0xb5, 0xce, 0xa8) : QColor(0x09, 0x86, 0x58));
    m_string.setForeground(dark ? QColor(0xce, 0x91, 0x78) : QColor(0xa3, 0x15, 0x15));
    m_comment.setForeground(palette.textSecondary);
    m_comment.setFontItalic(true);
    m_preprocessor.setForeground(dark ? QColor(0x9c, 0xdc, 0xfe) : QColor(0x80, 0x5b, 0x00));
    rehighlight();
}

void CHighlighter::highlightBlock(const QString& text) {
    static const QRegularExpression keywords(QStringLiteral(
        "\\b(?:if|else|for|while|do|return|break|continue|switch|case|default|goto|sizeof|"
        "static|const|extern|volatile|inline|struct|union|enum|typedef|register|restrict)\\b"));
    static const QRegularExpression types(QStringLiteral(
        "\\b(?:void|char|short|int|long|float|double|signed|unsigned|bool|size_t|"
        "u?int(?:8|16|32|64)_t|tl_decl|true|false|NULL)\\b"));
    static const QRegularExpression tracelab(QStringLiteral(
        "\\b(?:TL_[A-Z_]+|tl_[a-z_]+|der|next|(?:plant|filt|ctl)_(?:reset|deriv|update|output|"
        "step))\\b"));
    static const QRegularExpression number(
        QStringLiteral("\\b(?:0[xX][0-9a-fA-F]+|[0-9]+\\.?[0-9]*(?:[eE][-+]?[0-9]+)?)[uUlLfF]*\\b"));
    static const QRegularExpression string(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'(?:[^'\\\\]|\\\\.)*'"));
    static const QRegularExpression preprocessor(QStringLiteral("^\\s*#.*$"));

    const auto apply = [&](const QRegularExpression& re, const QTextCharFormat& format) {
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(int(m.capturedStart()), int(m.capturedLength()), format);
        }
    };
    apply(preprocessor, m_preprocessor);
    apply(keywords, m_keyword);
    apply(types, m_type);
    apply(tracelab, m_tracelab);
    apply(number, m_number);
    apply(string, m_string);

    // Comments: // to the end of the line, /* */ across blocks (state 1 =
    // inside one).
    setCurrentBlockState(0);
    int start = previousBlockState() == 1 ? 0 : -1;
    int i = 0;
    while (i < text.size()) {
        if (start >= 0) {
            const int end = int(text.indexOf(QStringLiteral("*/"), i));
            if (end < 0) {
                setFormat(start, int(text.size()) - start, m_comment);
                setCurrentBlockState(1);
                return;
            }
            setFormat(start, end + 2 - start, m_comment);
            i = end + 2;
            start = -1;
            continue;
        }
        if (text.at(i) == QLatin1Char('"')) {  // skip strings
            const int close = int(text.indexOf(QLatin1Char('"'), i + 1));
            i = close < 0 ? int(text.size()) : close + 1;
            continue;
        }
        if (text.mid(i, 2) == QLatin1String("//")) {
            setFormat(i, int(text.size()) - i, m_comment);
            return;
        }
        if (text.mid(i, 2) == QLatin1String("/*")) {
            start = i;
            i += 2;
            continue;
        }
        ++i;
    }
}

// ---------------------------------------------------------------------------

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    setFont(font);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * kTabSpaces);
    m_gutter = new Gutter(this);
    m_highlighter = new CHighlighter(document());
    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateGutterWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateGutter);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::refreshSelections);
    connect(&traceview::ThemeManager::instance(), &traceview::ThemeManager::themeChanged, this,
            &CodeEditor::applyTheme);
    updateGutterWidth();
    applyTheme();
}

void CodeEditor::applyTheme() {
    m_highlighter->applyTheme();
    refreshSelections();
    m_gutter->update();
}

int CodeEditor::gutterWidth() const {
    int digits = 1;
    for (int max = qMax(1, blockCount()); max >= 10; max /= 10) {
        ++digits;
    }
    digits = qMax(digits, 3);
    return kGutterPadding * 2 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateGutterWidth() {
    setViewportMargins(gutterWidth(), 0, 0, 0);
}

void CodeEditor::updateGutter(const QRect& rect, int dy) {
    if (dy) {
        m_gutter->scroll(0, dy);
    } else {
        m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
        updateGutterWidth();
    }
}

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect area = contentsRect();
    m_gutter->setGeometry(QRect(area.left(), area.top(), gutterWidth(), area.height()));
}

void CodeEditor::paintGutter(QPaintEvent* event) {
    const traceview::ThemePalette& palette = traceview::ThemeManager::instance().currentTheme();
    QPainter painter(m_gutter);
    painter.fillRect(event->rect(), palette.surfaceAlt);
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber() + 1;
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int current = textCursor().blockNumber() + 1;
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QColor color = number == current ? palette.textPrimary : palette.textSecondary;
            for (const Mark& mark : std::as_const(m_marks)) {
                if (mark.line == number) {
                    color = mark.warning ? palette.warning : palette.danger;
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(color);
                    const int size = fontMetrics().height() / 3;
                    painter.drawEllipse(QRect(3, top + (fontMetrics().height() - size) / 2, size, size));
                    break;
                }
            }
            painter.setPen(color);
            painter.drawText(0, top, m_gutter->width() - kGutterPadding, fontMetrics().height(),
                             Qt::AlignRight | Qt::AlignVCenter, QString::number(number));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

void CodeEditor::setMarks(const QVector<Mark>& marks) {
    m_marks = marks;
    refreshSelections();
    m_gutter->update();
}

void CodeEditor::goToLine(int line) {
    const QTextBlock block = document()->findBlockByNumber(qMax(0, line - 1));
    if (!block.isValid()) {
        return;
    }
    QTextCursor cursor(block);
    setTextCursor(cursor);
    centerCursor();
    setFocus();
}

void CodeEditor::refreshSelections() {
    const traceview::ThemePalette& palette = traceview::ThemeManager::instance().currentTheme();
    const QColor base = viewport()->palette().color(QPalette::Base);
    QList<QTextEdit::ExtraSelection> selections;
    for (const Mark& mark : std::as_const(m_marks)) {
        const QTextBlock block = document()->findBlockByNumber(mark.line - 1);
        if (!block.isValid()) {
            continue;
        }
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(
            mix(base, mark.warning ? palette.warning : palette.danger, 0.22));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = QTextCursor(block);
        selections.append(selection);
    }
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection current;
        current.format.setBackground(mix(base, palette.accent, 0.10));
        current.format.setProperty(QTextFormat::FullWidthSelection, true);
        current.cursor = textCursor();
        current.cursor.clearSelection();
        selections.prepend(current);
    }
    setExtraSelections(selections);
}

void CodeEditor::keyPressEvent(QKeyEvent* event) {
    // Tab indents with spaces; Enter keeps the line's indentation.
    if (event->key() == Qt::Key_Tab && !(event->modifiers() & Qt::ControlModifier)) {
        textCursor().insertText(QString(kTabSpaces, QLatin1Char(' ')));
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const QString line = textCursor().block().text();
        int indent = 0;
        while (indent < line.size() && line.at(indent) == QLatin1Char(' ')) {
            ++indent;
        }
        QString extra;
        const QString before = line.left(textCursor().positionInBlock()).trimmed();
        if (before.endsWith(QLatin1Char('{'))) {
            extra = QString(kTabSpaces, QLatin1Char(' '));
        }
        QPlainTextEdit::keyPressEvent(event);
        textCursor().insertText(QString(indent, QLatin1Char(' ')) + extra);
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

bool CodeEditor::viewportEvent(QEvent* event) {
    // The message of a marked line as its tooltip.
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const int line = cursorForPosition(help->pos()).blockNumber() + 1;
        QStringList texts;
        for (const Mark& mark : std::as_const(m_marks)) {
            if (mark.line == line && !mark.text.isEmpty()) {
                texts.append(mark.text);
            }
        }
        if (!texts.isEmpty()) {
            QToolTip::showText(help->globalPos(), texts.join(QLatin1Char('\n')), this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QPlainTextEdit::viewportEvent(event);
}

}  // namespace tracelab
