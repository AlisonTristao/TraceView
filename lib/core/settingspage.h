#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QDialogButtonBox;
class QPushButton;
class QListWidget;
class QFormLayout;
class QHBoxLayout;
class QVBoxLayout;
class QResizeEvent;

namespace traceview {

class DialogHeader;
class NavSidebar;

class SettingsPage final : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);

    // Called by MainWindow after any update check (auto or manual) resolves,
    // so the "Last checked" label reflects it without needing this tab to be
    // reopened. MainWindow only calls it through a QPointer to this page,
    // which nulls out once the Settings window closes.
    void setUpdateStatusText(const QString& text);

    // Shows/hides the page's own "Settings" heading -- redundant when the
    // window hosting it already titles it (embedded Page dialog top bar).
    void setTitleVisible(bool visible);
    // The footer's Close button (rejected() closes the hosting window).
    QDialogButtonBox* buttonBox() const { return m_buttonBox; }

signals:
    void clearRecentProjectsRequested();
    void restartRequested();
    void checkForUpdatesRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void refreshRestartNotice();
    // Narrow category sidebar to icons-only and stack each form row's
    // label above its field instead of beside it -- see resizeEvent()'s
    // own comment for why this exists.
    void applyCompactLayout(bool compact);
    // Sets every combo/spin box to m_fieldWidth, capped in compact mode to
    // what the current width can actually fit.
    void applyFieldWidth();
    DialogHeader* m_header = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
    QLabel* m_restartNotice = nullptr;
    QPushButton* m_restartButton = nullptr;
    QLabel* m_updateStatusLabel = nullptr;
    QString m_initialLanguageId;
    int m_initialFrameLogCapacity = 0;
    int m_initialNotificationHistoryCapacity = 0;

    NavSidebar* m_categoryList = nullptr;
    QList<QFormLayout*> m_formLayouts;
    // Margins/spacing that applyCompactLayout() trims on narrow screens.
    QVBoxLayout* m_rootLayout = nullptr;
    QHBoxLayout* m_bodyLayout = nullptr;
    QList<QVBoxLayout*> m_pageLayouts;
    QList<QWidget*> m_fieldWidgets;
    int m_fieldWidth = 0;
    bool m_compactLayout = false;
};

}  // namespace traceview
