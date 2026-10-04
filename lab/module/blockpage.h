#pragma once

#include <QPointer>
#include <QWidget>

#include "lab/code/codeblockowner.h"

class QComboBox;
class QFormLayout;
class QHBoxLayout;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

namespace tracelab {

class LabPageHeader;
class LabSection;

// The page of one code block -- a plant, a filter, a controller of any
// CodeBlockOwner -- in the TraceLab tab, without its code: the code opens
// in VS Code or in a window of its own (CodeWindow) from the header's one
// button. The header says what the block is and how it runs; below, in
// columns: where the code comes from (a template, a file of yours, a new
// one; back to the original) with the compiler's messages and what printf()
// wrote, the parameters (live) and the signals it can read with their
// values. A page above (the plant's) can take the header over and add
// columns of its own before these.
class BlockPage : public QWidget {
    Q_OBJECT

public:
    explicit BlockPage(QWidget* parent = nullptr);

    // `block`: the owner's numbering. Null: the code's columns hide.
    void setBlock(CodeBlockOwner* owner, int block);
    CodeBlockOwner* owner() const {
        return m_owner;
    }
    int block() const {
        return m_block;
    }

    // The status, log, fault, parameters and signals (the panel calls it a
    // few times a second).
    void refreshLive();

    LabPageHeader* header() const {
        return m_header;
    }
    // False: the header's kind, title and status are the caller's.
    void setAutoHeader(bool automatic);
    // A column before the code's.
    void addLeadingColumn(QWidget* column, int stretch);
    // "Open the code", hidden without a block.
    QPushButton* openCodeButton() const {
        return m_openButton;
    }
    // A line in the header for a few seconds (what an action did).
    void flash(const QString& text);

signals:
    // A filter asked to be taken out of the chain.
    void removeRequested(int block);

private:
    void applyTheme();
    void onBlocksChanged();
    void refreshHeader();
    void refreshOrigin();
    void refreshMessages();
    void rebuildParams();
    void refreshParamValues();
    void refreshSignals();
    void refreshSourceCombo();
    void pickSource(int index);
    void revertToOriginal();
    void openExternally(int how);
    QString kindLabel() const;

    QPointer<CodeBlockOwner> m_owner;
    int m_block = 0;
    bool m_autoHeader = true;
    QStringList m_paramNames;
    bool m_updating = false;
    QString m_fault;
    QString m_flash;
    qint64 m_flashUntil = 0;

    LabPageHeader* m_header = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_openButton = nullptr;
    QHBoxLayout* m_columns = nullptr;
    int m_leadingColumns = 0;
    LabSection* m_codeSection = nullptr;
    LabSection* m_paramsSection = nullptr;
    LabSection* m_signalsSection = nullptr;
    QComboBox* m_sourceCombo = nullptr;
    QLabel* m_origin = nullptr;
    QPushButton* m_revertButton = nullptr;
    QListWidget* m_messages = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QWidget* m_paramsBox = nullptr;
    QFormLayout* m_paramsForm = nullptr;
    QToolButton* m_defaultsButton = nullptr;
    QListWidget* m_signals = nullptr;
};

}  // namespace tracelab
