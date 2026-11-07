
#include "source/source.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_memory_and_locations(void)
{
    const unsigned char code[] = "abc\r\ndef\nxyz\r123";
    CcSourceManager sources;
    CcSourceError error;
    CcFileId id;

    cc_source_manager_init(&sources);

    assert(cc_source_add_memory(&sources, "memory.caret", code,
                                sizeof(code) - 1, &id, &error));
    assert(error.status == CC_SOURCE_OK);
    assert(id == 0);

    const CcSourceFile *file = cc_source_get(&sources, id);

    assert(file != NULL && file->length == sizeof(code) - 1);
    assert(file->line_count == 4);

    size_t line = 0, col = 0;

    assert(cc_source_location(file, 0, &line, &col) && line == 1 && col == 1);
    assert(cc_source_location(file, 5, &line, &col) && line == 2 && col == 1);
    assert(cc_source_location(file, 9, &line, &col) && line == 3 && col == 1);
    assert(cc_source_location(file, 13, &line, &col) && line == 4 && col == 1);
    assert(cc_source_location(file, file->length, &line, &col));
    assert(!cc_source_location(file, file->length + 1, &line, &col));

    cc_source_manager_destroy(&sources);
}

static void test_invalid_inputs(void)
{
    const unsigned char overlong[] = { 0xC0, 0xAF };
    const unsigned char truncated[] = { 0xE2, 0x82 };
    const unsigned char surrogate[] = { 0xED, 0xA0, 0x80 };
    const unsigned char nul[] = { 'a', 0, 'b' };

    CcSourceManager sources;
    CcSourceError error;
    CcFileId id;

    cc_source_manager_init(&sources);

    assert(!cc_source_add_memory(&sources, "bad", overlong,
                                  sizeof(overlong), &id, &error));
    assert(error.status == CC_SOURCE_INVALID_UTF8 && error.offset == 0);

    assert(!cc_source_add_memory(&sources, "bad", truncated,
                                  sizeof(truncated), &id, &error));
    assert(error.status == CC_SOURCE_INVALID_UTF8);

    assert(!cc_source_add_memory(&sources, "bad", surrogate,
                                  sizeof(surrogate), &id, &error));
    assert(error.status == CC_SOURCE_INVALID_UTF8);

    assert(!cc_source_add_memory(&sources, "bad", nul,
                                  sizeof(nul), &id, &error));
    assert(error.status == CC_SOURCE_EMBEDDED_NUL && error.offset == 1);

    assert(sources.count == 0);

    cc_source_manager_destroy(&sources);
}

static void test_empty_and_utf8(void)
{
    const unsigned char unicode[] = { 'x', 0xE2, 0x82, 0xAC, '\n' };

    CcSourceManager sources;
    CcSourceError error;
    CcFileId id;

    cc_source_manager_init(&sources);

    assert(cc_source_add_memory(&sources, "empty", NULL, 0, &id, &error));
    assert(cc_source_get(&sources, id)->line_count == 1);

    assert(cc_source_add_memory(&sources, "unicode", unicode,
                                sizeof(unicode), &id, &error));

    const CcSourceFile *file = cc_source_get(&sources, id);
    assert(file->line_count == 2);

    size_t line, column;

    assert(cc_source_location(file, 4, &line, &column));
    assert(line == 1 && column == 5); /* byte columns, not display columns */

    cc_source_manager_destroy(&sources);
}

static void test_file_loading(void)
{
    CcSourceManager sources;
    CcSourceError error;
    CcFileId id;

    cc_source_manager_init(&sources);

    assert(cc_source_add_file(&sources, "tests/fixtures/source_sample.caret",
                              &id, &error));

    const CcSourceFile *file = cc_source_get(&sources, id);

    assert(file && strstr((const char *)file->bytes, "return 42;") != NULL);
    assert(file->line_count == 4);

    assert(!cc_source_add_file(&sources, "tests/fixtures/absent.caret",
                               &id, &error));
    assert(error.status == CC_SOURCE_IO_ERROR);

    cc_source_manager_destroy(&sources);
}

int main(void)
{
    test_memory_and_locations();
    test_invalid_inputs();
    test_empty_and_utf8();
    test_file_loading();

    puts("source manager: all tests passed");
    return 0;
}

