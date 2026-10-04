#include "blockeditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

#include "lab/code/codefiles.h"
#include "lab/module/codeeditor.h"
#include "theme/doublespinbox.h"
#include "traceview/thememanager.h"

namespace tracelab {

using code::BlockKind;
using code::CodeLibrary;
using code::DeclKind;
using code::Declaration;

namespace {

constexpr int kMaxLogLines = 2000;
constexpr int kSideWidth = 280;
constexpr int kFieldWidth = 120;

// What an entry of the source combo stands for.
enum SourceType { Header = 0, Template, File, OpenFile, NewFile };

QLabel* sectionTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

// Enough decimals to show the code's value exactly, plus two to tune it.
int decimalsFor(const Declaration& decl) {
    int needed = 0;
    for (double value : {decl.defaultValue, decl.minimum, decl.maximum}) {
        int d = 0;
        double scaled = std::fabs(value);
        while (d < 6 && std::fabs(scaled - std::round(scaled)) > 1e-9 * qMax(1.0, scaled)) {
            scaled *= 10.0;
            ++d;
        }
        needed = qMax(needed, d);
    }
    return qBound(1, needed + 2, 6);
}

}  // namespace

BlockEditor::BlockEditor(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // The header: the block, where its code comes from, the actions.
    auto* header = new QHBoxLayout;
    header->setSpacing(8);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    m_title = sectionTitle(QString(), this);
    QFont titleFont = m_title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
    m_title->setFont(titleFont);
    m_origin = new QLabel(this);
    m_origin->setObjectName(QStringLiteral("labNote"));
    titles->addWidget(m_title);
    titles->addWidget(m_origin);
    header->addLayout(titles, 1);
    auto* codeLabel = new QLabel(tr("Code"), this);
    codeLabel->setObjectName(QStringLiteral("labFieldLabel"));
    header->addWidget(codeLabel);
    m_sourceCombo = new QComboBox(this);
    m_sourceCombo->setMinimumWidth(220);
    m_sourceCombo->setToolTip(tr("Where the block's code comes from: a template (the original, "
                                 "read-only; saving makes a copy of yours), one of your files, "
                                 "another file, or a new one."));
    header->addWidget(m_sourceCombo);
    m_saveButton = new QPushButton(tr("Save"), this);
    m_saveButton->setToolTip(tr("Saves and compiles the code (Ctrl+S). A template is saved as a "
                                "copy in your folder."));
    header->addWidget(m_saveButton);
    m_revertButton = new QPushButton(tr("Back to the original"), this);
    m_revertButton->setToolTip(tr("Replaces the code with the template it came from."));
    header->addWidget(m_revertButton);
    m_folderButton = new QToolButton(this);
    m_folderButton->setText(tr("Folder"));
    m_folderButton->setToolTip(tr("Opens the folder of the file (or your folder of blocks of "
                                  "this kind) in the system's file manager."));
    header->addWidget(m_folderButton);
    m_removeButton = new QPushButton(tr("Remove"), this);
    m_removeButton->setToolTip(tr("Takes this filter out of the chain (the file stays)."));
    header->addWidget(m_removeButton);
    layout->addLayout(header);

    // The code and, under it, its messages and log; the side panel on the
    // right.
    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    auto* left = new QSplitter(Qt::Vertical, split);
    left->setChildrenCollapsible(false);
    m_editor = new CodeEditor(left);
    auto* tabs = new QTabWidget(left);
    tabs->setDocumentMode(true);
    m_messages = new QListWidget(tabs);
    m_messages->setWordWrap(true);
    tabs->addTab(m_messages, tr("Messages"));
    m_log = new QPlainTextEdit(tabs);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(kMaxLogLines);
    m_log->setFont(m_editor->font());
    m_log->setPlaceholderText(tr("What printf() and tl_log() write shows here."));
    tabs->addTab(m_log, tr("Log"));
    left->addWidget(m_editor);
    left->addWidget(tabs);
    left->setStretchFactor(0, 4);
    left->setStretchFactor(1, 1);

    auto* sideScroll = new QScrollArea(split);
    sideScroll->setWidgetResizable(true);
    sideScroll->setFrameShape(QFrame::NoFrame);
    sideScroll->setMinimumWidth(kSideWidth);
    auto* side = new QWidget(sideScroll);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(8, 0, 4, 0);
    sideLayout->setSpacing(8);
    sideLayout->addWidget(sectionTitle(tr("Loop"), side));
    m_binding = new QLabel(side);
    m_binding->setWordWrap(true);
    m_binding->setTextFormat(Qt::RichText);
    sideLayout->addWidget(m_binding);
    m_previewNote = new QLabel(tr("Uses the plant's preview of the track (TL_PREVIEW): "
                                  "information a real robot may not have."),
                               side);
    m_previewNote->setObjectName(QStringLiteral("labNote"));
    m_previewNote->setWordWrap(true);
    sideLayout->addWidget(m_previewNote);

    sideLayout->addSpacing(4);
    sideLayout->addWidget(sectionTitle(tr("Parameters"), side));
    m_paramsBox = new QWidget(side);
    m_paramsForm = new QFormLayout(m_paramsBox);
    m_paramsForm->setContentsMargins(0, 0, 0, 0);
    m_paramsForm->setHorizontalSpacing(10);
    m_paramsForm->setVerticalSpacing(6);
    m_paramsForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    sideLayout->addWidget(m_paramsBox);
    m_defaultsButton = new QPushButton(tr("Defaults from the code"), side);
    m_defaultsButton->setToolTip(tr("Every parameter back to the value written in the code."));
    sideLayout->addWidget(m_defaultsButton, 0, Qt::AlignLeft);

    sideLayout->addSpacing(4);
    sideLayout->addWidget(sectionTitle(tr("Signals"), side));
    auto* signalsNote = new QLabel(tr("Double-click one to declare it at the cursor."), side);
    signalsNote->setObjectName(QStringLiteral("labNote"));
    signalsNote->setWordWrap(true);
    sideLayout->addWidget(signalsNote);
    m_signals = new QListWidget(side);
    m_signals->setFont(m_editor->font());
    m_signals->setMinimumHeight(160);
    sideLayout->addWidget(m_signals, 1);
    sideScroll->setWidget(side);

    split->addWidget(left);
    split->addWidget(sideScroll);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    layout->addWidget(split, 1);

    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, [this] { save(); });
    connect(m_saveButton, &QPushButton::clicked, this, [this] { save(); });
    connect(m_revertButton, &QPushButton::clicked, this, &BlockEditor::revertToOriginal);
    connect(m_removeButton, &QPushButton::clicked, this, [this] {
        if (maybeSave()) {
            emit removeRequested(m_block);
        }
    });
    connect(m_folderButton, &QToolButton::clicked, this, [this] {
        if (!m_device) {
            return;
        }
        const QString path = m_device->spec(m_block).path;
        const BlockKind kind =
            m_device->blockKind(m_block);
        const QString folder =
            path.isEmpty() ? CodeLibrary::userDirectory(kind) : QFileInfo(path).absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
    connect(m_sourceCombo, qOverload<int>(&QComboBox::activated), this, &BlockEditor::pickSource);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &BlockEditor::refreshHeader);
    connect(m_messages, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const int line = item->data(Qt::UserRole).toInt();
        if (line > 0) {
            m_editor->goToLine(line);
        }
    });
    connect(m_messages, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const int line = item->data(Qt::UserRole).toInt();
        if (line > 0) {
            m_editor->goToLine(line);
        }
    });
    connect(m_signals, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const QString declaration = item->data(Qt::UserRole).toString();
        if (!declaration.isEmpty()) {
            m_editor->textCursor().insertText(declaration);
            m_editor->setFocus();
        }
    });
    connect(m_defaultsButton, &QPushButton::clicked, this, [this] {
        if (!m_device) {
            return;
        }
        const code::CodeModule* module = m_device->module(m_block);
        if (!module) {
            return;
        }
        for (const Declaration* decl : module->params()) {
            m_device->setParam(m_block, decl->name, decl->defaultValue);
        }
        refreshParamValues();
    });
    connect(&traceview::ThemeManager::instance(), &traceview::ThemeManager::themeChanged, this,
            &BlockEditor::applyTheme);
    setBlock(nullptr, 0);
}

