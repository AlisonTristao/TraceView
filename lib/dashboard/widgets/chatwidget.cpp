#include "chatwidget.h"

#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "traceview/fontmanager.h"
#include "theme/iconlibrary.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kAuthorRole = Qt::UserRole + 1;
constexpr int kTextRole = Qt::UserRole + 2;
constexpr int kAttachmentRole = Qt::UserRole + 3;
constexpr int kStatusRole = Qt::UserRole + 4;
constexpr int kTimeRole = Qt::UserRole + 5;
constexpr int kOwnRole = Qt::UserRole + 6;
constexpr int kSystemRole = Qt::UserRole + 7;

constexpr int kPadding = 10;
constexpr int kLineGap = 4;
constexpr int kIconSize = 16;
// Stand-in for the round trip in visual-only mode (no device configured),
// so the Sending -> Sent transition is visible without a transport.
constexpr int kFakeDeliveryMs = 900;

const QLatin1String kDefaultTopicName("chat");
const QLatin1String kDefaultSendCommand("chat -send");

quint32 configSourceId(const QJsonObject& json) {
    return quint32(json.value(QLatin1String("sourceId")).toString("0").toULongLong(nullptr, 0));
}

quint16 configTopicId(const QJsonObject& json) {
    return quint16(
        qBound(0, json.value(QLatin1String("topicId")).toString("0").toInt(nullptr, 0), 65535));
}

QString statusText(ChatWidget::MessageStatus status) {
    switch (status) {
    case ChatWidget::MessageStatus::Sending:
        return ChatWidget::tr("Sending...");
    case ChatWidget::MessageStatus::Sent:
        return ChatWidget::tr("Sent");
    case ChatWidget::MessageStatus::Failed:
        return ChatWidget::tr("Not sent");
    case ChatWidget::MessageStatus::Received:
        return {};
    }
    return {};
}

QColor statusColor(ChatWidget::MessageStatus status, const ThemePalette& palette) {
    switch (status) {
    case ChatWidget::MessageStatus::Sending:
        return palette.textSecondary;
    case ChatWidget::MessageStatus::Sent:
        return palette.success;
    case ChatWidget::MessageStatus::Failed:
        return palette.danger;
    case ChatWidget::MessageStatus::Received:
        return palette.textSecondary;
    }
    return palette.textSecondary;
}

QFont authorFont(const QFont& base) {
    QFont font = base;
    font.setBold(true);
    return font;
}

// Time/status/attachment line, a notch smaller than the body. scaledFont()
// because on mobile the app font is pixel-sized (pointSizeF() is -1), and
// scaling only the point size there collapsed it to the 6pt floor.
QFont metaFont(const QFont& base) {
    return scaledFont(base, 0.85);
}

// Paints one message as a flat card: author top-left, wrapped text (and an
// attachment line) below it, time bottom-left, status bottom-right, and a
// divider under the card -- no bubbles, no avatar. With ownOnRight set, the
// user's own messages are mirrored: everything right-aligned, time on the
// right and status on the left.
class ChatMessageDelegate : public QStyledItemDelegate {
public:
    explicit ChatMessageDelegate(QListWidget* view) : QStyledItemDelegate(view), m_view(view) {}

