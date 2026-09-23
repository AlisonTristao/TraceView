#include "dialogpresenter.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <utility>

#include "traceview/fontmanager.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

QPointer<QWidget> s_host;
bool s_embedded = false;

// Card geometry. kCardInset is the gap between the card's rounded outline
// and its scroll area -- enough that the scroll area's square corners stay
// inside the kCardRadius arc.
constexpr int kScreenMargin = 16;
constexpr int kCardInset = 6;
constexpr int kCardTitleSpacing = 4;
constexpr qreal kCardRadius = 10.0;
constexpr int kBackdropAlpha = 140;
// Page top bar.
constexpr int kHeaderPadding = 8;

class DialogOverlay;
// Live overlays, oldest first -- the last one is the topmost, the one the
// Back key closes.
QList<DialogOverlay*> s_overlays;

// Covers the host and carries one embedded dialog (see dialogpresenter.h).
// Q_OBJECT is load-bearing beyond moc: the global stylesheet's
// "QWidget { background-color }" rule gives every widget whose metaObject
// is exactly QWidget's an opaque styled background, which would paint over
// the Card style's translucent backdrop.
class DialogOverlay : public QWidget {
    Q_OBJECT

public:
    DialogOverlay(QWidget* host, QDialog* dialog, DialogPresenter::Style style,
                  bool releaseOnFinish)
        : QWidget(host),
          m_host(host),
          m_dialog(dialog),
          m_style(style),
          m_originalParent(dialog->parentWidget()),
          m_originalFlags(dialog->windowFlags()),
          m_originalMinimumSize(dialog->minimumSize()),
          m_originalMoved(dialog->testAttribute(Qt::WA_Moved)),
          m_originalAutoFill(dialog->autoFillBackground()) {
        s_overlays.append(this);
        // A dialog's explicit minimum width is its author's idea of a
        // comfortable width -- keep it as the card's preferred width, but
        // drop it as a hard minimum so a phone narrower than that still
        // gets the whole dialog instead of a horizontal scrollbar.
        m_preferredWidth = qMax(m_originalMinimumSize.width(), dialog->sizeHint().width());
        if (dialog->layout() != nullptr) {
            m_contentMargins = dialog->layout()->contentsMargins();
        }

        m_title = new QLabel(dialog->windowTitle(), this);
        m_title->setWordWrap(true);
        QFont titleFont = m_title->font();
        titleFont.setBold(true);
        m_title->setFont(titleFont);

        if (m_style == DialogPresenter::Style::Page) {
            m_backButton = new QToolButton(this);
            m_backButton->setText(QString(QChar(0x2190)));  // leftwards arrow
            m_backButton->setToolTip(QCoreApplication::translate("DialogPresenter", "Back"));
            m_backButton->setAutoRaise(true);
            m_backButton->setFont(scaledFont(m_backButton->font(), 1.4));
            connect(m_backButton, &QToolButton::clicked, dialog, &QDialog::reject);
        }

        // The content always fits the screen's width -- never a horizontal
        // scrollbar. A Card is sized to its content's full height too, so it
        // only scrolls vertically if it's taller than the whole screen; a
        // Page (long by nature) scrolls vertically like any phone screen.
        m_scroll = new QScrollArea(this);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setWidgetResizable(true);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        // Single-line labels are what usually make a dialog wider than a
        // phone: let them wrap while embedded. Pixmap labels (Donate's QR
        // code) are left alone.
        const QList<QLabel*> labels = dialog->findChildren<QLabel*>();
        for (QLabel* label : labels) {
            if (!label->wordWrap() && label->pixmap().isNull()) {
                label->setWordWrap(true);
                m_wrappedLabels.append(label);
            }
        }
        // Rows of buttons that don't fit side by side get stacked instead;
        // the horizontal width is measured once, up front, so relayout()
        // can switch back when the screen gets wider (rotation).
        const QList<QDialogButtonBox*> buttonBoxes = dialog->findChildren<QDialogButtonBox*>();
        for (QDialogButtonBox* box : buttonBoxes) {
            m_buttonBoxes.append({box, box->orientation(), box->minimumSizeHint().width()});
        }

        dialog->setMinimumSize(0, 0);
        // QDialog::showEvent() centers a dialog over its parent unless
        // WA_Moved is set -- as a child widget that "centering" would shift
        // it inside the scroll area's viewport instead.
        dialog->setAttribute(Qt::WA_Moved, true);
        // Reparents into the viewport; setParent() strips the window type,
        // so the dialog becomes a plain child widget.
        m_scroll->setWidget(dialog);

        dialog->installEventFilter(this);
        host->installEventFilter(this);
        qApp->installEventFilter(this);
        connect(dialog, &QObject::destroyed, this, [this] { discard(); });
        if (releaseOnFinish) {
            connect(dialog, &QDialog::finished, this, &DialogOverlay::release);
        }
        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
                [this] { update(); });

