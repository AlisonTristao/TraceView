#include "blockpage.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

#include "lab/code/codefiles.h"
#include "lab/module/codeeditor.h"
#include "lab/module/codewindow.h"
#include "lab/module/externaleditor.h"
#include "lab/module/labui.h"
#include "theme/doublespinbox.h"
#include "traceview/thememanager.h"

namespace tracelab {

using code::BlockKind;
using code::CodeLibrary;
using code::DeclKind;
using code::Declaration;

namespace {

constexpr int kMaxLogLines = 2000;
constexpr int kFieldWidth = 120;
constexpr int kFlashMs = 6000;
constexpr int kCodeMinWidth = 280;
constexpr int kParamsMinWidth = 240;
constexpr int kSignalsMinWidth = 260;

// What an entry of the source combo stands for.
enum SourceType { Header = 0, Template, File, OpenFile, NewFile };

// The "Open the code" menu.
enum OpenHow { InVsCode = 0, InWindow, InOtherProgram, ShowFolder };

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

BlockPage::BlockPage(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    // The header: what the block is; its actions on the right, the primary
    // one last.
    m_header = new LabPageHeader(this);
    m_removeButton = new QPushButton(tr("Remove from the loop"), m_header);
    m_removeButton->setToolTip(tr("Takes this filter out of the chain (its file stays)."));
    m_header->addAction(m_removeButton);
    m_openButton = new QPushButton(tr("Open the code"), m_header);
    m_openButton->setProperty("labMenu", true);
    ui::makePrimary(m_openButton);
    auto* openMenu = new QMenu(m_openButton);
    QAction* vsCode = openMenu->addAction(tr("In VS Code"));
    vsCode->setData(InVsCode);
    QAction* window = openMenu->addAction(tr("In a window of its own"));
    window->setData(InWindow);
    openMenu->addSeparator();
    QAction* other = openMenu->addAction(external::hasChooser() ? tr("In another program...")
                                                                : tr("In the app for .c files"));
    other->setData(InOtherProgram);
    QAction* folder = openMenu->addAction(tr("Show in the folder"));
    folder->setData(ShowFolder);
    m_openButton->setMenu(openMenu);
    m_openButton->setToolTip(tr("The block's code: in VS Code (TraceLab watches the file -- saving "
                                "it there compiles it here) or in a window of TraceLab's own. A "
                                "template is copied to your folder before it opens elsewhere."));
    connect(openMenu, &QMenu::aboutToShow, this, [vsCode] {
        const bool found = !external::findVsCode().isEmpty();
        vsCode->setEnabled(found);
        vsCode->setText(found ? tr("In VS Code") : tr("In VS Code (not found)"));
    });
    connect(openMenu, &QMenu::triggered, this,
            [this](QAction* action) { openExternally(action->data().toInt()); });
    m_header->addAction(m_openButton);
    layout->addWidget(m_header);

    // The columns.
    m_columns = new QHBoxLayout;
    m_columns->setSpacing(12);
    layout->addLayout(m_columns, 1);

    // Where the code comes from, and what the compiler and the code said.
    m_codeSection = new LabSection(tr("Code"), this);
    m_codeSection->setMinimumWidth(kCodeMinWidth);
    auto* sourceRow = new QHBoxLayout;
    sourceRow->setSpacing(8);
    sourceRow->addWidget(ui::fieldLabel(tr("Source"), m_codeSection));
    m_sourceCombo = new QComboBox(m_codeSection);
    m_sourceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_sourceCombo->setMinimumContentsLength(14);
    m_sourceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_sourceCombo->setToolTip(tr("Where the block's code comes from: a template (the original, "
                                 "read-only; saving makes a copy of yours), one of your files, "
                                 "another file, or a new one."));
    sourceRow->addWidget(m_sourceCombo, 1);
    m_codeSection->body()->addLayout(sourceRow);
    m_origin = ui::note(QString(), m_codeSection);
    m_origin->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_codeSection->body()->addWidget(m_origin);
    m_revertButton = new QPushButton(tr("Back to the original"), m_codeSection);
    m_codeSection->body()->addWidget(m_revertButton, 0, Qt::AlignLeft);
    auto* tabs = new QTabWidget(m_codeSection);
    tabs->setDocumentMode(true);
    tabs->tabBar()->setDrawBase(false);
    m_messages = new QListWidget(tabs);
    m_messages->setWordWrap(true);
    tabs->addTab(m_messages, tr("Messages"));
    m_log = new QPlainTextEdit(tabs);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(kMaxLogLines);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setPlaceholderText(tr("What printf() and tl_log() write shows here."));
    tabs->addTab(m_log, tr("Log"));
    m_codeSection->body()->addWidget(tabs, 1);
    m_columns->addWidget(m_codeSection, 4);

    // The parameters, live.
    m_paramsSection = new LabSection(tr("Parameters"), this);
    m_paramsSection->setMinimumWidth(kParamsMinWidth);
    m_defaultsButton = new QToolButton(m_paramsSection);
    m_defaultsButton->setText(tr("Defaults"));
    m_defaultsButton->setAutoRaise(true);
    m_defaultsButton->setToolTip(tr("Every parameter back to the value written in the code."));
    m_paramsSection->titleRow()->addWidget(m_defaultsButton);
    auto* paramsScroll = new QScrollArea(m_paramsSection);
    paramsScroll->setWidgetResizable(true);
    paramsScroll->setFrameShape(QFrame::NoFrame);
    paramsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    paramsScroll->viewport()->setAutoFillBackground(false);
    m_paramsBox = new QWidget(paramsScroll);
    m_paramsBox->setAutoFillBackground(false);
    auto* paramsLayout = new QVBoxLayout(m_paramsBox);
    paramsLayout->setContentsMargins(0, 0, 0, 0);
    m_paramsForm = new QFormLayout;
    m_paramsForm->setContentsMargins(0, 0, 0, 0);
    m_paramsForm->setHorizontalSpacing(12);
    m_paramsForm->setVerticalSpacing(6);
    m_paramsForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    paramsLayout->addLayout(m_paramsForm);
    paramsLayout->addStretch();
    paramsScroll->setWidget(m_paramsBox);
    m_paramsSection->body()->addWidget(paramsScroll, 1);
    m_columns->addWidget(m_paramsSection, 3);

    // The signals it can read, with their values.
    m_signalsSection = new LabSection(tr("Signals"), this);
    m_signalsSection->setMinimumWidth(kSignalsMinWidth);
    m_signals = new QListWidget(m_signalsSection);
    m_signals->setObjectName(QStringLiteral("labSignals"));
    m_signals->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_signals->setToolTip(tr("Double-click one: its declaration goes into the open code window "
                             "(or to the clipboard)."));
    m_signalsSection->body()->addWidget(m_signals, 1);
    m_signalsSection->body()->addWidget(
        ui::note(tr("Double-click: declare it in the code."), m_signalsSection));
    m_columns->addWidget(m_signalsSection, 4);

    connect(m_removeButton, &QPushButton::clicked, this, [this] {
        if (CodeWindow::closeFrom(m_owner, m_block)) {
            emit removeRequested(m_block);
        }
    });
    connect(m_revertButton, &QPushButton::clicked, this, &BlockPage::revertToOriginal);
    connect(m_sourceCombo, qOverload<int>(&QComboBox::activated), this, &BlockPage::pickSource);
    const auto goToLine = [this](QListWidgetItem* item) {
        const int line = item->data(Qt::UserRole).toInt();
        if (line > 0 && m_owner) {
            CodeWindow::open(m_owner, m_block)->editor()->goToLine(line);
        }
    };
    connect(m_messages, &QListWidget::itemActivated, this, goToLine);
    connect(m_signals, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const QString declaration = item->data(Qt::UserRole).toString();
        if (declaration.isEmpty()) {
            return;
        }
        if (CodeWindow* window = CodeWindow::find(m_owner, m_block)) {
            window->insertText(declaration);
        } else {
            QApplication::clipboard()->setText(declaration);
            flash(tr("Copied: %1").arg(declaration));
        }
    });
    connect(m_defaultsButton, &QToolButton::clicked, this, [this] {
        const code::CodeModule* module = m_owner ? m_owner->module(m_block) : nullptr;
        if (!module) {
            return;
        }
        for (const Declaration* decl : module->params()) {
            m_owner->setParam(m_block, decl->name, decl->defaultValue);
        }
        refreshParamValues();
    });
    connect(&traceview::ThemeManager::instance(), &traceview::ThemeManager::themeChanged, this,
            &BlockPage::applyTheme);
    setBlock(nullptr, 0);
}

void BlockPage::applyTheme() {
    refreshMessages();
    refreshLive();
}

void BlockPage::setAutoHeader(bool automatic) {
    m_autoHeader = automatic;
    refreshHeader();
}

void BlockPage::addLeadingColumn(QWidget* column, int stretch) {
    m_columns->insertWidget(m_leadingColumns++, column, stretch);
}

void BlockPage::flash(const QString& text) {
    m_flash = text;
    m_flashUntil = QDateTime::currentMSecsSinceEpoch() + kFlashMs;
    refreshHeader();
}

QString BlockPage::kindLabel() const {
    if (!m_owner) {
        return QString();
    }
    switch (m_owner->blockKind(m_block)) {
        case BlockKind::Plant:
            return tr("plant");
        case BlockKind::Filter:
            return tr("filter");
        case BlockKind::Controller:
            break;
    }
    return tr("controller");
}

void BlockPage::setBlock(CodeBlockOwner* owner, int block) {
    if (m_owner) {
        disconnect(m_owner, nullptr, this, nullptr);
    }
    m_owner = owner;
    m_block = block;
    m_paramNames.clear();
    m_log->clear();
    m_flashUntil = 0;
    if (owner) {
        connect(owner, &CodeBlockOwner::blocksChanged, this, &BlockPage::onBlocksChanged);
        connect(owner, &CodeBlockOwner::statusChanged, this, &BlockPage::refreshLive);
        owner->takeLog(block);  // what it wrote before is not news
    }
    for (QWidget* section : {static_cast<QWidget*>(m_codeSection),
                             static_cast<QWidget*>(m_paramsSection),
                             static_cast<QWidget*>(m_signalsSection)}) {
        section->setVisible(owner != nullptr);
    }
    m_openButton->setVisible(owner != nullptr);
    m_removeButton->setVisible(owner && owner->canRemove(block));
    refreshSourceCombo();
    refreshOrigin();
    rebuildParams();
    refreshMessages();
    refreshLive();
}

void BlockPage::onBlocksChanged() {
    if (!m_owner) {
        return;
    }
    // A block removed can leave this index pointing past the owner's.
    if (m_owner->blockRole(m_block).isEmpty()) {
        setBlock(nullptr, 0);
        return;
    }
    refreshSourceCombo();
    refreshOrigin();
    rebuildParams();
    refreshMessages();
    refreshLive();
}

void BlockPage::refreshHeader() {
    const bool flashing = QDateTime::currentMSecsSinceEpoch() < m_flashUntil;
    if (!m_autoHeader) {
        if (flashing) {
            m_header->setStatus(m_flash, 0);
        }
        return;
    }
    if (!m_owner) {
        m_header->setKind(QString());
        m_header->setTitle(tr("No block"));
        m_header->setStatus(QString(), 0);
        return;
    }
    m_header->setKind(m_owner->blockRole(m_block) + QStringLiteral("  ·  C"));
    m_header->setTitle(m_owner->blockName(m_block));
    if (flashing) {
        m_header->setStatus(m_flash, 0);
        return;
    }
    // What is wrong with the code comes before how the loop runs.
    const QString fault = m_owner->runtimeError(m_block);
    if (!fault.isEmpty()) {
        m_header->setStatus(tr("stopped: %1").arg(fault), 3);
        return;
    }
    for (const code::CodeMessage& message : m_owner->messages(m_block)) {
        if (!message.warning) {
            m_header->setStatus(
                m_owner->compiled(m_block)
                    ? tr("does not compile (line %1): the previous version runs").arg(message.line)
                    : tr("does not compile (line %1)").arg(message.line),
                3);
            return;
        }
    }
    const CodeBlockOwner::Status status = m_owner->status(m_block);
    m_header->setStatus(status.text, status.tone);
}

void BlockPage::refreshOrigin() {
    if (!m_owner) {
        m_origin->clear();
        m_revertButton->hide();
        return;
    }
    const CodeBlockSpec spec = m_owner->spec(m_block);
    const QString source = m_owner->source(m_block);
    QString templateId = spec.templateId;
    if (templateId.isEmpty()) {
        templateId = CodeLibrary::originOf(source);
    }
    const code::CodeTemplate* t = CodeLibrary::instance().findTemplate(templateId);
    QString text;
    bool modified = false;
    if (spec.path.isEmpty()) {
        text = t ? tr("The template \"%1\" as shipped (read-only: saving it makes a copy of "
                      "yours).")
                       .arg(t->name)
                 : QString();
    } else {
        const QString file = QFileInfo(spec.path).fileName();
        modified = t && CodeLibrary::instance().isModified(source);
        text = !t ? tr("Your file %1.").arg(file)
                  : (modified ? tr("Your file %1, from the template \"%2\", modified.")
                                    .arg(file, t->name)
                              : tr("Your file %1, from the template \"%2\", unchanged.")
                                    .arg(file, t->name));
        m_origin->setToolTip(QDir::toNativeSeparators(spec.path));
    }
    m_origin->setText(text);
    m_revertButton->setVisible(t && !spec.path.isEmpty());
    m_revertButton->setEnabled(modified);
    m_revertButton->setToolTip(t ? tr("Replaces the code with the template \"%1\".").arg(t->name)
                                 : QString());
}

void BlockPage::refreshSourceCombo() {
    m_sourceCombo->clear();
    if (!m_owner) {
        return;
    }
    const BlockKind kind = m_owner->blockKind(m_block);
    const CodeBlockSpec spec = m_owner->spec(m_block);
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
    m_sourceCombo->addItem(tr("New %1 (empty)...").arg(kindLabel()),
                           QVariantList{NewFile, QString()});
    m_sourceCombo->setCurrentIndex(current);
}

void BlockPage::pickSource(int index) {
    if (!m_owner) {
        return;
    }
    const QVariantList data = m_sourceCombo->itemData(index).toList();
    const int type = data.value(0).toInt();
    const QString value = data.value(1).toString();
    const BlockKind kind = m_owner->blockKind(m_block);
    const CodeBlockSpec current = m_owner->spec(m_block);
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
                this, tr("Open a %1").arg(kindLabel()), CodeLibrary::userDirectory(kind),
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
                this, tr("New %1").arg(kindLabel()),
                tr("File name (saved in %1):")
                    .arg(QDir::toNativeSeparators(CodeLibrary::userDirectory(kind))),
                QLineEdit::Normal,
                kind == BlockKind::Controller ? QStringLiteral("my_controller")
                : kind == BlockKind::Filter   ? QStringLiteral("my_filter")
                                              : QStringLiteral("my_plant"),
                &ok);
            if (!ok || name.trimmed().isEmpty()) {
                refreshSourceCombo();
                return;
            }
            QString error;
            spec.path = CodeLibrary::instance().createFile(kind, QString(), name.trimmed(), &error);
            if (spec.path.isEmpty()) {
                QMessageBox::warning(this, tr("New %1").arg(kindLabel()), error);
                refreshSourceCombo();
                return;
            }
            break;
        }
        default:
            refreshSourceCombo();
            return;
    }
    // The open code window's edits are of the old source.
    CodeWindow* window = CodeWindow::find(m_owner, m_block);
    if (window && !window->maybeSave()) {
        refreshSourceCombo();
        return;
    }
    m_owner->setSpec(m_block, spec);
    if (window) {
        window->reload();
    }
}

