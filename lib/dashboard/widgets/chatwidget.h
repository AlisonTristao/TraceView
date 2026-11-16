#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "dashboard/dashboardwidget.h"
#include "dashboard/telemetryconsumer.h"
#include "dashboard/widgets/chatprotocol.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QStyledItemDelegate;
class QToolButton;

namespace traceview {

// A message feed in the style of early Twitter: each entry is a flat card
// with the author's name top-left, the message text below it (no avatar),
// and the delivery status bottom-right. Below the feed sits a compose row --
// a paperclip to attach one file, a text field, and a send button.
//
// Bound to a device ("deviceId" in its config) it is a working chat through
// that device's radio (see chatprotocol.h for the wire format): send() emits
// sendRequested() with the device's send command -- delivered by
// SerialWidgetBridge exactly like a Push Button's command -- and the
// device's UTF8 event-log topic ("topicName", default "chat") is read through
// the TelemetryConsumer interface, which MainWindow subscribes like any
// text topic. SENT/FAIL events drive setMessageStatus(); IN events become
// other people's messages; ERR events become muted system lines.
//
// With no device configured (an older config holding only userName /
// ownMessagesOnRight) it stays visual only: send() appends the message as
// Sending and a timer flips it to Sent.
class ChatWidget : public DashboardWidget, public TelemetryConsumer {
    Q_OBJECT

public:
    // Received: someone else's message, drawn without a delivery status.
    enum class MessageStatus { Sending, Sent, Failed, Received };

    static constexpr int kDeliveryTimeoutMs = 60'000;
    static constexpr double kDefaultSampleTimeMs = 200.0;

    // File types the paperclip offers, and the size ceiling for an
    // attachment. Kept deliberately narrow: small text/data files and images
    // that a device link can plausibly carry.
    static QStringList allowedAttachmentSuffixes();
    static constexpr qint64 kMaxAttachmentBytes = 5 * 1024 * 1024;

    explicit ChatWidget(QWidget* parent = nullptr);

    // config: {"userName": "...", "ownMessagesOnRight": true} -- the name
    // outgoing messages carry, and whether messages by that name are drawn
    // mirrored on the right (default on) -- plus, for device mode,
    // "deviceId", "topicName" (default "chat"), "sendCommand" (default
    // "chat -send"), the resolved "sourceId"/"topicId" and an optional
    // "sampleTimeMs" (default 200).
    void setConfig(const QJsonObject& config) override;

    // Appends a message and returns its row, for setMessageStatus().
    int appendMessage(const QString& author, const QString& text, const QString& attachment,
                      MessageStatus status);
    void setMessageStatus(int row, MessageStatus status);
    // A muted, centred notice in the feed (device ERR events).
    int appendSystemNotice(const QString& text);

    bool isDeviceMode() const {
        return !m_deviceId.isEmpty();
    }
    QString deviceId() const {
        return m_deviceId;
    }
    int messageCount() const;
    MessageStatus messageStatus(int row) const;
    QString messageAuthor(int row) const;
    QString messageText(int row) const;
    QString messageToolTip(int row) const;
    bool isSystemNotice(int row) const;

    // The command was not handed to the device (it isn't connected):
    // message `token` fails at once, with `reason` as its tooltip.
    void markSendFailed(quint32 token, const QString& reason);

    // TelemetryConsumer: the event-log topic, and its samples.
    QVector<TelemetryTopicRequest> topicRequests() const override;
    void onFieldSample(const TelemetryFieldBinding& binding, quint64 timestampUs,
                       double value) override;
    void onArraySample(const TelemetryFieldBinding& binding, quint64 timestampUs,
                       const QVector<float>& values) override;
    void onTextSample(quint32 sourceId, quint16 topicId, quint64 timestampUs,
                      const QString& text) override;

    // Sends `text` as the user (what the send button does), for tests.
    void sendText(const QString& text);

signals:
    // Device mode: one command line for the device in "deviceId", carrying
    // message `token`. SerialWidgetBridge delivers it like a Push Button's
    // command, and calls markSendFailed() when the device isn't connected.
    void sendRequested(const QByteArray& command, quint32 token);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void send();
    void pickAttachment();

private:
    void setPendingAttachment(const QString& path);
    void refreshIcons();
    void applyMode();
    void handleEvent(const ChatEvent& event);
    int appendEntry(const QString& author, const QString& text, const QString& attachment,
                    MessageStatus status, bool own, bool system);
    quint32 nextToken();

    QListWidget* m_feed = nullptr;
    QStyledItemDelegate* m_delegate = nullptr;
    QLineEdit* m_input = nullptr;
    QToolButton* m_attachButton = nullptr;
    QToolButton* m_sendButton = nullptr;
    QWidget* m_attachmentRow = nullptr;
    QLabel* m_attachmentChip = nullptr;
    QToolButton* m_removeAttachmentButton = nullptr;

    QString m_userName;
    QString m_pendingAttachment;

    // Device mode.
    QString m_deviceId;
    QString m_topicName;
    QString m_sendCommand;
    quint32 m_sourceId = 0;
    quint16 m_topicId = 0;
    double m_sampleTimeMs = kDefaultSampleTimeMs;
    quint32 m_lastToken = 0;
    QHash<quint32, int> m_rowByToken;  // outgoing messages, by their token
    ChatEventTracker m_events;
};

}  // namespace traceview
