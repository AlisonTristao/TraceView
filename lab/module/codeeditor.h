#pragma once

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

namespace tracelab {

// C with tracelab.h's words: keywords, types, numbers, strings, comments,
// preprocessor lines and the TL_* declarations / tl_* functions, in the
// theme's colors.
class CHighlighter final : public QSyntaxHighlighter {
public:
    explicit CHighlighter(QTextDocument* document);
    // Re-reads the theme's colors.
    void applyTheme();

protected:
    void highlightBlock(const QString& text) override;

private:
    QTextCharFormat m_keyword;
    QTextCharFormat m_type;
    QTextCharFormat m_tracelab;
    QTextCharFormat m_number;
    QTextCharFormat m_string;
    QTextCharFormat m_comment;
    QTextCharFormat m_preprocessor;
};

// A plain-text editor for a code block: monospace, line numbers in a gutter,
// the current line tinted, and lines with a compiler message (or the fault)
// marked -- red for an error, amber for a warning.
class CodeEditor final : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit CodeEditor(QWidget* parent = nullptr);

    struct Mark {
        int line = 0;  // 1-based
        bool warning = false;
        QString text;
    };
    void setMarks(const QVector<Mark>& marks);
    // Moves the cursor to the start of `line` (1-based) and shows it.
    void goToLine(int line);
    void applyTheme();

    // The gutter's.
    int gutterWidth() const;
    void paintGutter(QPaintEvent* event);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool viewportEvent(QEvent* event) override;

private:
    void updateGutterWidth();
    void updateGutter(const QRect& rect, int dy);
    void refreshSelections();

    QWidget* m_gutter = nullptr;
    CHighlighter* m_highlighter = nullptr;
    QVector<Mark> m_marks;
};

}  // namespace tracelab
