#include "codecall.h"

#include <setjmp.h>
#include <stddef.h>

#include "libtcc.h"

int tracelab_guarded_call(struct TCCState* state, void (*fn)(void)) {
    jmp_buf jump;
    int code = tcc_setjmp(state, jump, fn);
    if (code == 0) {
        fn();
    }
    _tcc_setjmp(state, NULL, NULL, NULL);
    return code;
}