    void setOwnOnRight(bool on) {
        m_ownOnRight = on;
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const int width = qMax(80, m_view->viewport()->width());
        return QSize(width, contentHeight(option.font, index, width));
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const ThemePalette& palette = ThemeManager::instance().currentTheme();
        const QRect r = option.rect;
        painter->save();
        painter->setRenderHint(QPainter::TextAntialiasing, true);

        if (option.state & QStyle::State_MouseOver) {
            painter->fillRect(r, palette.surfaceAlt);
        }
        painter->setPen(palette.border);
        painter->drawLine(r.bottomLeft(), r.bottomRight());

        const QRect inner = r.adjusted(kPadding, kPadding, -kPadding, -kPadding);
        if (index.data(kSystemRole).toBool()) {
            // A device notice: one muted, italic, centred block with its
            // time, no author card and no status.
            painter->setFont(systemFont(option.font));
            painter->setPen(palette.textSecondary);
            painter->drawText(inner, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
                              systemLine(index));
            painter->restore();
            return;
        }
        int y = inner.top();
        const bool mirrored = m_ownOnRight && index.data(kOwnRole).toBool();
        const Qt::Alignment side = mirrored ? Qt::AlignRight : Qt::AlignLeft;

        const QFont nameFont = authorFont(option.font);
        painter->setFont(nameFont);
        painter->setPen(palette.textPrimary);
        const QFontMetrics nameMetrics(nameFont);
        painter->drawText(QRect(inner.left(), y, inner.width(), nameMetrics.height()),
                          side | Qt::AlignVCenter,
                          nameMetrics.elidedText(index.data(kAuthorRole).toString(),
                                                 Qt::ElideRight, inner.width()));
        y += nameMetrics.height() + kLineGap;

        const QString text = index.data(kTextRole).toString();
        if (!text.isEmpty()) {
            painter->setFont(option.font);
            const QRect textRect = QFontMetrics(option.font)
                                       .boundingRect(QRect(inner.left(), y, inner.width(), 100000),
                                                     Qt::TextWordWrap, text);
            painter->drawText(QRect(inner.left(), y, inner.width(), textRect.height()),
                              side | Qt::TextWordWrap, text);
            y += textRect.height() + kLineGap;
        }

        const QFont small = metaFont(option.font);
        const QFontMetrics smallMetrics(small);
        painter->setFont(small);

        const QString attachment = index.data(kAttachmentRole).toString();
        if (!attachment.isEmpty()) {
            const QIcon clip = IconLibrary::instance().icon(QStringLiteral("lucide:paperclip"),
                                                            palette.accent, kIconSize);
            const int lineHeight = qMax(kIconSize, smallMetrics.height());
            const int textWidth = inner.width() - kIconSize - 4;
            const QString label = smallMetrics.elidedText(attachment, Qt::ElideMiddle, textWidth);
            // Mirrored, the clip sits just left of the right-aligned name.
            const int iconLeft =
                mirrored ? inner.right() - smallMetrics.horizontalAdvance(label) - 4 - kIconSize
                         : inner.left();
            clip.paint(painter, QRect(iconLeft, y, kIconSize, lineHeight));
            painter->setPen(palette.accent);
            const int textLeft = mirrored ? inner.left() : inner.left() + kIconSize + 4;
            painter->drawText(QRect(textLeft, y, textWidth, lineHeight), side | Qt::AlignVCenter,
                              label);
            y += lineHeight + kLineGap;
        }

        const auto status = static_cast<ChatWidget::MessageStatus>(index.data(kStatusRole).toInt());
        const QRect metaRect(inner.left(), y, inner.width(), smallMetrics.height());
        painter->setPen(palette.textSecondary);
        painter->drawText(metaRect, side | Qt::AlignVCenter,
                          index.data(kTimeRole).toDateTime().toString(QStringLiteral("HH:mm")));
        painter->setPen(statusColor(status, palette));
        painter->drawText(metaRect, (mirrored ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter,
                          statusText(status));

        painter->restore();
    }

private:
    static QFont systemFont(const QFont& base) {
        QFont font = metaFont(base);
        font.setItalic(true);
        return font;
    }

    static QString systemLine(const QModelIndex& index) {
        return QStringLiteral("%1 \u00b7 %2: %3")
            .arg(index.data(kTimeRole).toDateTime().toString(QStringLiteral("HH:mm")),
                 index.data(kAuthorRole).toString(), index.data(kTextRole).toString());
    }

    static int contentHeight(const QFont& font, const QModelIndex& index, int width) {
        const int innerWidth = width - 2 * kPadding;
        if (index.data(kSystemRole).toBool()) {
            return QFontMetrics(systemFont(font))
                       .boundingRect(QRect(0, 0, innerWidth, 100000),
                                     Qt::AlignHCenter | Qt::TextWordWrap, systemLine(index))
                       .height() +
                   2 * kPadding;
        }
        const QFontMetrics smallMetrics(metaFont(font));
        int height = kPadding + QFontMetrics(authorFont(font)).height() + kLineGap;
        const QString text = index.data(kTextRole).toString();
        if (!text.isEmpty()) {
            height += QFontMetrics(font)
                          .boundingRect(QRect(0, 0, innerWidth, 100000), Qt::TextWordWrap, text)
                          .height() +
                      kLineGap;
        }
        if (!index.data(kAttachmentRole).toString().isEmpty()) {
            height += qMax(kIconSize, smallMetrics.height()) + kLineGap;
        }
        return height + smallMetrics.height() + kPadding;
    }

    QListWidget* m_view;
    bool m_ownOnRight = true;
};

QToolButton* makeIconButton(QWidget* parent, const QString& toolTip) {
    auto* button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setToolTip(toolTip);
    button->setIconSize(QSize(18, 18));
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

}  // namespace

QStringList ChatWidget::allowedAttachmentSuffixes() {
    return {QStringLiteral("txt"), QStringLiteral("csv"), QStringLiteral("json"),
            QStringLiteral("log"), QStringLiteral("png"), QStringLiteral("jpg"),
            QStringLiteral("jpeg"), QStringLiteral("bin")};
}

ChatWidget::ChatWidget(QWidget* parent) : DashboardWidget(parent), m_userName(tr("You")) {
    m_feed = new QListWidget(this);
    m_delegate = new ChatMessageDelegate(m_feed);
    m_feed->setItemDelegate(m_delegate);
    m_feed->setFrameShape(QFrame::NoFrame);
    m_feed->setSelectionMode(QAbstractItemView::NoSelection);
    m_feed->setFocusPolicy(Qt::NoFocus);
    m_feed->setMouseTracking(true);
    m_feed->setResizeMode(QListView::Adjust);
    m_feed->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_feed->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_feed->viewport()->installEventFilter(this);

    // Pending attachment, shown above the compose row until sent or removed.
    m_attachmentChip = new QLabel(this);
    m_attachmentChip->setTextInteractionFlags(Qt::NoTextInteraction);
    m_removeAttachmentButton = makeIconButton(this, tr("Remove attachment"));
    connect(m_removeAttachmentButton, &QToolButton::clicked, this,
            [this] { setPendingAttachment({}); });
    // Its own widget so hiding it drops the row's margins too -- a bare
    // layout keeps them and pushes the compose row off-centre.
    m_attachmentRow = new QWidget(this);
    auto* chipRow = new QHBoxLayout(m_attachmentRow);
    chipRow->setContentsMargins(kPadding, 4, 4, 0);
    chipRow->setSpacing(4);
    chipRow->addWidget(m_attachmentChip, 1);
    chipRow->addWidget(m_removeAttachmentButton, 0);

    m_attachButton = makeIconButton(this, tr("Attach a file"));
    connect(m_attachButton, &QToolButton::clicked, this, &ChatWidget::pickAttachment);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Write a message..."));
    connect(m_input, &QLineEdit::returnPressed, this, &ChatWidget::send);

    m_sendButton = makeIconButton(this, tr("Send"));
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // The tap that lands here also closes the soft keyboard; the window
    // shrinks back and moves the button out from under the finger before the
    // release, so clicked() never fires. Send on press instead.
    connect(m_sendButton, &QToolButton::pressed, this, &ChatWidget::send);
#else
    connect(m_sendButton, &QToolButton::clicked, this, &ChatWidget::send);
#endif

    auto* composeRow = new QHBoxLayout;
    composeRow->setContentsMargins(4, 4, 4, 4);
    composeRow->setSpacing(4);
    composeRow->addWidget(m_attachButton, 0);
    composeRow->addWidget(m_input, 1);
    composeRow->addWidget(m_sendButton, 0);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_feed, 1);
    layout->addWidget(m_attachmentRow, 0);
    layout->addLayout(composeRow, 0);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { refreshIcons(); });
    refreshIcons();
    setPendingAttachment({});

    // Tokens only have to be unique per widget instance; seeding from the
    // clock keeps a reopened dashboard from reusing the last session's.
    m_lastToken = quint32(QDateTime::currentMSecsSinceEpoch() & 0x3FFFFFFF);
    m_topicName = kDefaultTopicName;
    m_sendCommand = kDefaultSendCommand;
    applyMode();

    // DashboardCell resizes this widget with a bare setGeometry(), so pin
    // the floor to what the compose row plus a sliver of feed needs.
    setMinimumSize(160, 120);
}

