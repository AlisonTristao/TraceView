#include "lfcontroller.h"

#include "gpccontroller.h"
#include "qlpvcontroller.h"
#include "scheduling.h"

namespace tracelab::lf {

// runner.py make_controller: the "gpc" section for type "gpc", otherwise the
// qLPV model with the "qlpv" section (iterative kwargs only for iterative).
std::unique_ptr<Controller> makeController(const ControllerConfig& config) {
    if (config.horizon < 2) {
        return nullptr;
    }
    if (config.type == "gpc") {
        return std::make_unique<GpcController>(config);
    }
    if (config.type != "qlpv") {
        return nullptr;
    }
    Embedding embedding;
    if (config.qlpv.embedding == "sinc") {
        embedding = Embedding::kSinc;
    } else if (config.qlpv.embedding == "sin_cos") {
        embedding = Embedding::kSinCos;
    } else {
        return nullptr;
    }
    std::unique_ptr<Scheduling> scheduling = makeScheduling(config.qlpv.scheduling, config.qlpv);
    if (scheduling == nullptr) {
        return nullptr;
    }
    return std::make_unique<QlpvController>(config, embedding, std::move(scheduling));
}

}  // namespace tracelab::lf
