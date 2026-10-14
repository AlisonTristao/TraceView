#pragma once

// The one place a code block's function is called: in C, because the fault
// guard is a setjmp()/longjmp() pair and no C++ object may live between the
// two.

#ifdef __cplusplus
extern "C" {
#endif

struct TCCState;

// Calls `fn` (compiled by `state`, which must have been compiled with -bt)
// with a fault guard around it: 0 when it returned normally, the code tcc's
// fault handler jumped back with (non-zero) when an invalid memory access,
// a division by zero... happened in the compiled code. The guard is lifted
// again before returning, so a fault outside a call is never caught.
int tracelab_guarded_call(struct TCCState* state, void (*fn)(void));

#ifdef __cplusplus
}
#endif
