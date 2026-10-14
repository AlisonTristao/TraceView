#pragma once

#include <QPointer>
#include <QWidget>

#include "lab/code/codeblockowner.h"

class QLabel;
class QListWidget;
class QPushButton;

namespace tracelab {

class CodeEditor;

// The code of one block -- a plant, a filter, a controller of any
// CodeBlockOwner -- in a window of its own: the editor, Save (Ctrl+S; a
// template is saved as a copy of the user's first) and the compiler's
// messages and the fault (click one: the editor goes to its line). The
// block's page in the TraceLab tab keeps the rest (where the code comes
// from, parameters, signals, log). One window a block; it follows code
// saved from elsewhere unless it holds edits of its own.
class CodeWindow : public QWidget {
    Q_OBJECT

public:
    // How a block's file opens outside TraceView.
    enum class External { VsCode, OtherProgram };

    // The block's window, made and shown (raised when it exists).
    static CodeWindow* open(CodeBlockOwner* owner, int block);
    // Its window, null when none is open.
    static CodeWindow* find(const CodeBlockOwner* owner, int block);
    // Closes every window of the owner from `firstBlock` on (asking about
    // unsaved changes); false when the user cancels.
    static bool closeFrom(const CodeBlockOwner* owner, int firstBlock);

    // Opens the block's file in another editor (at the line of its window's
    // cursor when open). A template is saved as a copy of the user's first,
    // a window's unsaved changes are asked about. `message`: what to tell
    // the user (also on failure). False when nothing was opened.
    static bool openExternally(QWidget* dialogParent, CodeBlockOwner* owner, int block,
                               External how, QString* message);

    ~CodeWindow() override;

    CodeBlockOwner* owner() const {
        return m_owner;
    }
    int block() const {
        return m_block;
    }
    CodeEditor* editor() const {
        return m_editor;
    }
    bool hasUnsavedChanges() const;
    // With unsaved changes, asks to save them: false when the user cancels.
    bool maybeSave();
    bool save();
    // Drops the editor's changes for what the block holds now.
    void reload();
    // Inserts at the cursor and brings the window up (a signal's
    // declaration from the page).
    void insertText(const QString& text);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    CodeWindow(CodeBlockOwner* owner, int block);

    void applyTheme();
    void onBlocksChanged();
    void refreshHeader();
    void refreshMessages();

    QPointer<CodeBlockOwner> m_owner;
    int m_block = 0;
    QString m_loadedSource;
    QString m_fault;

    QLabel* m_title = nullptr;
    QLabel* m_origin = nullptr;
    QPushButton* m_vsCodeButton = nullptr;
    QPushButton* m_saveButton = nullptr;
    CodeEditor* m_editor = nullptr;
    QListWidget* m_messages = nullptr;
};

}  // namespace tracelab