void BlockEditor::applyTheme() {
    refreshMessages();
    refreshLive();
}

QString BlockEditor::kindFolderLabel() const {
    if (!m_device) {
        return QString();
    }
    switch (m_device->blockKind(m_block)) {
        case BlockKind::Plant:
            return tr("plant");
        case BlockKind::Filter:
            return tr("filter");
        case BlockKind::Controller:
            break;
    }
    return tr("controller");
}

void BlockEditor::setBlock(CodeBlockOwner* device, int block) {
    if (m_device) {
        disconnect(m_device, nullptr, this, nullptr);
    }
    m_device = device;
    m_block = block;
    m_paramNames.clear();
    m_log->clear();
    setEnabled(device != nullptr);
    if (device) {
        connect(device, &CodeBlockOwner::blocksChanged, this, &BlockEditor::onBlocksChanged);
        connect(device, &CodeBlockOwner::statusChanged, this, &BlockEditor::refreshLive);
        device->takeLog(block);  // what it wrote before is not news
    }
    m_removeButton->setVisible(device && device->canRemove(block));
    loadSourceFromDevice();
    refreshSourceCombo();
    rebuildParams();
    refreshMessages();
    refreshLive();
}

bool BlockEditor::hasUnsavedChanges() const {
    return m_device && m_editor->toPlainText() != m_loadedSource;
}

