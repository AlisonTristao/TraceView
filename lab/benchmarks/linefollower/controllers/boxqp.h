#ifndef TRACELAB_LF_BOXQP_H
#define TRACELAB_LF_BOXQP_H

// The box-constrained QP of the line follower's MPC: TraceLab's generic one
// (lab/numeric/boxqp.h), under the names the controllers and their tests use.

#include "lab/numeric/boxqp.h"

namespace tracelab::lf {

using numeric::BoxQpResult;
using numeric::kBoxQpMaxIter;
using numeric::solveBoxQp;
using numeric::solveBoxQpProjected;

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_BOXQP_H
