#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QPushButton;
class QListWidget;
class QFormLayout;
class QHBoxLayout;
class QVBoxLayout;
class QResizeEvent;

namespace traceview {

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

    // User mode only exposes the Appearance page (theme, font, language):
    // the category sidebar is hidden and the page is pinned to Appearance.
    // MainWindow calls this on open and again whenever the mode changes.
    void setDeveloperMode(bool developer);

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
    // Caps the category list at its rows' height so its frame ends under
    // the last icon instead of running to the bottom of the page.
    void fitCategoryListHeight();

    QLabel* m_titleLabel = nullptr;
    QLabel* m_restartNotice = nullptr;
    QPushButton* m_restartButton = nullptr;
    QLabel* m_updateStatusLabel = nullptr;
    QString m_initialLanguageId;
    int m_initialFrameLogCapacity = 0;
    int m_initialNotificationHistoryCapacity = 0;

    QListWidget* m_categoryList = nullptr;
    QStringList m_categoryNames;
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