bool BlockEditor::maybeSave() {
    if (!hasUnsavedChanges()) {
        return true;
    }
    const auto answer = QMessageBox::question(
        this, tr("Unsaved code"),
        tr("The code of %1 has changes that were not saved.").arg(m_device->blockName(m_block)),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) {
        return false;
    }
    if (answer == QMessageBox::Save) {
        return save();
    }
    m_editor->setPlainText(m_loadedSource);
    return true;
}

void BlockEditor::loadSourceFromDevice() {
    m_updating = true;
    m_loadedSource = m_device ? m_device->source(m_block) : QString();
    m_editor->setPlainText(m_loadedSource);
    m_editor->document()->setModified(false);
    m_updating = false;
    refreshHeader();
}

void BlockEditor::onBlocksChanged() {
    if (!m_device) {
        return;
    }
    // A block removed can leave this index pointing past the owner's.
    if (m_device->blockRole(m_block).isEmpty()) {
        setBlock(nullptr, 0);
        return;
    }
    // New code from outside (a file saved elsewhere, another source picked):
    // shown unless the editor holds changes of its own.
    if (m_device->source(m_block) != m_loadedSource && !hasUnsavedChanges()) {
        loadSourceFromDevice();
    }
    refreshSourceCombo();
    rebuildParams();
    refreshMessages();
    refreshLive();
}

void BlockEditor::refreshHeader() {
    if (!m_device) {
        m_title->setText(tr("No block"));
        m_origin->clear();
        m_saveButton->setEnabled(false);
        m_revertButton->setEnabled(false);
        return;
    }
    const CodeBlockSpec spec = m_device->spec(m_block);
    const QString text = m_editor->toPlainText();
    QString title = m_device->blockName(m_block);
    if (hasUnsavedChanges()) {
        title += QStringLiteral(" *");
    }
    m_title->setText(title);
    // The template it came from, and whether it still is that.
    QString templateId = spec.templateId;
    if (templateId.isEmpty()) {
        templateId = CodeLibrary::originOf(text);
    }
    QStringList origin;
    origin.append(m_device->blockRole(m_block));
    if (!spec.path.isEmpty()) {
        origin.append(QDir::toNativeSeparators(spec.path));
    }
    const code::CodeTemplate* t = CodeLibrary::instance().findTemplate(templateId);
    if (t) {
        const bool original = spec.path.isEmpty() ? text == m_loadedSource
                                                  : !CodeLibrary::instance().isModified(text);
        origin.append(spec.path.isEmpty()
                          ? tr("template \"%1\" (read-only: Save makes a copy)").arg(t->name)
                          : (original ? tr("from the template \"%1\", unchanged").arg(t->name)
                                      : tr("from the template \"%1\", modified").arg(t->name)));
    }
    m_origin->setText(origin.join(QStringLiteral("  ·  ")));
    m_saveButton->setEnabled(hasUnsavedChanges() || (!spec.path.isEmpty() && !m_device->compiled(m_block)));
    bool canRevert = false;
    if (t) {
        const QString original = spec.path.isEmpty()
                                     ? CodeLibrary::instance().templateSource(templateId)
                                     : CodeLibrary::instance().originalOf(text);
        canRevert = !original.isEmpty() && original != text;
    }
    m_revertButton->setEnabled(canRevert);
    m_revertButton->setToolTip(t ? tr("Replaces the code with the template \"%1\".").arg(t->name)
                                 : tr("This code did not come from a template."));
}

