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

class LabSection;
class LabStatusLine;

// What one code block -- a plant, a filter, a controller of any
// CodeBlockOwner -- shows in the TraceLab tab, without its code: the code
// opens in VS Code or in a window of its own (CodeWindow). In parts, each
// on a page of the tab's menu, stacked down the page like Settings
// sections: the code (how the block stands; where its code comes from -- a
// template, a file of yours, a new one; back to the original -- with the
// buttons that open it, and the compiler's messages and what printf()
// wrote), the parameters (live) and the signals it can read with their
// values.
class BlockPage : public QWidget {
    Q_OBJECT

public:
    enum Part { CodePart = 0x1, ParametersPart = 0x2, SignalsPart = 0x4, AllParts = 0x7 };

    explicit BlockPage(int parts = AllParts, QWidget* parent = nullptr);

    // `block`: the owner's numbering. Null: the sections hide.
    void setBlock(CodeBlockOwner* owner, int block);
    CodeBlockOwner* owner() const {
        return m_owner;
    }
    int block() const {
        return m_block;
    }

    // The status, log, fault, parameters and signals of the parts shown
    // (the panel calls it a few times a second).
    void refreshLive();

    // "Open the code" (the code part's), hidden without a block.
    QPushButton* openCodeButton() const {
        return m_openButton;
    }
    // A line over the status for a few seconds (what an action did).
    void flash(const QString& text);

signals:
    // A filter asked to be taken out of the chain.
    void removeRequested(int block);

private:
    bool has(Part part) const {
        return (m_parts & part) != 0;
    }
    void applyTheme();
    void onBlocksChanged();
    void refreshStatus();
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

    int m_parts = AllParts;
    QPointer<CodeBlockOwner> m_owner;
    int m_block = 0;
    QStringList m_paramNames;
    bool m_updating = false;
    QString m_fault;
    QString m_flash;
    qint64 m_flashUntil = 0;

    LabStatusLine* m_status = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_openButton = nullptr;
    LabSection* m_codeSection = nullptr;
    LabSection* m_compileSection = nullptr;
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
