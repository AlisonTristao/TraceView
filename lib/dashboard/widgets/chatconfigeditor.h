#pragma once

#include "dashboard/widgetconfigeditor.h"

class QCheckBox;
class QLineEdit;

namespace traceview {

// Properties-panel section for ChatWidget: the name outgoing messages carry
// and whether they are drawn mirrored on the right, messenger-style.
class ChatConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit ChatConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;

private:
    void emitChanged();

    bool m_updating = false;
    QLineEdit* m_userNameEdit = nullptr;
    QCheckBox* m_ownOnRightCheck = nullptr;
};

}  // namespace traceview
