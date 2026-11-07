
#ifndef CCARET_SOURCE_H
#define CCARET_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t CcFileId;

#define CC_FILE_ID_INVALID UINT32_MAX

typedef struct {
    CcFileId file_id;
    uint32_t start;
    uint32_t end;
} CcSourceSpan;

typedef enum {
    CC_SOURCE_OK = 0,
    CC_SOURCE_BAD_ARGUMENT,
    CC_SOURCE_IO_ERROR,
    CC_SOURCE_OUT_OF_MEMORY,
    CC_SOURCE_TOO_LARGE,
    CC_SOURCE_INVALID_UTF8,
    CC_SOURCE_EMBEDDED_NUL
} CcSourceStatus;

typedef struct {
    CcSourceStatus status;
    size_t offset;
    int os_error;
} CcSourceError;

/*
 * Each \n, \r\n, or \r starts a new line. A trailing terminator therefore
 * leaves an empty final line, and line_count is the number of terminators
 * plus one.
 */
typedef struct {
    char *path;
    unsigned char *bytes;
    uint32_t length;
    uint32_t *line_starts;
    size_t line_count;
} CcSourceFile;

typedef struct {
    CcSourceFile *files;
    size_t count;
    size_t capacity;
} CcSourceManager;

void cc_source_manager_init(CcSourceManager *manager);
void cc_source_manager_destroy(CcSourceManager *manager);

bool cc_source_add_memory(
    CcSourceManager *manager,
    const char *name,
    const unsigned char *bytes,
    size_t length,
    CcFileId *out_id,
    CcSourceError *out_error
);

bool cc_source_add_file(
    CcSourceManager *manager,
    const char *path,
    CcFileId *out_id,
    CcSourceError *out_error
);

const CcSourceFile *cc_source_get(
    const CcSourceManager *manager,
    CcFileId id
);

bool cc_source_location(
    const CcSourceFile *file,
    size_t offset,
    size_t *out_line,
    size_t *out_column
);

const char *cc_source_status_string(CcSourceStatus status);

#endif