        setGeometry(host->rect());
        raise();
        show();
        relayout();
        QTimer::singleShot(0, this, &DialogOverlay::focusFirstChild);
    }

    ~DialogOverlay() override { s_overlays.removeOne(this); }

    QDialog* dialog() const { return m_dialog; }

    // Hands the dialog back to its original parent -- hidden, and a window
    // again -- and schedules this overlay's deletion. Safe to call twice.
    void release() {
        if (m_released) {
            return;
        }
        m_released = true;
        s_overlays.removeOne(this);
        if (m_dialog) {
            m_dialog->removeEventFilter(this);
            m_scroll->takeWidget();
            m_dialog->hide();
            m_dialog->setParent(m_originalParent, m_originalFlags);
            m_dialog->setMinimumSize(m_originalMinimumSize);
            m_dialog->setAttribute(Qt::WA_Moved, m_originalMoved);
            m_dialog->setAutoFillBackground(m_originalAutoFill);
        }
        for (const QPointer<QLabel>& label : std::as_const(m_wrappedLabels)) {
            if (label) {
                label->setWordWrap(false);
            }
        }
        for (const ButtonBoxState& state : std::as_const(m_buttonBoxes)) {
            if (state.box && state.box->orientation() != state.originalOrientation) {
                state.box->setOrientation(state.originalOrientation);
            }
        }
        hide();
        deleteLater();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        switch (event->type()) {
            case QEvent::Resize:
                if (watched == m_host) {
                    setGeometry(m_host->rect());
                }
                break;
            case QEvent::LayoutRequest:
                // Dialogs that show/hide rows (login errors, transport-
                // specific fields) change their height after the fact.
                if (watched == m_dialog) {
                    QTimer::singleShot(0, this, &DialogOverlay::relayout);
                }
                break;
            case QEvent::WindowTitleChange:
                if (watched == m_dialog) {
                    m_title->setText(m_dialog->windowTitle());
                    relayout();
                }
                break;
            case QEvent::KeyPress:
            case QEvent::KeyRelease: {
                // Android's Back key. Unhandled, it would reach QtActivity
                // and close the whole app; here it closes the topmost
                // embedded dialog instead. Consumed on press too so nothing
                // underneath reacts to half of the pair.
                auto* keyEvent = static_cast<QKeyEvent*>(event);
                if (keyEvent->key() != Qt::Key_Back || s_overlays.isEmpty() ||
                    s_overlays.constLast() != this) {
                    break;
                }
                if (event->type() == QEvent::KeyRelease && m_dialog) {
                    m_dialog->reject();
                }
                event->accept();
                return true;
            }
            default:
                break;
        }
        return QWidget::eventFilter(watched, event);
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        relayout();
    }

    void paintEvent(QPaintEvent*) override {
        const ThemePalette& palette = ThemeManager::instance().currentTheme();
        QPainter painter(this);
        if (m_style == DialogPresenter::Style::Page) {
            painter.fillRect(rect(), palette.background);
            painter.setPen(palette.border);
            painter.drawLine(0, m_headerHeight, width(), m_headerHeight);
            return;
        }
        painter.fillRect(rect(), QColor(0, 0, 0, kBackdropAlpha));
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(palette.border);
        painter.setBrush(palette.background);
        painter.drawRoundedRect(QRectF(m_cardRect).adjusted(0.5, 0.5, -0.5, -0.5), kCardRadius,
                                kCardRadius);
    }

    // The backdrop is modal: taps on it must not fall through to the host.
    void mousePressEvent(QMouseEvent* event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent* event) override { event->accept(); }
    void mouseDoubleClickEvent(QMouseEvent* event) override { event->accept(); }
    void wheelEvent(QWheelEvent* event) override { event->accept(); }

