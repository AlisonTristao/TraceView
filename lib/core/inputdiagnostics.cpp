#include "core/inputdiagnostics.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QStringList>
#include <QWidget>

namespace traceview {

namespace {

constexpr int kMaxLines = 16;

QString describe(const QObject* object) {
    if (object == nullptr) {
        return QStringLiteral("null");
    }
    const QString name = object->objectName();
    return name.isEmpty() ? QString::fromLatin1(object->metaObject()->className())
                          : QStringLiteral("%1#%2").arg(object->metaObject()->className(), name);
}

QString visibleText(const QString& text) {
    QString shown = text;
    shown.replace(QChar('\b'), QStringLiteral("\\b"));
    return QStringLiteral("\"%1\"").arg(shown);
}

class InputDiagnostics : public QObject {
public:
    explicit InputDiagnostics(QWidget* host) : QObject(host), m_host(host) {
        m_label = new QLabel(host);
        m_label->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_label->setFocusPolicy(Qt::NoFocus);
        m_label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_label->setWordWrap(true);
        m_label->setMargin(4);
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSizeF(qMax(6.0, font.pointSizeF() * 0.75));
        m_label->setFont(font);
        m_label->setStyleSheet(
            "background: rgba(0, 0, 0, 190); color: #7CFC00; border: 1px solid #7CFC00;");
        m_clock.start();

        QInputMethod* im = QGuiApplication::inputMethod();
        connect(im, &QInputMethod::visibleChanged, this,
                [this, im] { log(QStringLiteral("IM visible=%1").arg(im->isVisible())); });
        connect(im, &QInputMethod::keyboardRectangleChanged, this, [this, im] {
            const QRectF r = im->keyboardRectangle();
            log(QStringLiteral("IM rect %1x%2 @%3").arg(r.width()).arg(r.height()).arg(r.y()));
        });
        connect(im, &QInputMethod::animatingChanged, this,
                [this, im] { log(QStringLiteral("IM animating=%1").arg(im->isAnimating())); });
        connect(qApp, &QGuiApplication::focusObjectChanged, this,
                [this](QObject* object) { log(QStringLiteral("focusObject -> %1").arg(describe(object))); });
        connect(qApp, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState state) { log(QStringLiteral("appState %1").arg(int(state))); });
        qApp->installEventFilter(this);

        log(QStringLiteral("diagnostics on"));
        m_label->show();
        m_label->raise();
    }

    ~InputDiagnostics() override { delete m_label; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == m_label || m_logging) {
            return false;
        }
        switch (event->type()) {
            case QEvent::FocusIn:
            case QEvent::FocusOut: {
                if (!watched->isWidgetType()) {
                    break;
                }
                const auto* focus = static_cast<QFocusEvent*>(event);
                log(QStringLiteral("%1 %2 reason=%3")
                        .arg(event->type() == QEvent::FocusIn ? "FocusIn " : "FocusOut",
                             describe(watched))
                        .arg(int(focus->reason())));
                break;
            }
            case QEvent::KeyPress:
            case QEvent::KeyRelease:
            case QEvent::ShortcutOverride: {
                // Key events propagate up the parent chain; log only the
                // first delivery (to the focus object).
                if (watched != qApp->focusObject()) {
                    break;
                }
                const auto* key = static_cast<QKeyEvent*>(event);
                const char* kind = event->type() == QEvent::KeyPress     ? "KeyPress"
                                   : event->type() == QEvent::KeyRelease ? "KeyRel  "
                                                                         : "ShortOvr";
                log(QStringLiteral("%1 key=0x%2 %3 auto=%4")
                        .arg(kind, QString::number(key->key(), 16), visibleText(key->text()))
                        .arg(key->isAutoRepeat()));
                break;
            }
            case QEvent::InputMethod: {
                const auto* im = static_cast<QInputMethodEvent*>(event);
                log(QStringLiteral("IMEvent commit=%1 pre=%2 repl=%3/%4 attrs=%5")
                        .arg(visibleText(im->commitString()), visibleText(im->preeditString()))
                        .arg(im->replacementStart())
                        .arg(im->replacementLength())
                        .arg(im->attributes().size()));
                break;
            }
            case QEvent::Resize:
                if (watched == m_host) {
                    const QSize size = static_cast<QWidget*>(watched)->size();
                    log(QStringLiteral("host resize %1x%2").arg(size.width()).arg(size.height()));
                }
                break;
            case QEvent::WindowActivate:
            case QEvent::WindowDeactivate:
                if (watched == m_host) {
                    log(event->type() == QEvent::WindowActivate ? QStringLiteral("window activate")
                                                                : QStringLiteral("window deactivate"));
                }
                break;
            default:
                break;
        }
        return false;
    }

private:
    void log(const QString& line) {
        m_logging = true;
        m_lines.append(QStringLiteral("%1 %2").arg(m_clock.elapsed() % 100000, 5).arg(line));
        while (m_lines.size() > kMaxLines) {
            m_lines.removeFirst();
        }
        if (m_host) {
            const int width = m_host->width() - 8;
            m_label->setText(m_lines.join('\n'));
            m_label->setFixedWidth(qMax(100, width));
            m_label->adjustSize();
            m_label->move(4, 4);
            m_label->raise();
        }
        m_logging = false;
    }

    QPointer<QWidget> m_host;
    QLabel* m_label = nullptr;
    QStringList m_lines;
    QElapsedTimer m_clock;
    bool m_logging = false;
};

QPointer<InputDiagnostics> s_instance;

}  // namespace

void setInputDiagnosticsEnabled(QWidget* host, bool enabled) {
    if (enabled && !s_instance) {
        s_instance = new InputDiagnostics(host);
    } else if (!enabled && s_instance) {
        delete s_instance;
    }
}

}  // namespace traceview