bool ChatWidget::eventFilter(QObject* watched, QEvent* event) {
    // Card heights depend on the wrap width (see ChatMessageDelegate), and
    // QListView caches them -- re-measure once the viewport settles on a new
    // width, or text wrapped at an earlier, narrower width leaves gaps.
    if (watched == m_feed->viewport() && event->type() == QEvent::Resize) {
        QTimer::singleShot(0, m_feed, [feed = m_feed] { feed->doItemsLayout(); });
    }
    return DashboardWidget::eventFilter(watched, event);
}

void ChatWidget::setConfig(const QJsonObject& config) {
    const QString name = config.value(QStringLiteral("userName")).toString().trimmed();
    m_userName = name.isEmpty() ? tr("You") : name;
    static_cast<ChatMessageDelegate*>(m_delegate)
        ->setOwnOnRight(config.value(QStringLiteral("ownMessagesOnRight")).toBool(true));

    const QString deviceId = config.value(QStringLiteral("deviceId")).toString();
    const QString topicName = config.value(QStringLiteral("topicName")).toString().trimmed();
    const QString sendCommand = config.value(QStringLiteral("sendCommand")).toString().trimmed();
    const quint32 sourceId = configSourceId(config);
    const quint16 topicId = configTopicId(config);
    // Another device or topic is another event log: its ids mean nothing
    // against the ones remembered so far.
    if (deviceId != m_deviceId || sourceId != m_sourceId || topicId != m_topicId) {
        m_events.reset();
    }
    m_deviceId = deviceId;
    m_topicName = topicName.isEmpty() ? QString(kDefaultTopicName) : topicName;
    m_sendCommand = sendCommand.isEmpty() ? QString(kDefaultSendCommand) : sendCommand;
    m_sourceId = sourceId;
    m_topicId = topicId;
    m_sampleTimeMs = qMax(
        1.0, config.value(QStringLiteral("sampleTimeMs")).toDouble(kDefaultSampleTimeMs));
    applyMode();
    m_feed->viewport()->update();
}

