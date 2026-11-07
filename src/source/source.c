#include "source/source.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(CcSourceError *error, CcSourceStatus status,
                      size_t offset, int os_error)
{
    if (error != NULL) {
        error->status = status;
        error->offset = offset;
        error->os_error = os_error;
    }
}

static bool reserve_bytes(unsigned char **data, size_t *capacity, size_t needed)
{
    if (needed <= *capacity) return true;

    size_t new_capacity = *capacity == 0 ? 4096 : *capacity;
    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX /2) {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2;
    }

    unsigned char *grown = realloc(*data, new_capacity);
    if (grown == NULL) return false;
    *data = grown;
    *capacity = new_capacity;
    return true;
}

static bool push_line(uint32_t **lines, size_t *count, size_t *capacity,
                      uint32_t offset)
{
    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 16 : *capacity;
        if (new_capacity > SIZE_MAX /2 ) return false;
        new_capacity *= 2;
        if (new_capacity > SIZE_MAX / sizeof(**lines)) return false;
        uint32_t *grown = realloc(*lines, new_capacity * sizeof(**lines));
        if (grown==NULL) return false;
        *lines = grown;
        *capacity = new_capacity;
    }
    (*lines)[(*count)++] = offset;
    return true;
}

static bool valid_utf8(const unsigned char *bytes, size_t length,
                       CcSourceError *error)
{
    size_t i = 0;
    while (i < length) {
        size_t start = i;
        unsigned char lead = bytes[i++];

        if (lead == 0) {
            set_error(error, CC_SOURCE_EMBEDDED_NUL, start, 0);
            return false;
        }
        if (lead < 0x80) continue;

        unsigned int codepoint;
        unsigned int minimum;
        size_t following;

        if (lead >= 0xC2 && lead <= 0xDF) {
            codepoint = lead & 0x1FU;
            minimum = 0x80U;
            following = 1;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            codepoint = lead & 0x0FU;
            minimum = 0x800U;
            following = 2;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            codepoint = lead & 0x07U;
            minimum = 0x10000U;
            following = 3;
        } else {
            set_error(error, CC_SOURCE_INVALID_UTF8, start, 0);
            return false;
        }

        if (following > length - i) {
            set_error(error, CC_SOURCE_INVALID_UTF8, start, 0);
            return false;
        }

        for (size_t k = 0; k < following; k++) {
            unsigned char next = bytes[i++];
            if ((next & 0xC0U) != 0x80U) {
                set_error(error, CC_SOURCE_INVALID_UTF8, start, 0);
                return false;
            }
            codepoint = (codepoint << 6) | (next & 0x3FU);
        }

        if (codepoint < minimum || codepoint > 0x10FFFFU ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
            set_error(error, CC_SOURCE_INVALID_UTF8, start, 0);
            return false;
        }
    }
    return true;
}

static void free_file(CcSourceFile *file)
{
    free(file->path);
    free(file->bytes);
    free(file->line_starts);
    *file = (CcSourceFile){0};
}

void cc_source_manager_init(CcSourceManager *manager)
{
    if (manager != NULL) *manager = (CcSourceManager){0};
}

void cc_source_manager_destroy(CcSourceManager *manager)
{
    if (manager == NULL) return;

    for (size_t i = 0; i < manager->count; i++)
        free_file(&manager->files[i]);

    free(manager->files);
    *manager = (CcSourceManager){0};
}

static bool add_owned(CcSourceManager *manager, const char *path,
                      unsigned char *bytes, size_t length,
                      CcFileId *out_id, CcSourceError *error)
{
    CcSourceFile file = {0};
    size_t line_capacity = 0;

    if (length >= UINT32_MAX || manager->count >= CC_FILE_ID_INVALID) {
        set_error(error, CC_SOURCE_TOO_LARGE, 0, 0);
        free(bytes);
        return false;
    }

    if (!valid_utf8(bytes, length, error)) {
        free(bytes);
        return false;
    }

    size_t path_length = strlen(path);
    if (path_length == SIZE_MAX) {
        set_error(error, CC_SOURCE_OUT_OF_MEMORY, 0, 0);
        free(bytes);
        return false;
    }

    file.path = malloc(path_length + 1);
    if (file.path == NULL) goto oom;
    memcpy(file.path, path, path_length + 1);

    file.bytes = bytes;
    file.length = (uint32_t)length;

    if (!push_line(&file.line_starts, &file.line_count, &line_capacity, 0))
        goto oom;

    for (size_t i = 0; i < length; i++) {
        if (bytes[i] == '\r') {
            if (i + 1 < length && bytes[i + 1] == '\n') i++;
            if (!push_line(&file.line_starts, &file.line_count,
                           &line_capacity, (uint32_t)(i + 1)))
                goto oom;
        } else if (bytes[i] == '\n') {
            if (!push_line(&file.line_starts, &file.line_count,
                           &line_capacity, (uint32_t)(i + 1)))
                goto oom;
        }
    }

    if (manager->count == manager->capacity) {
        size_t new_capacity = manager->capacity == 0 ? 8 : manager->capacity;

        if (new_capacity > SIZE_MAX / 2) goto oom;
        new_capacity *= 2;

        if (new_capacity > SIZE_MAX / sizeof(*manager->files)) goto oom;

        CcSourceFile *grown = realloc(
            manager->files, new_capacity * sizeof(*manager->files)
        );
        if (grown == NULL) goto oom;

        manager->files = grown;
        manager->capacity = new_capacity;
    }

    CcFileId id = (CcFileId)manager->count;
    manager->files[manager->count++] = file;

    if (out_id != NULL) *out_id = id;
    set_error(error, CC_SOURCE_OK, 0, 0);
    return true;

oom:
    if (file.bytes == NULL) free(bytes);
    free_file(&file);
    set_error(error, CC_SOURCE_OUT_OF_MEMORY, 0, 0);
    return false;
}