void BlockPage::revertToOriginal() {
    if (!m_owner) {
        return;
    }
    const CodeBlockSpec spec = m_owner->spec(m_block);
    const QString original = CodeLibrary::instance().originalOf(m_owner->source(m_block));
    if (spec.path.isEmpty() || original.isEmpty()) {
        return;
    }
    CodeWindow* window = CodeWindow::find(m_owner, m_block);
    const auto answer = QMessageBox::question(
        this, tr("Back to the original"),
        tr("Replace the code of %1 with the original template? Your version of the file is "
           "overwritten%2.")
            .arg(QFileInfo(spec.path).fileName(),
                 window && window->hasUnsavedChanges() ? tr(", and the unsaved changes in its "
                                                            "window are dropped")
                                                       : QString()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!CodeLibrary::writeFile(spec.path, original, &error)) {
        QMessageBox::warning(this, tr("Back to the original"), error);
        return;
    }
    m_owner->reload(m_block);
    if (window) {
        window->reload();
    }
    flash(tr("%1 is the template again.").arg(QFileInfo(spec.path).fileName()));
}

void BlockPage::openExternally(int how) {
    if (!m_owner) {
        return;
    }
    switch (how) {
        case InWindow:
            CodeWindow::open(m_owner, m_block);
            return;
        case ShowFolder: {
            const QString path = m_owner->spec(m_block).path;
            QDesktopServices::openUrl(QUrl::fromLocalFile(
                path.isEmpty() ? CodeLibrary::userDirectory(m_owner->blockKind(m_block))
                               : QFileInfo(path).absolutePath()));
            return;
        }
        default:
            break;
    }
    QString message;
    CodeWindow::openExternally(this, m_owner, m_block,
                               how == InVsCode ? CodeWindow::External::VsCode
                                               : CodeWindow::External::OtherProgram,
                               &message);
    if (!message.isEmpty()) {
        flash(message);
    }
}

void BlockPage::refreshMessages() {
    m_messages->clear();
    if (!m_owner) {
        return;
    }
    const traceview::ThemePalette& palette = traceview::ThemeManager::instance().currentTheme();
    m_fault = m_owner->runtimeError(m_block);
    if (!m_fault.isEmpty()) {
        auto* item = new QListWidgetItem(
            tr("Stopped: %1 -- fix it and save, or reset the simulation.").arg(m_fault),
            m_messages);
        item->setForeground(palette.danger);
        static const QRegularExpression line(QStringLiteral(":(\\d+):"));
        const auto m = line.match(m_fault);
        if (m.hasMatch()) {
            item->setData(Qt::UserRole, m.captured(1).toInt());
        }
    }
    const QVector<code::CodeMessage> messages = m_owner->messages(m_block);
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
        if (message.line > 0) {
            item->setToolTip(tr("Double-click: the line in the code window."));
        }
        anyError = anyError || !message.warning;
    }
    if (anyError && m_owner->compiled(m_block)) {
        auto* item = new QListWidgetItem(
            tr("The previous version keeps running until this one compiles."), m_messages);
        item->setForeground(palette.textSecondary);
    }
    if (messages.isEmpty() && m_fault.isEmpty() && m_owner->compiled(m_block)) {
        auto* item = new QListWidgetItem(tr("Compiled, no messages."), m_messages);
        item->setForeground(palette.success);
    }
}