void ChatWidget::applyMode() {
    // Attachments have no device transport yet: the paperclip only exists in
    // visual-only mode.
    const bool deviceMode = isDeviceMode();
    m_attachButton->setVisible(!deviceMode);
    if (deviceMode && !m_pendingAttachment.isEmpty()) {
        setPendingAttachment({});
    }
}

int ChatWidget::appendMessage(const QString& author, const QString& text,
                              const QString& attachment, MessageStatus status) {
    return appendEntry(author, text, attachment, status, author == m_userName, false);
}

int ChatWidget::appendSystemNotice(const QString& text) {
    return appendEntry(tr("System"), text, {}, MessageStatus::Received, false, true);
}

int ChatWidget::appendEntry(const QString& author, const QString& text,
                            const QString& attachment, MessageStatus status, bool own,
                            bool system) {
    QScrollBar* bar = m_feed->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 4;

    auto* item = new QListWidgetItem(m_feed);
    item->setData(kAuthorRole, author);
    item->setData(kOwnRole, own);
    item->setData(kSystemRole, system);
    item->setData(kTextRole, text);
    item->setData(kAttachmentRole, attachment);
    item->setData(kStatusRole, int(status));
    item->setData(kTimeRole, QDateTime::currentDateTime());
    item->setFlags(Qt::ItemIsEnabled);

    // Follow the conversation only if the reader was already at the end --
    // someone scrolled up reading history shouldn't be yanked down.
    if (atBottom) {
        m_feed->scrollToBottom();
    }
    return m_feed->row(item);
}

