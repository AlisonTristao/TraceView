#include "codewindow.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QVBoxLayout>

#include "lab/code/codefiles.h"
#include "lab/module/codeeditor.h"
#include "lab/module/externaleditor.h"
#include "lab/module/labui.h"
#include "traceview/thememanager.h"

namespace tracelab {

using code::BlockKind;
using code::CodeLibrary;

namespace {

constexpr int kMargin = 14;
constexpr int kWidth = 1000;
constexpr int kHeight = 780;
constexpr int kCascade = 28;
constexpr char kGeometryKey[] = "TraceLab/codeWindowGeometry";

QList<QPointer<CodeWindow>>& windows() {
    static QList<QPointer<CodeWindow>> list;
    list.removeAll(nullptr);
    return list;
}

}  // namespace

CodeWindow* CodeWindow::find(const CodeBlockOwner* owner, int block) {
    for (const QPointer<CodeWindow>& window : windows()) {
        if (window->m_owner == owner && window->m_block == block) {
            return window;
        }
    }
    return nullptr;
}

CodeWindow* CodeWindow::open(CodeBlockOwner* owner, int block) {
    if (!owner) {
        return nullptr;
    }
    CodeWindow* window = find(owner, block);
    if (!window) {
        const int others = int(windows().size());
        window = new CodeWindow(owner, block);
        windows().append(window);
        if (!window->restoreGeometry(QSettings().value(QLatin1String(kGeometryKey)).toByteArray())) {
            window->resize(kWidth, kHeight);
        }
        if (others > 0) {
            window->move(window->pos() + QPoint(kCascade, kCascade) * others);
        }
    }
    window->showNormal();
    window->raise();
    window->activateWindow();
    window->m_editor->setFocus();
    return window;
}

bool CodeWindow::closeFrom(const CodeBlockOwner* owner, int firstBlock) {
    const QList<QPointer<CodeWindow>> list = windows();
    for (const QPointer<CodeWindow>& window : list) {
        if (window && window->m_owner == owner && window->m_block >= firstBlock) {
            if (!window->maybeSave()) {
                return false;
            }
            window->m_loadedSource = window->m_editor->toPlainText();  // no second question
            window->close();
        }
    }
    return true;
}

bool CodeWindow::openExternally(QWidget* dialogParent, CodeBlockOwner* owner, int block,
                                External how, QString* message) {
    const auto fail = [message](const QString& text) {
        if (message) {
            *message = text;
        }
        return false;
    };
    if (!owner) {
        return false;
    }
    // A file to open: edits in its window saved (or dropped) first, a
    // template made a copy of the user's.
    CodeWindow* window = find(owner, block);
    if (window && !window->maybeSave()) {
        return false;
    }
    CodeBlockSpec spec = owner->spec(block);
    if (spec.path.isEmpty()) {
        if (spec.templateId.isEmpty()) {
            return fail(tr("This block has no code to open."));
        }
        QString error;
        const QString path = CodeLibrary::instance().createFile(owner->blockKind(block),
                                                                spec.templateId, QString(), &error);
        if (path.isEmpty()) {
            QMessageBox::warning(dialogParent, tr("Open the code"), error);
            return fail(error);
        }
        spec.path = path;
        spec.templateId.clear();
        owner->setSpec(block, spec);
        if (window) {
            window->reload();
        }
    }
    QString configError;
    const bool configured = CodeLibrary::writeEditorConfig(&configError);
    const int line = window ? window->m_editor->textCursor().blockNumber() + 1 : 0;
    bool opened = false;
    QString program;
    switch (how) {
        case External::VsCode:
            opened = external::openInVsCode(external::findVsCode(), spec.path, line,
                                            CodeLibrary::userRoot());
            program = tr("VS Code");
            break;
        case External::OtherProgram:
            opened = external::hasChooser() ? external::openWithChooser(spec.path)
                                            : external::openWithDefaultApp(spec.path);
            program = tr("the editor");
            break;
    }
    const QString file = QFileInfo(spec.path).fileName();
    if (!opened) {
        const QString text = tr("Could not open %1 in %2.").arg(file, program);
        QMessageBox::warning(dialogParent, tr("Open the code"), text);
        return fail(text);
    }
    QString text = tr("%1 opened in %2: saving it there compiles it here.").arg(file, program);
    if (!configured) {
        text += QLatin1Char(' ') +
                tr("(Could not write the editor configuration: %1)").arg(configError);
    }
    if (message) {
        *message = text;
    }
    return true;
}

CodeWindow::CodeWindow(CodeBlockOwner* owner, int block)
    : QWidget(nullptr, Qt::Window), m_owner(owner), m_block(block) {
    setAttribute(Qt::WA_DeleteOnClose);
    // TraceView quits when its main window closes, open code or not.
    setAttribute(Qt::WA_QuitOnClose, false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
    layout->setSpacing(10);

    auto* header = new QHBoxLayout;
    header->setSpacing(8);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(2);
    m_title = new QLabel(this);
    QFont titleFont = m_title->font();
    if (titleFont.pointSizeF() > 0) {
        titleFont.setPointSizeF(titleFont.pointSizeF() * 1.25);
    }
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    titles->addWidget(m_title);
    m_origin = ui::note(QString(), this);
    m_origin->setWordWrap(false);
    m_origin->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titles->addWidget(m_origin);
    header->addLayout(titles, 1);
    m_vsCodeButton = new QPushButton(tr("Open in VS Code"), this);
    m_vsCodeButton->setToolTip(tr("Opens this file in VS Code at the cursor's line; saving it "
                                  "there compiles it here."));
    header->addWidget(m_vsCodeButton, 0, Qt::AlignVCenter);
    m_saveButton = new QPushButton(tr("Save"), this);
    m_saveButton->setToolTip(tr("Saves and compiles the code (Ctrl+S). A template is saved as a "
                                "copy in your folder."));
    ui::makePrimary(m_saveButton);
    header->addWidget(m_saveButton, 0, Qt::AlignVCenter);
    layout->addLayout(header);

    auto* split = new QSplitter(Qt::Vertical, this);
    split->setChildrenCollapsible(false);
    m_editor = new CodeEditor(split);
    m_messages = new QListWidget(split);
    m_messages->setWordWrap(true);
    m_messages->setMinimumHeight(48);
    split->addWidget(m_editor);
    split->addWidget(m_messages);
    split->setStretchFactor(0, 6);
    split->setStretchFactor(1, 1);
    split->setSizes({kHeight - 220, 100});
    layout->addWidget(split, 1);

    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    saveShortcut->setContext(Qt::WindowShortcut);
    connect(saveShortcut, &QShortcut::activated, this, [this] { save(); });
    connect(m_saveButton, &QPushButton::clicked, this, [this] { save(); });
    connect(m_vsCodeButton, &QPushButton::clicked, this, [this] {
        QString message;
        openExternally(this, m_owner, m_block, External::VsCode, &message);
        if (!message.isEmpty()) {
            m_origin->setToolTip(message);
        }
    });
    connect(m_editor, &QPlainTextEdit::textChanged, this, &CodeWindow::refreshHeader);
    const auto goToLine = [this](QListWidgetItem* item) {
        const int line = item->data(Qt::UserRole).toInt();
        if (line > 0) {
            m_editor->goToLine(line);
        }
    };
    connect(m_messages, &QListWidget::itemClicked, this, goToLine);
    connect(m_messages, &QListWidget::itemActivated, this, goToLine);
    connect(owner, &CodeBlockOwner::blocksChanged, this, &CodeWindow::onBlocksChanged);
    connect(owner, &CodeBlockOwner::statusChanged, this, [this] {
        if (m_owner && m_owner->runtimeError(m_block) != m_fault) {
            refreshMessages();
        }
    });
    // The block's device going away takes its window along.
    connect(owner, &QObject::destroyed, this, &QObject::deleteLater);
    connect(&traceview::ThemeManager::instance(), &traceview::ThemeManager::themeChanged, this,
            &CodeWindow::applyTheme);
    applyTheme();
    reload();
}

CodeWindow::~CodeWindow() {
    QSettings().setValue(QLatin1String(kGeometryKey), saveGeometry());
}

void CodeWindow::applyTheme() {
    setStyleSheet(ui::styleSheet(traceview::ThemeManager::instance().currentTheme()));
    m_vsCodeButton->setVisible(!external::findVsCode().isEmpty());
    refreshMessages();
}

bool CodeWindow::hasUnsavedChanges() const {
    return m_owner && m_editor->toPlainText() != m_loadedSource;
}

void CodeWindow::reload() {
    m_loadedSource = m_owner ? m_owner->source(m_block) : QString();
    m_editor->setPlainText(m_loadedSource);
    m_editor->document()->setModified(false);
    refreshHeader();
    refreshMessages();
}

void CodeWindow::insertText(const QString& text) {
    m_editor->textCursor().insertText(text);
    raise();
    activateWindow();
    m_editor->setFocus();
}

void CodeWindow::onBlocksChanged() {
    if (!m_owner) {
        return;
    }
    // Its block taken out of the loop.
    if (m_owner->blockRole(m_block).isEmpty()) {
        m_loadedSource = m_editor->toPlainText();
        close();
        return;
    }
    // New code from outside (a file saved elsewhere, another source picked):
    // shown unless the editor holds changes of its own.
    if (m_owner->source(m_block) != m_loadedSource && !hasUnsavedChanges()) {
        reload();
        return;
    }
    refreshHeader();
    refreshMessages();
}

void CodeWindow::refreshHeader() {
    if (!m_owner) {
        return;
    }
    const CodeBlockSpec spec = m_owner->spec(m_block);
    const QString text = m_editor->toPlainText();
    QString title = m_owner->blockName(m_block);
    if (hasUnsavedChanges()) {
        title += QStringLiteral(" *");
    }
    m_title->setText(title);
    const QString file = spec.path.isEmpty() ? QString() : QFileInfo(spec.path).fileName();
    setWindowTitle(tr("%1 -- %2 -- TraceLab code")
                       .arg(file.isEmpty() ? m_owner->blockName(m_block) : file,
                            m_owner->blockRole(m_block)) +
                   (hasUnsavedChanges() ? QStringLiteral(" *") : QString()));
    QStringList origin;
    origin.append(m_owner->blockRole(m_block));
    if (!spec.path.isEmpty()) {
        origin.append(QDir::toNativeSeparators(spec.path));
    }
    QString templateId = spec.templateId;
    if (templateId.isEmpty()) {
        templateId = CodeLibrary::originOf(text);
    }
    if (const code::CodeTemplate* t = CodeLibrary::instance().findTemplate(templateId)) {
        origin.append(spec.path.isEmpty()
                          ? tr("template \"%1\" (read-only: Save makes a copy)").arg(t->name)
                          : (CodeLibrary::instance().isModified(text)
                                 ? tr("from the template \"%1\", modified").arg(t->name)
                                 : tr("from the template \"%1\", unchanged").arg(t->name)));
    }
    m_origin->setText(origin.join(QStringLiteral("  ·  ")));
    m_saveButton->setEnabled(hasUnsavedChanges() ||
                             (!spec.path.isEmpty() && !m_owner->compiled(m_block)));
}

void CodeWindow::refreshMessages() {
    m_messages->clear();
    QVector<CodeEditor::Mark> marks;
    if (!m_owner) {
        m_editor->setMarks(marks);
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
            marks.append({m.captured(1).toInt(), false, m_fault});
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
        anyError = anyError || !message.warning;
        if (message.line > 0) {
            marks.append({message.line, message.warning, message.text});
        }
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
    m_editor->setMarks(marks);
}

bool CodeWindow::maybeSave() {
    if (!hasUnsavedChanges()) {
        return true;
    }
    raise();
    activateWindow();
    const auto answer = QMessageBox::question(
        this, tr("Unsaved code"),
        tr("The code of %1 has changes that were not saved.").arg(m_owner->blockName(m_block)),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) {
        return false;
    }
    if (answer == QMessageBox::Save) {
        return save();
    }
    reload();
    return true;
}

bool CodeWindow::save() {
    if (!m_owner) {
        return false;
    }
    CodeBlockSpec spec = m_owner->spec(m_block);
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
            const int position = m_editor->textCursor().position();
            m_editor->setPlainText(text);
            QTextCursor cursor = m_editor->textCursor();
            cursor.setPosition(qMin(position, int(text.size())));
            m_editor->setTextCursor(cursor);
        }
        m_owner->reload(m_block);
    } else if (!spec.templateId.isEmpty()) {
        // A template stays as shipped: the edited code becomes a file of yours.
        const QString path = CodeLibrary::instance().createFile(m_owner->blockKind(m_block),
                                                                spec.templateId, QString(), &error);
        const QString withOrigin = CodeLibrary::withOrigin(text, spec.templateId);
        if (path.isEmpty() || !CodeLibrary::writeFile(path, withOrigin, &error)) {
            QMessageBox::warning(this, tr("Save"), error);
            return false;
        }
        spec.path = path;
        spec.templateId.clear();
        m_loadedSource = withOrigin;
        const int position = m_editor->textCursor().position();
        m_editor->setPlainText(withOrigin);
        QTextCursor cursor = m_editor->textCursor();
        cursor.setPosition(qMin(position + int(withOrigin.size() - text.size()),
                                int(withOrigin.size())));
        m_editor->setTextCursor(cursor);
        m_owner->setSpec(m_block, spec);
    } else {
        return false;
    }
    refreshHeader();
    return true;
}

void CodeWindow::closeEvent(QCloseEvent* event) {
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    QWidget::closeEvent(event);
}

}  // namespace tracelab