private:
    // The dialog was deleted out from under the overlay (WA_DeleteOnClose,
    // or its owner going away) -- nothing left to hand back.
    void discard() {
        m_released = true;
        s_overlays.removeOne(this);
        hide();
        deleteLater();
    }

    int contentHeightFor(int width) const {
        if (!m_dialog) {
            return 0;
        }
        const int forWidth = m_dialog->hasHeightForWidth() ? m_dialog->heightForWidth(width) : -1;
        return forWidth > 0 ? forWidth : m_dialog->sizeHint().height();
    }

    // Stacks a dialog's button rows vertically when they don't fit
    // `contentWidth` side by side. Only touches boxes whose orientation
    // actually has to change -- setOrientation() always re-lays out,
    // which would bounce straight back here via LayoutRequest.
    void fitButtonBoxes(int contentWidth) {
        const int available =
            contentWidth - m_contentMargins.left() - m_contentMargins.right();
        for (const ButtonBoxState& state : std::as_const(m_buttonBoxes)) {
            if (!state.box) {
                continue;
            }
            const Qt::Orientation wanted =
                state.horizontalWidth > available ? Qt::Vertical : state.originalOrientation;
            if (state.box->orientation() != wanted) {
                state.box->setOrientation(wanted);
            }
        }
    }

    void relayout() {
        if (m_released) {
            return;
        }
        const QRect area = rect();
        if (m_style == DialogPresenter::Style::Page) {
            const QSize backSize = m_backButton->sizeHint();
            const int titleLeft = kHeaderPadding + backSize.width() + kHeaderPadding;
            const int titleWidth = qMax(0, area.width() - titleLeft - kHeaderPadding);
            const int titleHeight = m_title->heightForWidth(titleWidth);
            m_headerHeight = qMax(backSize.height(), titleHeight) + 2 * kHeaderPadding;
            m_backButton->setGeometry(kHeaderPadding, (m_headerHeight - backSize.height()) / 2,
                                      backSize.width(), backSize.height());
            m_title->setGeometry(titleLeft, (m_headerHeight - titleHeight) / 2, titleWidth,
                                 titleHeight);
            m_scroll->setGeometry(0, m_headerHeight + 1, area.width(),
                                  qMax(0, area.height() - m_headerHeight - 1));
            fitButtonBoxes(area.width());
            update();
            return;
        }

        const int maxCardWidth = qMax(0, area.width() - 2 * kScreenMargin);
        const int cardWidth = qMin(maxCardWidth, m_preferredWidth + 2 * kCardInset);
        const int innerWidth = qMax(0, cardWidth - 2 * kCardInset);
        fitButtonBoxes(innerWidth);
        // The title lines up with the dialog's own content, i.e. inside the
        // dialog layout's margins rather than at the card's inset.
        const int titleWidth =
            qMax(0, innerWidth - m_contentMargins.left() - m_contentMargins.right());
        const bool hasTitle = !m_title->text().isEmpty();
        const int titleHeight = hasTitle ? m_title->heightForWidth(titleWidth) : 0;
        const int titleBlock =
            hasTitle ? m_contentMargins.top() + titleHeight + kCardTitleSpacing : 0;
        const int naturalHeight = 2 * kCardInset + titleBlock + contentHeightFor(innerWidth);
        const int maxCardHeight = qMax(0, area.height() - 2 * kScreenMargin);
        const int cardHeight = qMin(maxCardHeight, naturalHeight);

        m_cardRect = QRect((area.width() - cardWidth) / 2, (area.height() - cardHeight) / 2,
                           cardWidth, cardHeight);
        const int innerLeft = m_cardRect.left() + kCardInset;
        int y = m_cardRect.top() + kCardInset;
        m_title->setVisible(hasTitle);
        if (hasTitle) {
            m_title->setGeometry(innerLeft + m_contentMargins.left(), y + m_contentMargins.top(),
                                 titleWidth, titleHeight);
            y += titleBlock;
        }
        m_scroll->setGeometry(innerLeft, y, innerWidth,
                              qMax(0, m_cardRect.bottom() + 1 - kCardInset - y));
        update();
    }

    // QDialog's own initial-focus logic looks at window()'s focus widget --
    // MainWindow's, once embedded -- so it can leave focus behind on the
    // page underneath, where Escape/Enter would never reach the dialog.
    void focusFirstChild() {
        if (!m_dialog || m_released) {
            return;
        }
        QWidget* current = QApplication::focusWidget();
        if (current != nullptr && m_dialog->isAncestorOf(current)) {
            return;
        }
        const QList<QWidget*> children = m_dialog->findChildren<QWidget*>();
        for (QWidget* child : children) {
            if (child->isVisibleTo(m_dialog) && child->isEnabled() &&
                (child->focusPolicy() & Qt::TabFocus)) {
                child->setFocus(Qt::TabFocusReason);
                return;
            }
        }
        m_dialog->setFocus(Qt::OtherFocusReason);
    }

    QPointer<QWidget> m_host;
    QPointer<QDialog> m_dialog;
    DialogPresenter::Style m_style;
    QPointer<QWidget> m_originalParent;
    Qt::WindowFlags m_originalFlags;
    QSize m_originalMinimumSize;
    bool m_originalMoved = false;
    bool m_originalAutoFill = false;
    bool m_released = false;

    struct ButtonBoxState {
        QPointer<QDialogButtonBox> box;
        Qt::Orientation originalOrientation;
        int horizontalWidth;
    };

    int m_preferredWidth = 0;
    QList<QPointer<QLabel>> m_wrappedLabels;
    QList<ButtonBoxState> m_buttonBoxes;
    QMargins m_contentMargins;
    QLabel* m_title = nullptr;
    QToolButton* m_backButton = nullptr;
    QScrollArea* m_scroll = nullptr;
    QRect m_cardRect;
    int m_headerHeight = 0;
};