void BlockEditor::refreshSourceCombo() {
    m_sourceCombo->clear();
    if (!m_device) {
        return;
    }
    const BlockKind kind =
        m_device->blockKind(m_block);
    const CodeBlockSpec spec = m_device->spec(m_block);
    int current = -1;
    const auto addHeader = [this](const QString& text) {
        m_sourceCombo->addItem(text, QVariantList{Header, QString()});
        const int row = m_sourceCombo->count() - 1;
        m_sourceCombo->setItemData(row, QVariant(0), Qt::UserRole - 1);  // disabled
        QFont font = m_sourceCombo->font();
        font.setBold(true);
        m_sourceCombo->setItemData(row, font, Qt::FontRole);
    };
    const QVector<code::CodeTemplate> templates = CodeLibrary::instance().templates(kind);
    if (!templates.isEmpty()) {
        addHeader(tr("Templates"));
        for (const code::CodeTemplate& t : templates) {
            m_sourceCombo->addItem(QStringLiteral("  ") + t.name, QVariantList{Template, t.id});
            m_sourceCombo->setItemData(m_sourceCombo->count() - 1, t.description, Qt::ToolTipRole);
            if (spec.path.isEmpty() && spec.templateId == t.id) {
                current = m_sourceCombo->count() - 1;
            }
        }
    }
    QStringList files = CodeLibrary::userFiles(kind);
    if (!spec.path.isEmpty() && !files.contains(QFileInfo(spec.path).absoluteFilePath())) {
        files.prepend(spec.path);
    }
    if (!files.isEmpty()) {
        addHeader(tr("Your files"));
        for (const QString& path : std::as_const(files)) {
            m_sourceCombo->addItem(QStringLiteral("  ") + QFileInfo(path).fileName(),
                                   QVariantList{File, path});
            m_sourceCombo->setItemData(m_sourceCombo->count() - 1,
                                       QDir::toNativeSeparators(path), Qt::ToolTipRole);
            if (!spec.path.isEmpty() &&
                QFileInfo(spec.path).absoluteFilePath() == QFileInfo(path).absoluteFilePath()) {
                current = m_sourceCombo->count() - 1;
            }
        }
    }
    m_sourceCombo->insertSeparator(m_sourceCombo->count());
    m_sourceCombo->addItem(tr("Open a file..."), QVariantList{OpenFile, QString()});
    m_sourceCombo->addItem(tr("New %1 (empty)...").arg(kindFolderLabel()),
                           QVariantList{NewFile, QString()});
    m_sourceCombo->setCurrentIndex(current);
}

void BlockEditor::pickSource(int index) {
    if (!m_device) {
        return;
    }
    const QVariantList data = m_sourceCombo->itemData(index).toList();
    const int type = data.value(0).toInt();
    const QString value = data.value(1).toString();
    const BlockKind kind =
        m_device->blockKind(m_block);
    const CodeBlockSpec current = m_device->spec(m_block);
    CodeBlockSpec spec;
    switch (type) {
        case Template:
            if (current.path.isEmpty() && current.templateId == value) {
                return;
            }
            spec.templateId = value;
            break;
        case File:
            if (QFileInfo(current.path).absoluteFilePath() == QFileInfo(value).absoluteFilePath()) {
                return;
            }
            spec.path = value;
            break;
        case OpenFile: {
            const QString path = QFileDialog::getOpenFileName(
                this, tr("Open a %1").arg(kindFolderLabel()), CodeLibrary::userDirectory(kind),
                tr("C files (*.c);;All files (*)"));
            if (path.isEmpty()) {
                refreshSourceCombo();
                return;
            }
            spec.path = path;
            break;
        }
        case NewFile: {
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, tr("New %1").arg(kindFolderLabel()),
                tr("File name (saved in %1):")
                    .arg(QDir::toNativeSeparators(CodeLibrary::userDirectory(kind))),
                QLineEdit::Normal,
                kind == BlockKind::Controller ? QStringLiteral("my_controller")
                                              : QStringLiteral("my_filter"),
                &ok);
            if (!ok || name.trimmed().isEmpty()) {
                refreshSourceCombo();
                return;
            }
            QString error;
            spec.path = CodeLibrary::instance().createFile(kind, QString(), name.trimmed(), &error);
            if (spec.path.isEmpty()) {
                QMessageBox::warning(this, tr("New %1").arg(kindFolderLabel()), error);
                refreshSourceCombo();
                return;
            }
            break;
        }
        default:
            refreshSourceCombo();
            return;
    }
    if (!maybeSave()) {
        refreshSourceCombo();
        return;
    }
    m_loadedSource.clear();  // take whatever the device loads
    m_editor->clear();
    m_device->setSpec(m_block, spec);
    loadSourceFromDevice();
}

