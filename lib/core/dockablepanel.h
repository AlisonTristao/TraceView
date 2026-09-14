#pragma once

#include <QWidget>

class QVBoxLayout;

namespace traceview {

class DockablePanelHeader;

// Shared base for LayersPanel/PropertiesPanel: owns the header (drag handle,
// see DockablePanelHeader) that both panels used to duplicate byte-for-byte.
// A subclass just adds its real content to bodyLayout() and reports how
// thick it wants to be when docked (see preferredThickness()) -- everything
// about *where* the panel actually sits (edge, floating, drag handling,
// visibility) lives in PanelDockController/MainWindow instead, which this
// class knows nothing about.
class DockablePanel : public QWidget {
    Q_OBJECT

public:
    explicit DockablePanel(QWidget* parent = nullptr);

    // The panel's fixed size along its docked axis: width when docked
    // left/right, height when docked top/bottom. Also used as the starting
    // size the first time the panel is floated. Panels don't otherwise
    // constrain their own size -- PanelDockController applies this via
    // setGeometry() rather than setFixedWidth()/setFixedHeight(), since a
    // fixed size on the "wrong" axis would break docking to the other pair
    // of edges.
    virtual int preferredThickness() const = 0;

    DockablePanelHeader* header() const {
        return m_header;
    }

protected:
    // Subclasses add their real content here instead of building their own
    // outer QVBoxLayout.
    QVBoxLayout* bodyLayout() const {
        return m_bodyLayout;
    }
    // The outer layout (header + bodyLayout()) -- exposed only so a subclass
    // that wants its content flush against the edges (LayersPanel) can zero
    // out its margins, matching each panel's pre-refactor look.
    QVBoxLayout* mainLayout() const {
        return m_mainLayout;
    }

private:
    DockablePanelHeader* m_header = nullptr;
    QVBoxLayout* m_mainLayout = nullptr;
    QVBoxLayout* m_bodyLayout = nullptr;
};

}  // namespace traceview
