#include "observation.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace tracelab::sensors {

namespace {

Eigen::VectorXd nanVector(int size) {
    return Eigen::VectorXd::Constant(size, std::numeric_limits<double>::quiet_NaN());
}

}  // namespace

// --- SensorBlock ---------------------------------------------------------

SensorBlock::SensorBlock(std::string name, int outputCount)
    : m_name(std::move(name)), m_outputCount(outputCount) {
    if (m_name.empty() || outputCount < 1) {
        throw std::invalid_argument("SensorBlock: a name and at least one output");
    }
    m_configs.resize(std::size_t(outputCount));
    m_true = nanVector(outputCount);
    m_raw = nanVector(outputCount);
    m_measured = nanVector(outputCount);
}

void SensorBlock::setImperfections(int channel, const ImperfectionConfig& config) {
    if (channel < 0 || channel >= m_outputCount) {
        throw std::out_of_range("SensorBlock::setImperfections: channel out of range");
    }
    m_configs[std::size_t(channel)] = config;
}

void SensorBlock::setAllImperfections(const ImperfectionConfig& config) {
    for (ImperfectionConfig& c : m_configs) {
        c = config;
    }
}

const ImperfectionConfig& SensorBlock::imperfections(int channel) const {
    return m_configs.at(std::size_t(channel));
}

void SensorBlock::configure(double ts, std::uint64_t masterSeed) {
    std::vector<ChannelImperfections> channels;
    channels.reserve(m_configs.size());
    for (int i = 0; i < m_outputCount; ++i) {
        channels.emplace_back(m_configs[std::size_t(i)], ts,
                              deriveSeed(masterSeed, m_name + "/" + std::to_string(i)));
    }
    m_channels = std::move(channels);
    m_ts = ts;
    m_configured = true;
    reset();
}

void SensorBlock::reset() {
    for (ChannelImperfections& channel : m_channels) {
        channel.reset();
    }
    m_true = nanVector(m_outputCount);
    m_raw = nanVector(m_outputCount);
    m_measured = nanVector(m_outputCount);
    resetState();
}

void SensorBlock::sample(const Eigen::VectorXd& x, const Eigen::VectorXd& u,
                         const SensorEnvironment& env) {
    if (!m_configured) {
        throw std::logic_error("SensorBlock::sample before configure()");
    }
    m_true.resize(m_outputCount);
    evaluate(x, u, env, m_true);
    m_raw = m_true;
    transduce(m_raw);
    for (int i = 0; i < m_outputCount; ++i) {
        m_measured[i] = m_channels[std::size_t(i)].apply(m_raw[i]);
    }
}

const ChannelImperfections& SensorBlock::channel(int index) const {
    return m_channels.at(std::size_t(index));
}

// --- LinearObservation ---------------------------------------------------

LinearObservation::LinearObservation(std::string name, Eigen::MatrixXd h, Eigen::MatrixXd d)
    : SensorBlock(std::move(name), int(h.rows())), m_h(std::move(h)), m_d(std::move(d)) {
    if (m_d.size() > 0 && m_d.rows() != m_h.rows()) {
        throw std::invalid_argument("LinearObservation: D must have as many rows as H");
    }
}

void LinearObservation::evaluate(const Eigen::VectorXd& x, const Eigen::VectorXd& u,
                                 const SensorEnvironment& env, Eigen::VectorXd& y) {
    (void)env;
    if (x.size() != m_h.cols()) {
        throw std::invalid_argument("LinearObservation: x does not match H");
    }
    y.noalias() = m_h * x;
    if (m_d.size() > 0) {
        if (u.size() != m_d.cols()) {
            throw std::invalid_argument("LinearObservation: u does not match D");
        }
        y.noalias() += m_d * u;
    }
}

// --- NonlinearObservation ------------------------------------------------

NonlinearObservation::NonlinearObservation(std::string name, int outputCount, Function h)
    : SensorBlock(std::move(name), outputCount), m_h(std::move(h)) {
    if (!m_h) {
        throw std::invalid_argument("NonlinearObservation: empty function");
    }
}

NonlinearObservation::NonlinearObservation(std::string name, int outputCount, RawFunction h,
                                           void* context)
    : SensorBlock(std::move(name), outputCount) {
    if (!h) {
        throw std::invalid_argument("NonlinearObservation: null function");
    }
    m_h = [h, context, outputCount](const Eigen::VectorXd& x, const Eigen::VectorXd& u,
                                    const SensorEnvironment& env) {
        Eigen::VectorXd y = Eigen::VectorXd::Zero(outputCount);
        h(x.data(), int(x.size()), u.data(), int(u.size()), env, y.data(), context);
        return y;
    };
}

NonlinearObservation::NonlinearObservation(std::string name, std::vector<ChannelFunction> channels)
    : SensorBlock(std::move(name), int(channels.size())) {
    for (const ChannelFunction& f : channels) {
        if (!f) {
            throw std::invalid_argument("NonlinearObservation: empty channel function");
        }
    }
    m_h = [channels = std::move(channels)](const Eigen::VectorXd& x, const Eigen::VectorXd& u,
                                           const SensorEnvironment& env) {
        Eigen::VectorXd y(Eigen::Index(channels.size()));
        for (std::size_t i = 0; i < channels.size(); ++i) {
            y[Eigen::Index(i)] = channels[i](x, u, env);
        }
        return y;
    };
}

void NonlinearObservation::evaluate(const Eigen::VectorXd& x, const Eigen::VectorXd& u,
                                    const SensorEnvironment& env, Eigen::VectorXd& y) {
    Eigen::VectorXd value = m_h(x, u, env);
    if (value.size() != outputCount()) {
        throw std::runtime_error("NonlinearObservation '" + name() +
                                 "': h returned the wrong number of values");
    }
    y = std::move(value);
}

// --- SensorSet -----------------------------------------------------------

SensorBlock& SensorSet::add(std::unique_ptr<SensorBlock> block) {
    if (!block) {
        throw std::invalid_argument("SensorSet::add: null block");
    }
    if (find(block->name())) {
        throw std::invalid_argument("SensorSet::add: duplicate name '" + block->name() + "'");
    }
    m_blocks.push_back(std::move(block));
    return *m_blocks.back();
}

void SensorSet::configure(double ts, std::uint64_t masterSeed) {
    for (auto& block : m_blocks) {
        block->configure(ts, masterSeed);
    }
}

void SensorSet::sample(const Eigen::VectorXd& x, const Eigen::VectorXd& u,
                       const SensorEnvironment& env) {
    for (auto& block : m_blocks) {
        block->sample(x, u, env);
    }
}

void SensorSet::reset() {
    for (auto& block : m_blocks) {
        block->reset();
    }
}

SensorBlock* SensorSet::find(const std::string& name) {
    for (auto& block : m_blocks) {
        if (block->name() == name) {
            return block.get();
        }
    }
    return nullptr;
}

const SensorBlock* SensorSet::find(const std::string& name) const {
    return const_cast<SensorSet*>(this)->find(name);
}

}  // namespace tracelab::sensors
