#pragma once

#include <QPointer>
#include <QWidget>

#include "lab/code/codeblockowner.h"

class QComboBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

namespace tracelab {

class CodeEditor;

// The page of one code block -- a plant, a filter, a controller: a block of
// any CodeBlockOwner -- in the TraceLab tab: its code in an editor, where it
// comes from (a template, a file of yours, a new one), Save (Ctrl+S; a
// template is saved as a copy in your folder first), back to the original,
// the compiler's messages and the fault (click one: the editor goes to its
// line), what printf() wrote, the parameters (live) and the signals it can
// read, with their values. The code can also go to a window of its own
// (another monitor, the scene still in sight) and open in another editor:
// the file is watched, a save there compiles it here.
class BlockEditor : public QWidget {
    Q_OBJECT

public:
    // How "Open in editor" opens the file.
    enum class External { VsCode, DefaultApp, Chooser };

    explicit BlockEditor(QWidget* parent = nullptr);
    ~BlockEditor() override;

    // `block`: the owner's numbering. Null: shows nothing.
    void setBlock(CodeBlockOwner* device, int block);
    CodeBlockOwner* device() const {
        return m_device;
    }
    int block() const {
        return m_block;
    }

    bool hasUnsavedChanges() const;
    // With unsaved changes, asks to save them: false when the user cancels.
    bool maybeSave();

    // The log, the signals' values and the fault, from the running code
    // (the panel calls it a few times a second).
    void refreshLive();

    // The editor in a window of its own (true) or back in its page; the
    // window closing brings it back.
    void setDetached(bool detached);
    bool isDetached() const {
        return m_window != nullptr;
    }
    QWidget* detachedWindow() const {
        return m_window;
    }

    // Opens the block's file in another editor at the cursor's line; a
    // template is saved as a copy of yours first, unsaved changes asked
    // about. False when nothing was opened.
    bool openExternally(External how);

signals:
    // A filter asked to be taken out of the chain.
    void removeRequested(int block);
    // A short line for the status bar.
    void statusMessage(const QString& text);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyTheme();
    void refreshExternalButton();
    External preferredExternal() const;
    void setSidePanelVisible(bool visible);
    void onBlocksChanged();
    void loadSourceFromDevice();
    void refreshMessages();
    void refreshHeader();
    void rebuildParams();
    void refreshParamValues();
    void refreshSignals();
    void refreshSourceCombo();
    void pickSource(int index);
    bool save();
    void revertToOriginal();
    QString kindFolderLabel() const;

    QPointer<CodeBlockOwner> m_device;
    int m_block = 0;
    QString m_loadedSource;  // the text the editor was given
    QStringList m_paramNames;
    bool m_updating = false;

    QLabel* m_title = nullptr;
    QLabel* m_origin = nullptr;
    QComboBox* m_sourceCombo = nullptr;
    QPushButton* m_saveButton = nullptr;
    QPushButton* m_revertButton = nullptr;
    QToolButton* m_folderButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QToolButton* m_externalButton = nullptr;
    QToolButton* m_sideButton = nullptr;
    QToolButton* m_windowButton = nullptr;
    QWidget* m_body = nullptr;          // everything but the note below
    QWidget* m_detachedNote = nullptr;  // in the page while the code is away
    QWidget* m_side = nullptr;
    QPointer<QWidget> m_window;
    CodeEditor* m_editor = nullptr;
    QListWidget* m_messages = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QLabel* m_binding = nullptr;
    QLabel* m_previewNote = nullptr;
    QWidget* m_paramsBox = nullptr;
    QFormLayout* m_paramsForm = nullptr;
    QPushButton* m_defaultsButton = nullptr;
    QListWidget* m_signals = nullptr;
};

}  // namespace tracelab