bool BlockEditor::save() {
    if (!m_device) {
        return false;
    }
    CodeBlockSpec spec = m_device->spec(m_block);
    QString text = m_editor->toPlainText();
    if (!text.endsWith(QLatin1Char('\n'))) {
        text += QLatin1Char('\n');
    }
    QString error;
    if (!spec.path.isEmpty()) {
        if (!CodeLibrary::writeFile(spec.path, text, &error)) {
            QMessageBox::warning(this, tr("Save"), tr("Cannot write %1: %2")
                                                       .arg(QDir::toNativeSeparators(spec.path), error));
            return false;
        }
        m_loadedSource = text;
        if (m_editor->toPlainText() != text) {
            m_updating = true;
            const int position = m_editor->textCursor().position();
            m_editor->setPlainText(text);
            QTextCursor cursor = m_editor->textCursor();
            cursor.setPosition(qMin(position, int(text.size())));
            m_editor->setTextCursor(cursor);
            m_updating = false;
        }
        m_device->reload(m_block);
        emit statusMessage(tr("Saved %1").arg(QFileInfo(spec.path).fileName()));
    } else if (!spec.templateId.isEmpty()) {
        // A template stays as shipped: the edited code becomes a file of yours.
        const BlockKind kind =
            m_device->blockKind(m_block);
        const QString path =
            CodeLibrary::instance().createFile(kind, spec.templateId, QString(), &error);
        const QString withOrigin = CodeLibrary::withOrigin(text, spec.templateId);
        if (path.isEmpty() || !CodeLibrary::writeFile(path, withOrigin, &error)) {
            QMessageBox::warning(this, tr("Save"), error);
            return false;
        }
        spec.path = path;
        spec.templateId.clear();
        m_loadedSource = withOrigin;
        m_updating = true;
        m_editor->setPlainText(withOrigin);
        m_updating = false;
        m_device->setSpec(m_block, spec);
        emit statusMessage(tr("Saved as %1").arg(QDir::toNativeSeparators(path)));
    } else {
        return false;
    }
    refreshHeader();
    return true;
}

void BlockEditor::revertToOriginal() {
    if (!m_device) {
        return;
    }
    const CodeBlockSpec spec = m_device->spec(m_block);
    const QString text = m_editor->toPlainText();
    const QString original = spec.path.isEmpty()
                                 ? CodeLibrary::instance().templateSource(spec.templateId)
                                 : CodeLibrary::instance().originalOf(text);
    if (original.isEmpty()) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Back to the original"),
        spec.path.isEmpty()
            ? tr("Discard your changes and go back to the template?")
            : tr("Replace the code of %1 with the original template? Your version of the file is "
                 "overwritten.")
                  .arg(QFileInfo(spec.path).fileName()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }
    m_editor->setPlainText(original);
    if (!spec.path.isEmpty()) {
        save();
    } else {
        m_loadedSource = original;
        refreshHeader();
    }
}

