#include "plantmanifest.h"

#include <QtGlobal>
#include <btp/catalog.hpp>

namespace tracelab {

// To write (plan phase T2): one Catalog::add_topic() per topic of the header,
// fields named after the model's outputs, inputs, states and parameters.
bool buildPlantCatalog(const sim::ModelDefinition& model, btp::Catalog& catalog) {
    Q_UNUSED(model);
    Q_UNUSED(catalog);
    return false;
}

}  // namespace tracelab
