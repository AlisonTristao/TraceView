#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "dashboard/dashboardwidget.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QToolButton;

namespace traceview {

// A message feed in the style of early Twitter: each entry is a flat card
// with the author's name top-left, the message text below it (no avatar),
// and the delivery status bottom-right. Below the feed sits a compose row --
// a paperclip to attach one file, a text field, and a send button.
//
// Visual only for now: there is no transport behind it. send() appends the
// message as Sending and a timer flips it to Sent, so the three status
// states can be seen; setMessageStatus() is the hook a backend will drive.
class ChatWidget : public DashboardWidget {
    Q_OBJECT

public:
    enum class MessageStatus { Sending, Sent, Failed };

    // File types the paperclip offers, and the size ceiling for an
    // attachment. Kept deliberately narrow: small text/data files and images
    // that a device link can plausibly carry.
    static QStringList allowedAttachmentSuffixes();
    static constexpr qint64 kMaxAttachmentBytes = 5 * 1024 * 1024;

    explicit ChatWidget(QWidget* parent = nullptr);

    // config: {"userName": "..."} -- the name outgoing messages carry.
    void setConfig(const QJsonObject& config) override;

    // Appends a message and returns its row, for setMessageStatus().
    int appendMessage(const QString& author, const QString& text, const QString& attachment,
                      MessageStatus status);
    void setMessageStatus(int row, MessageStatus status);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void send();
    void pickAttachment();

private:
    void setPendingAttachment(const QString& path);
    void refreshIcons();

    QListWidget* m_feed = nullptr;
    QLineEdit* m_input = nullptr;
    QToolButton* m_attachButton = nullptr;
    QToolButton* m_sendButton = nullptr;
    QWidget* m_attachmentRow = nullptr;
    QLabel* m_attachmentChip = nullptr;
    QToolButton* m_removeAttachmentButton = nullptr;

    QString m_userName;
    QString m_pendingAttachment;
};

}  // namespace traceview