bool cc_source_add_memory(CcSourceManager *manager, const char *name,
                          const unsigned char *bytes, size_t length,
                          CcFileId *out_id, CcSourceError *error)
{
    if (out_id != NULL) *out_id = CC_FILE_ID_INVALID;

    if (manager == NULL || name == NULL || (bytes == NULL && length != 0)) {
        set_error(error, CC_SOURCE_BAD_ARGUMENT, 0, 0);
        return false;
    }

    if (length >= UINT32_MAX || length == SIZE_MAX) {
        set_error(error, CC_SOURCE_TOO_LARGE, 0, 0);
        return false;
    }

    unsigned char *copy = malloc(length + 1);
    if (copy == NULL) {
        set_error(error, CC_SOURCE_OUT_OF_MEMORY, 0, 0);
        return false;
    }

    if (length != 0) memcpy(copy, bytes, length);
    copy[length] = 0;

    return add_owned(manager, name, copy, length, out_id, error);
}

bool cc_source_add_file(CcSourceManager *manager, const char *path,
                        CcFileId *out_id, CcSourceError *error)
{
    if (out_id != NULL) *out_id = CC_FILE_ID_INVALID;

    if (manager == NULL || path == NULL) {
        set_error(error, CC_SOURCE_BAD_ARGUMENT, 0, 0);
        return false;
    }

    FILE *stream = fopen(path, "rb");
    if (stream == NULL) {
        set_error(error, CC_SOURCE_IO_ERROR, 0, errno);
        return false;
    }

    unsigned char *bytes = NULL;
    size_t length = 0;
    size_t capacity = 0;
    unsigned char chunk[4096];
    CcSourceStatus failure = CC_SOURCE_OK;
    int io_error = 0;

    for (;;) {
        size_t read_count = fread(chunk, 1, sizeof(chunk), stream);

        if (read_count != 0) {
            if (length >= UINT32_MAX || read_count >= UINT32_MAX - length) {
                failure = CC_SOURCE_TOO_LARGE;
                break;
            }

            if (!reserve_bytes(&bytes, &capacity, length + read_count + 1)) {
                failure = CC_SOURCE_OUT_OF_MEMORY;
                break;
            }

            memcpy(bytes + length, chunk, read_count);
            length += read_count;
        }

        if (read_count < sizeof(chunk)) {
            if (ferror(stream)) {
                failure = CC_SOURCE_IO_ERROR;
                io_error = errno;
            }
            break;
        }
    }

    if (fclose(stream) != 0 && failure == CC_SOURCE_OK) {
        failure = CC_SOURCE_IO_ERROR;
        io_error = errno;
    }

    if (failure != CC_SOURCE_OK) {
        free(bytes);
        set_error(error, failure, 0, io_error);
        return false;
    }

    if (!reserve_bytes(&bytes, &capacity, length + 1)) {
        free(bytes);
        set_error(error, CC_SOURCE_OUT_OF_MEMORY, 0, 0);
        return false;
    }

    bytes[length] = 0;
    return add_owned(manager, path, bytes, length, out_id, error);
}

const CcSourceFile *cc_source_get(const CcSourceManager *manager, CcFileId id)
{
    if (manager == NULL || id == CC_FILE_ID_INVALID || id >= manager->count)
        return NULL;

    return &manager->files[id];
}

bool cc_source_location(const CcSourceFile *file, size_t offset,
                        size_t *out_line, size_t *out_column)
{
    if (file == NULL || out_line == NULL || out_column == NULL ||
        offset > file->length || file->line_count == 0)
        return false;

    size_t left = 0;
    size_t right = file->line_count;

    while (left < right) {
        size_t mid = left + (right - left) / 2;

        if (file->line_starts[mid] <= offset)
            left = mid + 1;
        else
            right = mid;
    }

    size_t line_index = left - 1;

    *out_line = line_index + 1;
    *out_column = offset - file->line_starts[line_index] + 1;

    return true;
}

const char *cc_source_status_string(CcSourceStatus status)
{
    switch (status) {
    case CC_SOURCE_OK: return "ok";
    case CC_SOURCE_BAD_ARGUMENT: return "invalid argument";
    case CC_SOURCE_IO_ERROR: return "file I/O error";
    case CC_SOURCE_OUT_OF_MEMORY: return "out of memory";
    case CC_SOURCE_TOO_LARGE: return "source file exceeds supported size";
    case CC_SOURCE_INVALID_UTF8: return "invalid UTF-8";
    case CC_SOURCE_EMBEDDED_NUL: return "NUL byte in source file";
    }

    return "unknown source error";
}
