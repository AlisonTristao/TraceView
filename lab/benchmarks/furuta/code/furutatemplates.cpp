#include "furutatemplates.h"

#include <QCoreApplication>

#include "lab/code/codefiles.h"

namespace tracelab::furuta {

void registerFurutaTemplates() {
    using code::BlockKind;
    const auto tr = [](const char* text) {
        return QCoreApplication::translate("tracelab::furuta", text);
    };
    const struct {
        BlockKind kind;
        const char* id;
        QString name;
        QString description;
        const char* file;
    } templates[] = {
        {BlockKind::Controller, "furuta/qlpv_mpc", tr("qLPV NMPC (Furuta)"),
         tr("Swing-up and balance by model predictive control on the exact qLPV model of the "
            "pendulum: frozen, warm-start, Taylor or iterative scheduling (QP every tick)."),
         "controllers/qlpv_mpc.c"},
        {BlockKind::Controller, "furuta/energy_lqr", tr("Energy swing-up + LQR"),
         tr("The classic one: pumps the pendulum's energy up, then balances it with an LQR."),
         "controllers/energy_lqr.c"},
        {BlockKind::Plant, "furuta/pendulum", tr("Furuta pendulum"),
         tr("A rotary arm and a pendulum at its tip, Euler-Lagrange model, torque input, "
            "encoders and a disturbance on the pendulum's joint."),
         "plants/pendulum.c"},
    };
    for (const auto& t : templates) {
        code::CodeLibrary::instance().registerTemplate(
            {t.kind, QString::fromLatin1(t.id), t.name, t.description,
             QStringLiteral(":/tracelab/templates/furuta/") + QLatin1String(t.file)});
    }
}

}  // namespace tracelab::furuta