void ChatWidget::setMessageStatus(int row, MessageStatus status) {
    if (QListWidgetItem* item = m_feed->item(row)) {
        item->setData(kStatusRole, int(status));
    }
}

int ChatWidget::messageCount() const {
    return m_feed->count();
}

ChatWidget::MessageStatus ChatWidget::messageStatus(int row) const {
    const QListWidgetItem* item = m_feed->item(row);
    return item ? static_cast<MessageStatus>(item->data(kStatusRole).toInt())
                : MessageStatus::Failed;
}

QString ChatWidget::messageAuthor(int row) const {
    const QListWidgetItem* item = m_feed->item(row);
    return item ? item->data(kAuthorRole).toString() : QString();
}

QString ChatWidget::messageText(int row) const {
    const QListWidgetItem* item = m_feed->item(row);
    return item ? item->data(kTextRole).toString() : QString();
}

QString ChatWidget::messageToolTip(int row) const {
    const QListWidgetItem* item = m_feed->item(row);
    return item ? item->toolTip() : QString();
}

bool ChatWidget::isSystemNotice(int row) const {
    const QListWidgetItem* item = m_feed->item(row);
    return item && item->data(kSystemRole).toBool();
}

quint32 ChatWidget::nextToken() {
    ++m_lastToken;
    if (m_lastToken == 0) {
        m_lastToken = 1;
    }
    return m_lastToken;
}

void ChatWidget::markSendFailed(quint32 token, const QString& reason) {
    const auto it = m_rowByToken.constFind(token);
    if (it == m_rowByToken.constEnd()) {
        return;
    }
    setMessageStatus(it.value(), MessageStatus::Failed);
    if (QListWidgetItem* item = m_feed->item(it.value())) {
        item->setToolTip(reason);
    }
}

QVector<TelemetryTopicRequest> ChatWidget::topicRequests() const {
    if (m_deviceId.isEmpty() || m_sourceId == 0 || m_topicId == 0) {
        return {};
    }
    const double millihz = 1'000'000.0 / m_sampleTimeMs;
    return {{m_deviceId, m_sourceId, m_topicId, quint32(qBound(1.0, millihz, 4.0e9))}};
}

void ChatWidget::onFieldSample(const TelemetryFieldBinding&, quint64, double) {}

void ChatWidget::onArraySample(const TelemetryFieldBinding&, quint64, const QVector<float>&) {}

void ChatWidget::onTextSample(quint32 sourceId, quint16 topicId, quint64 timestampUs,
                              const QString& text) {
    Q_UNUSED(timestampUs);
    if (!isDeviceMode() || sourceId != m_sourceId || topicId != m_topicId) {
        return;
    }
    const QVector<ChatEvent> fresh = m_events.takeNew(parseChatEventLog(text));
    for (const ChatEvent& event : fresh) {
        handleEvent(event);
    }
}

void ChatWidget::handleEvent(const ChatEvent& event) {
    switch (event.kind) {
    case ChatEvent::Kind::Incoming: {
        const QString author = event.author.trimmed().isEmpty() ? tr("Unknown") : event.author;
        const int row = appendEntry(author, event.text, {}, MessageStatus::Received, false, false);
        QListWidgetItem* item = m_feed->item(row);
        if (item && !event.token.isEmpty()) {
            item->setToolTip(tr("From radio %1").arg(event.token));
        }
        break;
    }
    case ChatEvent::Kind::Sent:
    case ChatEvent::Kind::Failed: {
        bool ok = false;
        const quint32 token = event.token.toUInt(&ok, 10);
        const auto it = ok ? m_rowByToken.constFind(token) : m_rowByToken.constEnd();
        if (it == m_rowByToken.constEnd()) {
            break;  // another widget's (or an earlier session's) message
        }
        const bool sent = event.kind == ChatEvent::Kind::Sent;
        setMessageStatus(it.value(), sent ? MessageStatus::Sent : MessageStatus::Failed);
        if (QListWidgetItem* item = m_feed->item(it.value())) {
            item->setToolTip(sent ? QString() : event.text);
        }
        break;
    }
    case ChatEvent::Kind::Error:
        appendSystemNotice(event.text.isEmpty() ? event.author : event.text);
        break;
    case ChatEvent::Kind::Unknown:
        break;
    }
}

