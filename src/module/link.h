#ifndef CCARET_MODULE_LINK_H
#define CCARET_MODULE_LINK_H

#include "ast/arena.h"
#include "source/source.h"

#include <stdbool.h>

typedef struct CcModuleOffset CcModuleOffset;

struct CcModuleOffset {
    uint32_t start;
    uint32_t end;
    CcFileId original_file;
    uint32_t original_start;
    uint32_t original_end;
    CcModuleOffset *next;
};

typedef struct {
    CcFileId linked_file;
    bool modular;
    CcModuleOffset *offsets;
} CcModuleLink;

typedef struct {
    CcSourceSpan span;
    const char *stage;
    const char *message;
} CcModuleError;

/*
 * Resolve an entry file's local module graph and link its declarations into
 * one source unit for the existing semantic -> verified IR -> C17 pipeline.
 * Legacy files without a module header are returned untouched.
 *
 * CcSourceManager and CcArena must outlive the returned CcModuleLink.
 */
bool cc_module_link(CcSourceManager *sources, CcArena *arena,
                    CcFileId entry, CcModuleLink *result,
                    CcModuleError *error);

/* Translate a diagnostic span in the linked source back to its origin. */
CcSourceSpan cc_module_origin(const CcModuleLink *link, CcSourceSpan span);

#endif