void BlockEditor::refreshMessages() {
    m_messages->clear();
    QVector<CodeEditor::Mark> marks;
    if (!m_device) {
        m_editor->setMarks(marks);
        return;
    }
    const traceview::ThemePalette& palette = traceview::ThemeManager::instance().currentTheme();
    const QString fault = m_device->runtimeError(m_block);
    if (!fault.isEmpty()) {
        auto* item = new QListWidgetItem(tr("Stopped: %1 -- fix it and save, or reset the "
                                            "simulation.")
                                             .arg(fault),
                                         m_messages);
        item->setForeground(palette.danger);
        static const QRegularExpression line(QStringLiteral(":(\\d+):"));
        const auto m = line.match(fault);
        if (m.hasMatch()) {
            item->setData(Qt::UserRole, m.captured(1).toInt());
            marks.append({m.captured(1).toInt(), false, fault});
        }
    }
    const QVector<code::CodeMessage> messages = m_device->messages(m_block);
    bool anyError = false;
    for (const code::CodeMessage& message : messages) {
        const QString text = message.line > 0
                                 ? tr("Line %1: %2").arg(message.line).arg(message.text)
                                 : message.text;
        auto* item = new QListWidgetItem(
            (message.warning ? tr("warning") : tr("error")) + QStringLiteral(" -- ") + text,
            m_messages);
        item->setForeground(message.warning ? palette.warning : palette.danger);
        item->setData(Qt::UserRole, message.line);
        anyError = anyError || !message.warning;
        if (message.line > 0) {
            marks.append({message.line, message.warning, message.text});
        }
    }
    if (anyError && m_device->compiled(m_block)) {
        auto* item = new QListWidgetItem(
            tr("The previous version keeps running until this one compiles."), m_messages);
        item->setForeground(palette.textSecondary);
    }
    if (messages.isEmpty() && fault.isEmpty() && m_device->compiled(m_block)) {
        auto* item = new QListWidgetItem(tr("Compiled, no messages."), m_messages);
        item->setForeground(palette.success);
    }
    m_editor->setMarks(marks);
}

void BlockEditor::rebuildParams() {
    const code::CodeModule* module = m_device ? m_device->module(m_block) : nullptr;
    QStringList names;
    if (module) {
        for (const Declaration* decl : module->params()) {
            names.append(QStringLiteral("%1/%2/%3/%4/%5")
                             .arg(decl->name)
                             .arg(int(decl->kind))
                             .arg(decl->minimum)
                             .arg(decl->maximum)
                             .arg(decl->options.join(QLatin1Char('|'))));
        }
    }
    if (names == m_paramNames) {
        refreshParamValues();
        return;
    }
    m_paramNames = names;
    while (m_paramsForm->rowCount() > 0) {
        m_paramsForm->removeRow(0);
    }
    m_defaultsButton->setVisible(module && !module->params().isEmpty());
    if (!module || module->params().isEmpty()) {
        auto* none = new QLabel(module ? tr("No TL_PARAM in the code.") : tr("Not compiled."),
                                m_paramsBox);
        none->setObjectName(QStringLiteral("labNote"));
        m_paramsForm->addRow(none);
        return;
    }
    for (const Declaration* decl : module->params()) {
        const QString name = decl->name;
        auto* label = new QLabel(name, m_paramsBox);
        label->setObjectName(QStringLiteral("labFieldLabel"));
        QWidget* field = nullptr;
        const bool ranged = decl->maximum > decl->minimum;
        switch (decl->kind) {
            case DeclKind::ParamBool: {
                auto* check = new QCheckBox(m_paramsBox);
                connect(check, &QCheckBox::toggled, this, [this, name](bool on) {
                    if (!m_updating && m_device) {
                        m_device->setParam(m_block, name, on ? 1.0 : 0.0);
                    }
                });
                field = check;
                break;
            }
            case DeclKind::ParamEnum: {
                auto* combo = new QComboBox(m_paramsBox);
                combo->addItems(decl->options);
                connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                        [this, name](int index) {
                            if (!m_updating && m_device && index >= 0) {
                                m_device->setParam(m_block, name, index);
                            }
                        });
                field = combo;
                break;
            }
            case DeclKind::ParamInt: {
                auto* spin = new QSpinBox(m_paramsBox);
                spin->setRange(ranged ? int(decl->minimum) : -1000000000,
                               ranged ? int(decl->maximum) : 1000000000);
                connect(spin, &QSpinBox::editingFinished, this, [this, name, spin] {
                    if (!m_updating && m_device) {
                        m_device->setParam(m_block, name, spin->value());
                    }
                });
                field = spin;
                break;
            }
            default: {
                auto* spin = new traceview::DoubleSpinBox(m_paramsBox);
                spin->setRange(ranged ? decl->minimum : -1e12, ranged ? decl->maximum : 1e12);
                spin->setDecimals(decimalsFor(*decl));
                if (!decl->unit.isEmpty()) {
                    spin->setSuffix(QLatin1Char(' ') + decl->unit);
                }
                connect(spin, &QDoubleSpinBox::editingFinished, this, [this, name, spin] {
                    if (!m_updating && m_device) {
                        m_device->setParam(m_block, name, spin->value());
                    }
                });
                field = spin;
                break;
            }
        }
        field->setObjectName(name);
        field->setMinimumWidth(kFieldWidth);
        field->setToolTip(ranged ? tr("%1, from %2 to %3 (in the code: %4)")
                                       .arg(name)
                                       .arg(decl->minimum)
                                       .arg(decl->maximum)
                                       .arg(decl->defaultValue)
                                 : tr("%1 (in the code: %2)").arg(name).arg(decl->defaultValue));
        m_paramsForm->addRow(label, field);
    }
    refreshParamValues();
}