void BlockPage::rebuildParams() {
    const code::CodeModule* module = m_owner ? m_owner->module(m_block) : nullptr;
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
    if (!names.isEmpty() && names == m_paramNames) {
        refreshParamValues();
        return;
    }
    m_paramNames = names;
    while (m_paramsForm->rowCount() > 0) {
        m_paramsForm->removeRow(0);
    }
    m_defaultsButton->setVisible(module && !module->params().isEmpty());
    if (!module || module->params().isEmpty()) {
        m_paramsForm->addRow(ui::note(module ? tr("No TL_PARAM in the code.") : tr("Not compiled."),
                                      m_paramsBox));
        return;
    }
    for (const Declaration* decl : module->params()) {
        const QString name = decl->name;
        QWidget* field = nullptr;
        const bool ranged = decl->maximum > decl->minimum;
        switch (decl->kind) {
            case DeclKind::ParamBool: {
                auto* check = new QCheckBox(m_paramsBox);
                connect(check, &QCheckBox::toggled, this, [this, name](bool on) {
                    if (!m_updating && m_owner) {
                        m_owner->setParam(m_block, name, on ? 1.0 : 0.0);
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
                            if (!m_updating && m_owner && index >= 0) {
                                m_owner->setParam(m_block, name, index);
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
                    if (!m_updating && m_owner) {
                        m_owner->setParam(m_block, name, spin->value());
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
                    if (!m_updating && m_owner) {
                        m_owner->setParam(m_block, name, spin->value());
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
        m_paramsForm->addRow(ui::fieldLabel(name, m_paramsBox), field);
    }
    refreshParamValues();
}

void BlockPage::refreshParamValues() {
    const code::CodeModule* module = m_owner ? m_owner->module(m_block) : nullptr;
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

void BlockPage::refreshSignals() {
    if (!m_owner) {
        m_signals->clear();
        return;
    }
    const QVector<SignalRow> rows = m_owner->signalRows(m_block);
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

void BlockPage::refreshLive() {
    refreshHeader();
    if (!m_owner) {
        m_signals->clear();
        return;
    }
    const QStringList lines = m_owner->takeLog(m_block);
    for (const QString& line : lines) {
        m_log->appendPlainText(line);
    }
    // A new fault shows in the messages.
    if (m_owner->runtimeError(m_block) != m_fault) {
        refreshMessages();
    }
    refreshSignals();
    refreshParamValues();
}

}  // namespace tracelab