void ChatWidget::sendText(const QString& text) {
    m_input->setText(text);
    send();
}

void ChatWidget::send() {
    const QString text = m_input->text().trimmed();
    if (isDeviceMode()) {
        if (text.isEmpty()) {
            return;
        }
        const quint32 token = nextToken();
        const int row = appendEntry(m_userName, text, {}, MessageStatus::Sending, true, false);
        m_rowByToken.insert(token, row);
        m_feed->scrollToBottom();
        m_input->clear();

        QTimer::singleShot(kDeliveryTimeoutMs, this, [this, row] {
            if (messageStatus(row) == MessageStatus::Sending) {
                setMessageStatus(row, MessageStatus::Failed);
                if (QListWidgetItem* item = m_feed->item(row)) {
                    item->setToolTip(tr("No confirmation from the device"));
                }
            }
        });
        // SerialWidgetBridge delivers it -- or calls markSendFailed() right
        // here when the device isn't connected.
        emit sendRequested(buildChatSendCommand(m_sendCommand, token, m_userName, text).toUtf8(),
                           token);
        return;
    }

    if (text.isEmpty() && m_pendingAttachment.isEmpty()) {
        return;
    }
    const int row = appendMessage(m_userName, text, QFileInfo(m_pendingAttachment).fileName(),
                                  MessageStatus::Sending);
    m_feed->scrollToBottom();
    m_input->clear();
    setPendingAttachment({});

    // Visual-only mode: no transport, so pretend it went through.
    QTimer::singleShot(kFakeDeliveryMs, this,
                       [this, row] { setMessageStatus(row, MessageStatus::Sent); });
}

void ChatWidget::pickAttachment() {
    QStringList patterns;
    for (const QString& suffix : allowedAttachmentSuffixes()) {
        patterns << QStringLiteral("*.") + suffix;
    }
    const QString filter = tr("Supported files (%1)").arg(patterns.join(QLatin1Char(' ')));
    const QString path = QFileDialog::getOpenFileName(this, tr("Attach a file"), QString(), filter);
    if (path.isEmpty()) {
        return;
    }

    // The dialog filter is only a suggestion (the user can type any name),
    // so check the pick again here.
    const QFileInfo info(path);
    if (!allowedAttachmentSuffixes().contains(info.suffix().toLower())) {
        QMessageBox::warning(this, tr("Attach a file"),
                             tr("This file type can't be sent.\nAllowed: %1")
                                 .arg(allowedAttachmentSuffixes().join(QStringLiteral(", "))));
        return;
    }
    if (info.size() > kMaxAttachmentBytes) {
        QMessageBox::warning(this, tr("Attach a file"),
                             tr("This file is too large (limit %1 MB).")
                                 .arg(kMaxAttachmentBytes / (1024 * 1024)));
        return;
    }
    setPendingAttachment(path);
}

void ChatWidget::setPendingAttachment(const QString& path) {
    m_pendingAttachment = path;
    const bool has = !path.isEmpty();
    m_attachmentRow->setVisible(has);
    if (has) {
        m_attachmentChip->setText(QFileInfo(path).fileName());
        m_attachmentChip->setToolTip(QDir::toNativeSeparators(path));
    }
    m_attachButton->setEnabled(!has);  // one attachment per message
}

void ChatWidget::refreshIcons() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    IconLibrary& icons = IconLibrary::instance();
    m_attachButton->setIcon(
        icons.icon(QStringLiteral("lucide:paperclip"), palette.textSecondary, 18));
    m_sendButton->setIcon(icons.icon(QStringLiteral("lucide:send"), palette.accent, 18));
    m_removeAttachmentButton->setIcon(
        icons.icon(QStringLiteral("lucide:x"), palette.textSecondary, 14));
    m_attachmentChip->setStyleSheet(
        QStringLiteral("color: %1;").arg(palette.accent.name()));
    m_feed->viewport()->update();
}

}  // namespace traceview