bool embedActive() {
    return s_embedded && s_host;
}

// Stand-in for QMessageBox/QInputDialog while embedded. QMessageBox pins
// itself to a screen-derived fixed size on every show (fighting the card's
// own sizing) and on Android would try the platform's native dialog
// helper besides; a plain QDialog has neither problem.
class MessageDialog : public QDialog {
public:
    MessageDialog(QWidget* parent, const QString& title, const QString& text)
        : QDialog(parent) {
        setWindowTitle(title);
        // Preferred card width only -- DialogOverlay lifts it as a minimum.
        setMinimumWidth(300);
        auto* label = new QLabel(text, this);
        label->setWordWrap(true);
        m_buttons = new QDialogButtonBox(this);
        m_layout = new QVBoxLayout(this);
        m_layout->addWidget(label);
        m_layout->addSpacing(8);
        m_layout->addWidget(m_buttons);
    }

    QDialogButtonBox* buttons() const { return m_buttons; }
    QVBoxLayout* contentLayout() const { return m_layout; }

private:
    QDialogButtonBox* m_buttons = nullptr;
    QVBoxLayout* m_layout = nullptr;
};

// What QMessageBox reports when its window is closed without a button.
QMessageBox::StandardButton escapeButton(QMessageBox::StandardButtons buttons) {
    for (QMessageBox::StandardButton candidate :
         {QMessageBox::Cancel, QMessageBox::No, QMessageBox::Close, QMessageBox::Abort}) {
        if (buttons.testFlag(candidate)) {
            return candidate;
        }
    }
    return QMessageBox::NoButton;
}

QMessageBox::StandardButton execMessage(QWidget* parent, const QString& title,
                                        const QString& text,
                                        QMessageBox::StandardButtons buttons,
                                        QMessageBox::StandardButton defaultButton) {
    MessageDialog message(parent, title, text);
    // QMessageBox::StandardButton and QDialogButtonBox::StandardButton share
    // their values by design.
    message.buttons()->setStandardButtons(QDialogButtonBox::StandardButtons(buttons.toInt()));
    if (QPushButton* button = message.buttons()->button(
            static_cast<QDialogButtonBox::StandardButton>(defaultButton))) {
        button->setDefault(true);
    }
    QObject::connect(message.buttons(), &QDialogButtonBox::clicked, &message,
                     [&message](QAbstractButton* button) {
                         message.done(message.buttons()->standardButton(button));
                     });
    const int result = DialogPresenter::exec(message, DialogPresenter::Style::Card);
    return result == QDialog::Rejected ? escapeButton(buttons)
                                       : static_cast<QMessageBox::StandardButton>(result);
}

}  // namespace

