#pragma once

#include <QHash>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

namespace tracelab {

// The loop of a plant drawn as a block diagram, statically (nothing is
// dragged): the plant, its filters and its controller left to right, each
// signal a lane between them. A dot on a block's left edge: it reads the
// signal; on its right edge: it writes it (a filter's TL_INOUT replaces it,
// its TL_OUT of a new name adds one). A lane dashed over a block passes it by
// untouched; a lane ending in a ring is not read by the controller. The
// plant's inputs come back under the blocks, from the controller's outputs
// to the plant's left edge. Clicking a block emits blockClicked(). What it
// shows is a Model the TraceLab tab builds from the blocks' declarations
// (lab/code/codemodule.h) and the controller host's signal names.
class LoopDiagram : public QWidget {
    Q_OBJECT

public:
    struct Signal {
        enum Kind { Measurement, Preview, Created, Command };
        QString name;  // an array's base name ("s" for s[0]..s[14])
        int count = 1;
        QString unit;
        Kind kind = Measurement;
        // Formatted live values, empty when unknown: as the plant gives it,
        // and as the controller reads it (after the filters).
        QString measured;
        QString final;
    };
    struct Block {
        enum Kind { Plant, Filter, Controller, NoController };
        Kind kind = Plant;
        int index = 0;  // a filter's number in the chain
        QString title;
        QString subtitle;
        QString status;
        int tone = 0;  // 0 neutral, 1 good, 2 warning, 3 bad
        QString iconPath;
        // Signal names (Signal::name): read on the left edge, written on
        // the right. The plant reads the commands and writes the
        // measurements; the controller writes the commands.
        QStringList reads;
        QStringList writes;
        bool selected = false;
    };
    struct Model {
        QVector<Signal> signalList;
        QVector<Block> blocks;  // the plant first, the controller last
    };

    explicit LoopDiagram(QWidget* parent = nullptr);

    void setModel(const Model& model);
    const Model& model() const {
        return m_model;
    }
    // Where block `index` of the model is drawn (widget coordinates).
    QRectF blockRect(int index) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void blockClicked(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;

private:
    // Positions computed from the model (relayout()).
    struct Lane {
        int signal = 0;  // into m_model.signalList
        qreal y = 0;
        int producer = 0;  // block column
        int end = 0;       // block column the lane runs to
    };
    struct Command {
        int signal = 0;
        int from = -1;  // block writing it (the controller), -1 none
        qreal fromY = 0;
        qreal toY = 0;  // on the plant's left edge
        qreal returnY = 0;
        qreal fromX = 0;  // the riser right of the writer
        qreal toX = 0;    // the riser left of the plant
    };

    void relayout();
    int laneOf(const QString& name) const;
    bool blockTouches(int block, int lane) const;
    QString laneTip(const QPointF& pos) const;
    QString signalLabel(const Signal& signal) const;

    Model m_model;
    QVector<QRectF> m_blockRects;
    QVector<Lane> m_lanes;
    QVector<Command> m_commands;
    QHash<QString, int> m_laneByName;
    QSize m_size;
    qreal m_legendY = 0;
};

}  // namespace tracelab
