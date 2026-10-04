#include "lftemplates.h"

#include <QCoreApplication>

#include "lab/code/codefiles.h"

namespace tracelab::lf {

void registerLineFollowerTemplates() {
    using code::BlockKind;
    const auto tr = [](const char* text) {
        return QCoreApplication::translate("LineFollowerTemplates", text);
    };
    const struct {
        BlockKind kind;
        const char* id;
        QString name;
        QString description;
        const char* file;
    } templates[] = {
        {BlockKind::Controller, "linefollower/qlpv_mpc", tr("qLPV MPC"),
         tr("Model predictive control on the exact qLPV model of the car: tracks the "
            "preview of the track (QP every tick)."),
         "controllers/qlpv_mpc.c"},
        {BlockKind::Controller, "linefollower/gpc", tr("GPC"),
         tr("Generalized predictive control linearized at a straight heading: one gain "
            "matrix, no QP."),
         "controllers/gpc.c"},
        {BlockKind::Controller, "linefollower/pd", tr("PD (line sensors)"),
         tr("The classic line follower: a PD on the sensor array's centroid."),
         "controllers/pd.c"},
        {BlockKind::Filter, "linefollower/lowpass", tr("Low-pass (wheel speeds)"),
         tr("First-order low-pass on w_l and w_r, replacing them."), "filters/lowpass.c"},
    };
    for (const auto& t : templates) {
        code::CodeLibrary::instance().registerTemplate(
            {t.kind, QString::fromLatin1(t.id), t.name, t.description,
             QStringLiteral(":/tracelab/templates/linefollower/") + QLatin1String(t.file)});
    }
}

}  // namespace tracelab::lf
