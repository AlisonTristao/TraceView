#pragma once

#include <QPointer>
#include <QWidget>

#include "lab/control/labdevices.h"

class QComboBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

namespace tracelab {

class CodeEditor;

// The page of one code block -- a filter or the controller of a
// LabControllerDevice -- in the TraceLab tab: its code in an editor, where it
// comes from (a template, a file of yours, a new one), Save (Ctrl+S; a
// template is saved as a copy in your folder first), back to the original,
// the compiler's messages and the fault (click one: the editor goes to its
// line), what printf() wrote, the parameters (live) and the signals it can
// read, with their values.
class BlockEditor : public QWidget {
    Q_OBJECT

public:
    explicit BlockEditor(QWidget* parent = nullptr);

    // `block`: LabControllerDevice::kController or a filter's index. Null:
    // shows nothing.
    void setBlock(LabControllerDevice* device, int block);
    LabControllerDevice* device() const {
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

signals:
    // A filter asked to be taken out of the chain.
    void removeRequested(int block);
    // A short line for the status bar.
    void statusMessage(const QString& text);

private:
    void applyTheme();
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

    QPointer<LabControllerDevice> m_device;
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