namespace DialogPresenter {

void setHost(QWidget* host) {
    s_host = host;
}

void setEmbedded(bool embedded) {
    s_embedded = embedded;
}

bool embedded() {
    return embedActive();
}

int exec(QDialog& dialog, Style style) {
    if (!embedActive()) {
        return dialog.exec();
    }
    QPointer<DialogOverlay> overlay = new DialogOverlay(s_host, &dialog, style, false);
    const int result = dialog.exec();
    if (overlay) {
        overlay->release();
    }
    return result;
}

void show(QDialog* dialog, Style style) {
    if (dialog == nullptr) {
        return;
    }
    for (DialogOverlay* overlay : std::as_const(s_overlays)) {
        if (overlay->dialog() == dialog) {
            overlay->raise();
            return;
        }
    }
    if (!embedActive()) {
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    new DialogOverlay(s_host, dialog, style, true);
    dialog->show();
}

QMessageBox::StandardButton question(QWidget* parent, const QString& title, const QString& text,
                                     QMessageBox::StandardButtons buttons,
                                     QMessageBox::StandardButton defaultButton) {
    if (!embedActive()) {
        return QMessageBox::question(parent, title, text, buttons, defaultButton);
    }
    return execMessage(parent, title, text, buttons, defaultButton);
}

void information(QWidget* parent, const QString& title, const QString& text) {
    if (!embedActive()) {
        QMessageBox::information(parent, title, text);
        return;
    }
    execMessage(parent, title, text, QMessageBox::Ok, QMessageBox::Ok);
}

void warning(QWidget* parent, const QString& title, const QString& text) {
    if (!embedActive()) {
        QMessageBox::warning(parent, title, text);
        return;
    }
    execMessage(parent, title, text, QMessageBox::Ok, QMessageBox::Ok);
}

QString getText(QWidget* parent, const QString& title, const QString& label,
                QLineEdit::EchoMode echo, const QString& text, bool* ok) {
    if (!embedActive()) {
        return QInputDialog::getText(parent, title, label, echo, text, ok);
    }
    MessageDialog prompt(parent, title, label);
    auto* edit = new QLineEdit(text, &prompt);
    edit->setEchoMode(echo);
    prompt.contentLayout()->insertWidget(1, edit);
    prompt.buttons()->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(prompt.buttons(), &QDialogButtonBox::accepted, &prompt, &QDialog::accept);
    QObject::connect(prompt.buttons(), &QDialogButtonBox::rejected, &prompt, &QDialog::reject);
    const bool accepted = exec(prompt, Style::Card) == QDialog::Accepted;
    if (ok != nullptr) {
        *ok = accepted;
    }
    return accepted ? edit->text() : QString();
}

bool confirm(QWidget* parent, const QString& title, const QString& text,
             const QString& acceptText, const QString& rejectText) {
    if (!embedActive()) {
        QMessageBox box(parent);
        box.setWindowTitle(title);
        box.setText(text);
        QPushButton* acceptButton = box.addButton(acceptText, QMessageBox::AcceptRole);
        box.addButton(rejectText, QMessageBox::RejectRole);
        box.exec();
        return box.clickedButton() == acceptButton;
    }
    MessageDialog message(parent, title, text);
    message.buttons()->addButton(acceptText, QDialogButtonBox::AcceptRole);
    message.buttons()->addButton(rejectText, QDialogButtonBox::RejectRole);
    QObject::connect(message.buttons(), &QDialogButtonBox::accepted, &message, &QDialog::accept);
    QObject::connect(message.buttons(), &QDialogButtonBox::rejected, &message, &QDialog::reject);
    return exec(message, Style::Card) == QDialog::Accepted;
}

}  // namespace DialogPresenter

}  // namespace traceview

#include "dialogpresenter.moc"
