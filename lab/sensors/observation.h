#ifndef TRACELAB_SENSORS_OBSERVATION_H
#define TRACELAB_SENSORS_OBSERVATION_H

// TraceLab sensors: observation blocks, the TRUE state x and input u turned
// into MEASURED outputs,
//
//   y_true = h(x, u, env)              the block's observation
//   y_meas = imperfections(y_true)     one ChannelImperfections per channel
//
// SensorBlock is the one interface every block shares, so linear,
// nonlinear and model-specific blocks (an encoder, a line sensor array)
// mix in one SensorSet:
//
//   LinearObservation      y = H x + D u (H: m x nx, D: m x nu; D may be empty)
//   NonlinearObservation   y = h(x, u, env), the whole vector from one user
//                          function (std::function, or a plain function
//                          pointer plus a context), or one callable per channel
//
// A block steps at its sample period Ts: configure(ts, masterSeed) builds
// the channels' imperfections, channel i drawing from the stream
// deriveSeed(masterSeed, "<block name>/<i>") -- name your blocks uniquely,
// and adding a block never changes another one's noise. Then one sample()
// per tick. Simulation side only (never exported to firmware); plain C++17
// and Eigen, no Qt (target tracelab_sensors).

#include <Eigen/Core>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "imperfections.h"

namespace tracelab::sensors {

// What an observation may look at besides x and u: the time, the tick and
// an opaque pointer to the caller's environment (a track, a map, ...).
struct SensorEnvironment {
    double t = 0.0;
    long long tick = 0;
    const void* user = nullptr;
};

class SensorBlock {
public:
    // std::invalid_argument for outputCount < 1 or an empty name.
    SensorBlock(std::string name, int outputCount);
    virtual ~SensorBlock() = default;
    SensorBlock(const SensorBlock&) = delete;
    SensorBlock& operator=(const SensorBlock&) = delete;

    const std::string& name() const {
        return m_name;
    }
    int outputCount() const {
        return m_outputCount;
    }

    // The imperfections of one channel, or of all; they take effect at the
    // next configure(). Out-of-range channel: std::out_of_range.
    void setImperfections(int channel, const ImperfectionConfig& config);
    void setAllImperfections(const ImperfectionConfig& config);
    const ImperfectionConfig& imperfections(int channel) const;

    // Sample period and master seed; resets the block.
    void configure(double ts, std::uint64_t masterSeed);
    bool configured() const {
        return m_configured;
    }
    double ts() const {
        return m_ts;
    }

    // One tick. std::logic_error before configure().
    void sample(const Eigen::VectorXd& x, const Eigen::VectorXd& u,
                const SensorEnvironment& env = {});
    // Back to tick 0 (same streams); the outputs become NaN.
    void reset();

    // The last sample: the ideal observation and the measured one.
    const Eigen::VectorXd& trueOutput() const {
        return m_true;
    }
    const Eigen::VectorXd& measured() const {
        return m_measured;
    }
    const ChannelImperfections& channel(int index) const;

protected:
    // y_true = h(x, u, env), y already sized outputCount().
    virtual void evaluate(const Eigen::VectorXd& x, const Eigen::VectorXd& u,
                          const SensorEnvironment& env, Eigen::VectorXd& y) = 0;
    // The transducer between the ideal value and the imperfections (an
    // encoder's tick counting, ...): raw starts as y_true. Default: none.
    virtual void transduce(Eigen::VectorXd& raw) {
        (void)raw;
    }
    // The block's own state back to tick 0.
    virtual void resetState() {}

private:
    std::string m_name;
    int m_outputCount;
    std::vector<ImperfectionConfig> m_configs;
    std::vector<ChannelImperfections> m_channels;
    Eigen::VectorXd m_true;
    Eigen::VectorXd m_raw;
    Eigen::VectorXd m_measured;
    double m_ts = 0.0;
    bool m_configured = false;
};

class LinearObservation : public SensorBlock {
public:
    // std::invalid_argument when D is not empty and D.rows() != H.rows().
    LinearObservation(std::string name, Eigen::MatrixXd h, Eigen::MatrixXd d = {});

    const Eigen::MatrixXd& h() const {
        return m_h;
    }
    const Eigen::MatrixXd& d() const {
        return m_d;
    }

protected:
    // std::invalid_argument when x or u does not match H's or D's columns.
    void evaluate(const Eigen::VectorXd& x, const Eigen::VectorXd& u, const SensorEnvironment& env,
                  Eigen::VectorXd& y) override;

private:
    Eigen::MatrixXd m_h;
    Eigen::MatrixXd m_d;
};

class NonlinearObservation : public SensorBlock {
public:
    // The whole measurement vector at once; it must return outputCount values.
    using Function = std::function<Eigen::VectorXd(
        const Eigen::VectorXd& x, const Eigen::VectorXd& u, const SensorEnvironment& env)>;
    // The same as a plain function: writes outputCount values to y.
    using RawFunction = void (*)(const double* x, int nx, const double* u, int nu,
                                 const SensorEnvironment& env, double* y, void* context);
    // One channel.
    using ChannelFunction = std::function<double(const Eigen::VectorXd& x, const Eigen::VectorXd& u,
                                                 const SensorEnvironment& env)>;

    NonlinearObservation(std::string name, int outputCount, Function h);
    NonlinearObservation(std::string name, int outputCount, RawFunction h, void* context);
    NonlinearObservation(std::string name, std::vector<ChannelFunction> channels);

protected:
    // std::runtime_error when h returns the wrong number of values.
    void evaluate(const Eigen::VectorXd& x, const Eigen::VectorXd& u, const SensorEnvironment& env,
                  Eigen::VectorXd& y) override;

private:
    Function m_h;
};

// The sensors of a plant, sampled together.
class SensorSet {
public:
    // Takes ownership; returns the block. std::invalid_argument for a
    // duplicate name.
    SensorBlock& add(std::unique_ptr<SensorBlock> block);
    template <typename Block, typename... Args>
    Block& emplace(Args&&... args) {
        auto block = std::make_unique<Block>(std::forward<Args>(args)...);
        Block& ref = *block;
        add(std::move(block));
        return ref;
    }

    void configure(double ts, std::uint64_t masterSeed);
    void sample(const Eigen::VectorXd& x, const Eigen::VectorXd& u,
                const SensorEnvironment& env = {});
    void reset();

    int size() const {
        return int(m_blocks.size());
    }
    SensorBlock& at(int index) {
        return *m_blocks[std::size_t(index)];
    }
    const SensorBlock& at(int index) const {
        return *m_blocks[std::size_t(index)];
    }
    // nullptr when there is no block of that name.
    SensorBlock* find(const std::string& name);
    const SensorBlock* find(const std::string& name) const;

private:
    std::vector<std::unique_ptr<SensorBlock>> m_blocks;
};

}  // namespace tracelab::sensors

#endif  // TRACELAB_SENSORS_OBSERVATION_H
