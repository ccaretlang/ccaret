#ifndef CCARET_BACKEND_C_EMITTER_H
#define CCARET_BACKEND_C_EMITTER_H

#include "ir/ir.h"

#include <stdbool.h>
#include <stdio.h>

/* Both APIs require an IR module accepted by the independent verifier. */
bool cc_c_emit(FILE *out, const CcIrModule *module, CcIrError *error);
bool cc_c_has_main(const CcIrModule *module);

#endif