void BlockEditor::refreshParamValues() {
    const code::CodeModule* module = m_device ? m_device->module(m_block) : nullptr;
    if (!module) {
        return;
    }
    m_updating = true;
    for (const Declaration* decl : module->params()) {
        QWidget* field = m_paramsBox->findChild<QWidget*>(decl->name);
        if (!field || field->hasFocus()) {
            continue;
        }
        const double value = module->paramValue(decl->name);
        if (auto* check = qobject_cast<QCheckBox*>(field)) {
            check->setChecked(value != 0.0);
        } else if (auto* combo = qobject_cast<QComboBox*>(field)) {
            combo->setCurrentIndex(int(value));
        } else if (auto* spin = qobject_cast<QSpinBox*>(field)) {
            spin->setValue(int(value));
        } else if (auto* dspin = qobject_cast<QDoubleSpinBox*>(field)) {
            dspin->setValue(value);
        }
    }
    m_updating = false;
}

void BlockEditor::refreshSignals() {
    if (!m_device) {
        m_signals->clear();
        return;
    }
    const QVector<SignalRow> rows = m_device->signalRows(m_block);
    // Same rows: update the texts in place (keeps the selection and scroll).
    if (m_signals->count() == rows.size()) {
        for (int i = 0; i < rows.size(); ++i) {
            if (m_signals->item(i)->text() != rows[i].text) {
                m_signals->item(i)->setText(rows[i].text);
            }
        }
        return;
    }
    m_signals->clear();
    const traceview::ThemePalette& palette = traceview::ThemeManager::instance().currentTheme();
    for (const SignalRow& row : rows) {
        auto* item = new QListWidgetItem(row.text, m_signals);
        item->setData(Qt::UserRole, row.declaration);
        if (row.header) {
            item->setForeground(palette.textSecondary);
            item->setFlags(Qt::NoItemFlags);
        } else if (!row.declaration.isEmpty()) {
            item->setToolTip(tr("Double-click: %1").arg(row.declaration));
        }
    }
}

void BlockEditor::refreshLive() {
    if (!m_device) {
        m_binding->clear();
        m_previewNote->hide();
        m_signals->clear();
        return;
    }
    const traceview::ThemePalette& palette = traceview::ThemeManager::instance().currentTheme();
    const CodeBlockOwner::Status status = m_device->status(m_block);
    const QColor tones[] = {palette.textSecondary, palette.success, palette.warning, palette.danger};
    m_binding->setText(QStringLiteral("<span style='color:%1'>%2</span>")
                           .arg(tones[qBound(0, status.tone, 3)].name(),
                                status.text.toHtmlEscaped()));
    const QString note = m_device->note(m_block);
    m_previewNote->setText(note);
    m_previewNote->setVisible(!note.isEmpty());

    const QStringList lines = m_device->takeLog(m_block);
    for (const QString& line : lines) {
        m_log->appendPlainText(line);
    }
    // A new fault shows in the messages.
    const QString fault = m_device->runtimeError(m_block);
    if (fault != m_messages->property("fault").toString()) {
        m_messages->setProperty("fault", fault);
        refreshMessages();
    }
    refreshSignals();
    if (!m_editor->hasFocus()) {
        refreshParamValues();
    }
}

}  // namespace tracelab
