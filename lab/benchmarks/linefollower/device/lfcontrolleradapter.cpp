#include "lfcontrolleradapter.h"

#include <QCoreApplication>
#include <array>
#include <cmath>
#include <vector>

#include "lab/filters/runtime/tlfilter.h"
#include "lfcontroller.h"
#include "pdcontroller.h"

namespace tracelab::lf {

// Two first-order low-passes, one per wheel, as a tlf_bank in a tlf_chain:
// the same C structures the robot firmware would run.
struct LineControllerAdapter::Filter {
    tlf_lowpass1 lowpass[2];
    tlf_block wheels[2];
    tlf_bank bank;
    tlf_block bankBlock;
    tlf_chain chain;
    bool active = false;

    void init(double tau, double ts) {
        active = tau > 0.0;
        for (int i = 0; i < 2; ++i) {
            tlf_lowpass1_init(&lowpass[i], tlf_real(tau), tlf_real(ts), /*prime=*/1);
            wheels[i] = tlf_lowpass1_block(&lowpass[i]);
        }
        tlf_bank_init(&bank, wheels, 2);
        bankBlock = tlf_bank_block(&bank);
        tlf_chain_init(&chain, &bankBlock, 1);
    }
    void step(double* left, double* right) {
        if (!active) {
            return;
        }
        const tlf_real in[2] = {tlf_real(*left), tlf_real(*right)};
        tlf_real out[2] = {0, 0};
        tlf_chain_step(&chain, in, out);
        *left = out[0];
        *right = out[1];
    }
    void reset() {
        tlf_chain_reset(&chain);
    }
};

ControllerSettings ControllerSettings::fromJson(const QJsonObject& json) {
    ControllerSettings s;
    s.plantDeviceId = json.value("plantDeviceId").toString();
    const QString type = json.value("controller").toString(s.type);
    s.type = type == QLatin1String("gpc") || type == QLatin1String("pd") ? type
                                                                           : QStringLiteral("qlpv");
    s.fps = qBound(1, json.value("fps").toInt(s.fps), 1000);
    s.horizon = qBound(2, json.value("horizon").toInt(s.horizon), 256);
    s.filterTau = qMax(0.0, json.value("filterTau").toDouble(s.filterTau));
    s.qLateral = json.value("qLateral").toDouble(s.qLateral);
    s.qTheta = json.value("qTheta").toDouble(s.qTheta);
    s.gpcQTheta = json.value("gpcQTheta").toDouble(s.gpcQTheta);
    s.gpcQDistance = json.value("gpcQDistance").toDouble(s.gpcQDistance);
    s.gpcLambda = json.value("gpcLambda").toDouble(s.gpcLambda);
    const QString scheduling = json.value("qlpvScheduling").toString(s.qlpvScheduling);
    if (scheduling == QLatin1String("frozen") || scheduling == QLatin1String("warm_start") ||
        scheduling == QLatin1String("taylor") || scheduling == QLatin1String("iterative")) {
        s.qlpvScheduling = scheduling;
    }
    const QString embedding = json.value("qlpvEmbedding").toString(s.qlpvEmbedding);
    if (embedding == QLatin1String("sinc") || embedding == QLatin1String("sin_cos")) {
        s.qlpvEmbedding = embedding;
    }
    s.qlpvIterations = qBound(1, json.value("qlpvIterations").toInt(s.qlpvIterations), 50);
    s.pdKp = json.value("pdKp").toDouble(s.pdKp);
    s.pdKd = json.value("pdKd").toDouble(s.pdKd);
    s.pdBase = json.value("pdBase").toDouble(s.pdBase);
    return s;
}

QJsonObject ControllerSettings::toJson() const {
    return QJsonObject{
        {"plantDeviceId", plantDeviceId}, {"controller", type},
        {"fps", fps},                     {"horizon", horizon},
        {"filterTau", filterTau},         {"qLateral", qLateral},
        {"qTheta", qTheta},               {"gpcQTheta", gpcQTheta},
        {"gpcQDistance", gpcQDistance},   {"gpcLambda", gpcLambda},
        {"pdKp", pdKp},                   {"pdKd", pdKd},
        {"pdBase", pdBase},
        {"qlpvScheduling", qlpvScheduling},
        {"qlpvEmbedding", qlpvEmbedding},
        {"qlpvIterations", qlpvIterations},
    };
}

LineControllerAdapter::LineControllerAdapter(const ControllerSettings& settings)
    : m_settings(settings), m_filter(std::make_unique<Filter>()) {
    rebuild();
}

LineControllerAdapter::~LineControllerAdapter() = default;

void LineControllerAdapter::setSettings(const ControllerSettings& settings) {
    m_settings = settings;
    rebuild();
}

void LineControllerAdapter::rebuild() {
    // The in-process harness's ControllerConfig (lfrunner.cpp): Ts, N, the
    // car's default parameters, plus the weights edited here.
    ControllerConfig config;
    config.type = m_settings.type.toStdString();
    config.ts = 1.0 / m_settings.fps;
    config.horizon = m_settings.horizon;
    config.qlpv.q[0] = m_settings.qTheta;
    config.qlpv.q[1] = m_settings.qLateral;
    config.qlpv.scheduling = m_settings.qlpvScheduling.toStdString();
    config.qlpv.embedding = m_settings.qlpvEmbedding.toStdString();
    config.qlpv.lMax = m_settings.qlpvIterations;
    config.gpc.qTheta = m_settings.gpcQTheta;
    config.gpc.qDistance = m_settings.gpcQDistance;
    config.gpc.lamb = m_settings.gpcLambda;
    m_controller.reset();
    m_pd.reset();
    if (m_settings.type == QLatin1String("pd")) {
        PdParams pd;
        pd.kp = m_settings.pdKp;
        pd.kd = m_settings.pdKd;
        pd.base = m_settings.pdBase;
        pd.ts = config.ts;
        m_pd = std::make_unique<PdController>(pd);
    } else {
        m_controller = makeController(config);
    }
    m_filter->init(m_settings.filterTau, config.ts);
}

QString LineControllerAdapter::name() const {
    QString text = m_pd           ? QCoreApplication::translate("LineControllerAdapter",
                                                                  "PD (line sensors)")
                   : m_controller ? QString::fromStdString(m_controller->name())
                                  : QCoreApplication::translate("LineControllerAdapter",
                                                                "no controller");
    if (m_settings.filterTau > 0.0) {
        text += QCoreApplication::translate("LineControllerAdapter", " + low-pass %1 s")
                    .arg(m_settings.filterTau);
    }
    return text;
}

void LineControllerAdapter::setSignalNames(const QStringList& measurements,
                                           const QStringList& inputs) {
    Q_UNUSED(inputs);
    m_leftIndex = qMax(0, measurements.indexOf(QStringLiteral("w_l")));
    m_rightIndex = measurements.indexOf(QStringLiteral("w_r"));
    if (m_rightIndex < 0) {
        m_rightIndex = 1;
    }
    m_sensorIndices.clear();
    for (int i = 0;; ++i) {
        const int index = measurements.indexOf(QStringLiteral("s[%1]").arg(i));
        if (index < 0) {
            break;
        }
        m_sensorIndices.append(index);
    }
}

bool LineControllerAdapter::configure(int measurementCount, int inputCount, QString* error) {
    if (m_pd) {
        if (m_sensorIndices.isEmpty() || inputCount < 2) {
            if (error) {
                *error = QCoreApplication::translate(
                    "LineControllerAdapter", "the plant does not publish s[] and take u_l, u_r");
            }
            return false;
        }
        return true;
    }
    if (measurementCount <= qMax(m_leftIndex, m_rightIndex) || inputCount < 2) {
        if (error) {
            *error = QCoreApplication::translate(
                "LineControllerAdapter", "the plant does not publish w_l, w_r and take u_l, u_r");
        }
        return false;
    }
    return m_controller != nullptr;
}

void LineControllerAdapter::reset() {
    if (m_controller) {
        m_controller->reset();
    }
    if (m_pd) {
        m_pd->reset();
    }
    m_filter->reset();
}

QVector<ControllerParam> LineControllerAdapter::params() const {
    return {
        {QStringLiteral("filter_tau"), QStringLiteral("s"), m_settings.filterTau, 0.0, 1.0},
        {QStringLiteral("q_lateral"), QString(), m_settings.qLateral, 0.0, 1e7},
        {QStringLiteral("q_theta"), QString(), m_settings.qTheta, 0.0, 1e7},
        {QStringLiteral("gpc_q_theta"), QString(), m_settings.gpcQTheta, 0.0, 1e3},
        {QStringLiteral("gpc_q_distance"), QString(), m_settings.gpcQDistance, 0.0, 1e3},
        {QStringLiteral("gpc_lambda"), QString(), m_settings.gpcLambda, 0.0, 10.0},
        {QStringLiteral("pd_kp"), QStringLiteral("%/m"), m_settings.pdKp, 0.0, 1e5},
        {QStringLiteral("pd_kd"), QStringLiteral("%s/m"), m_settings.pdKd, 0.0, 1e4},
        {QStringLiteral("pd_base"), QStringLiteral("%"), m_settings.pdBase, -100.0, 100.0},
    };
}

void LineControllerAdapter::setParam(const QString& name, double value) {
    ControllerSettings s = m_settings;
    if (name == QLatin1String("filter_tau")) {
        s.filterTau = qMax(0.0, value);
    } else if (name == QLatin1String("q_lateral")) {
        s.qLateral = value;
    } else if (name == QLatin1String("q_theta")) {
        s.qTheta = value;
    } else if (name == QLatin1String("gpc_q_theta")) {
        s.gpcQTheta = value;
    } else if (name == QLatin1String("gpc_q_distance")) {
        s.gpcQDistance = value;
    } else if (name == QLatin1String("gpc_lambda")) {
        s.gpcLambda = value;
    } else if (name == QLatin1String("pd_kp")) {
        s.pdKp = value;
    } else if (name == QLatin1String("pd_kd")) {
        s.pdKd = value;
    } else if (name == QLatin1String("pd_base")) {
        s.pdBase = value;
    } else {
        return;
    }
    setSettings(s);
}

QStringList LineControllerAdapter::diagNames() const {
    return {QStringLiteral("qps"),       QStringLiteral("qp_iterations"),
            QStringLiteral("rho_error"), QStringLiteral("theta_ref"),
            QStringLiteral("theta_pred"), QStringLiteral("w_l_filt"),
            QStringLiteral("w_r_filt")};
}

QStringList LineControllerAdapter::diagUnits() const {
    return {QString(),           QString(),           QString(),
            QStringLiteral("rad"), QStringLiteral("rad"), QStringLiteral("rad/s"),
            QStringLiteral("rad/s")};
}

bool LineControllerAdapter::computePd(const ControllerInput& input, ControllerOutput* output) {
    std::vector<double> sensors;
    sensors.reserve(std::size_t(m_sensorIndices.size()));
    for (const int index : std::as_const(m_sensorIndices)) {
        sensors.push_back(input.y.value(index, 0.0));
    }
    const std::array<double, 2> u = m_pd->step(sensors);
    output->u = {QVector<double>{u[0], u[1]}};
    // No QP and no prediction; theta_ref carries the line offset (m) the PD
    // acts on, w_l/w_r what the encoders said.
    output->iterations = 0;
    output->diag = {0.0, 0.0, 0.0, m_pd->error(), qQNaN(), input.y.value(m_leftIndex),
                    input.y.value(m_rightIndex)};
    return true;
}

bool LineControllerAdapter::compute(const ControllerInput& input, ControllerOutput* output) {
    if (m_pd) {
        return computePd(input, output);
    }
    if (!m_controller || input.reference.isEmpty()) {
        return false;  // no preview of this tick: hold
    }
    double left = input.y.value(m_leftIndex);
    double right = input.y.value(m_rightIndex);
    m_filter->step(&left, &right);

    const int count = int(input.reference.size() / 2);
    std::vector<CarPoint> future;
    future.reserve(std::size_t(count));
    for (int i = 0; i < count; ++i) {
        future.push_back({input.reference[i], input.reference[count + i]});
    }
    const std::array<double, 2> u = m_controller->step({left, right}, future);
    output->u = {QVector<double>{u[0], u[1]}};
    const Prediction prediction = m_controller->prediction();
    output->predictedX = QVector<double>(prediction.x.begin(), prediction.x.end());
    output->predictedY = QVector<double>(prediction.y.begin(), prediction.y.end());
    output->predictedTheta = QVector<double>(prediction.theta.begin(), prediction.theta.end());
    const ControllerStats stats = m_controller->stats();
    output->iterations = stats.qps;
    // The heading the controller aims at on the first step (its own
    // reference when it has one, else the bearing of the first preview
    // point) next to the heading it predicts there.
    const Prediction reference = m_controller->reference();
    const double thetaRef = !reference.theta.empty()
                                ? reference.theta.front()
                                : (count > 0 ? std::atan2(input.reference[0], input.reference[count])
                                             : qQNaN());
    const double thetaPred = prediction.theta.empty() ? qQNaN() : prediction.theta.front();
    output->diag = {double(stats.qps), double(stats.qpIterations), stats.rhoError, thetaRef,
                    thetaPred, left, right};
    return true;
}

}  // namespace tracelab::lf
