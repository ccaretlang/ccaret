#include "semantic/semantic.h"
#include "parser/parser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    CcSourceManager sources;
    CcFileId id;
    CcArena arena;
    CcLexer lexer;
    CcParser parser;
} Fixture;

static void init(Fixture *fixture, const char *text)
{
    CcSourceError source_error;

    cc_source_manager_init(&fixture->sources);
    assert(cc_source_add_memory(&fixture->sources, "semantic test",
                                (const unsigned char *)text, strlen(text),
                                &fixture->id, &source_error));

    const CcSourceFile *file = cc_source_get(&fixture->sources, fixture->id);

    assert(file != NULL);
    cc_arena_init(&fixture->arena);
    cc_lexer_init(&fixture->lexer, file, fixture->id);
    cc_parser_init(&fixture->parser, &fixture->lexer, &fixture->arena);
}

static void destroy(Fixture *fixture)
{
    cc_arena_destroy(&fixture->arena);
    cc_source_manager_destroy(&fixture->sources);
}

static void valid(const char *source)
{
    Fixture fixture;

    init(&fixture, source);
    CcFunctionDeclaration *functions =
        cc_parser_parse_program(&fixture.parser);

    if (fixture.parser.failed)
        fprintf(stderr, "unexpected parse error: %s\n", fixture.parser.error);
    assert(!fixture.parser.failed);

    CcSemanticError error;
    const CcSourceFile *file = cc_source_get(&fixture.sources, fixture.id);
    bool ok = cc_semantic_check(file, functions, &fixture.arena, &error);

    if (!ok)
        fprintf(stderr, "unexpected semantic error: %s at byte %u in %s\n",
                error.message, error.span.start, source);
    assert(ok);
    assert(error.message == NULL);

    destroy(&fixture);
}

static void invalid(const char *source, const char *message,
                    const char *at)
{
    Fixture fixture;

    init(&fixture, source);
    CcFunctionDeclaration *functions =
        cc_parser_parse_program(&fixture.parser);

    if (fixture.parser.failed)
        fprintf(stderr, "unexpected parse error: %s in %s\n",
                fixture.parser.error, source);
    assert(!fixture.parser.failed);

    CcSemanticError error;
    const CcSourceFile *file = cc_source_get(&fixture.sources, fixture.id);
    bool ok = cc_semantic_check(file, functions, &fixture.arena, &error);

    if (ok || error.message == NULL || strcmp(error.message, message) != 0)
        fprintf(stderr, "unexpected result for %s: %s\n",
                source, error.message == NULL ? "success" : error.message);
    assert(!ok);
    assert(error.message != NULL);
    assert(strcmp(error.message, message) == 0);
    assert(error.span.file_id == fixture.id);
    assert(error.span.start <= error.span.end);
    assert(error.span.end <= file->length);

    if (at != NULL) {
        size_t len = strlen(at);

        assert(error.span.end - error.span.start == len);
        assert(memcmp(file->bytes + error.span.start, at, len) == 0);
    }

    destroy(&fixture);
}

static void test_valid_programs(void)
{
    static const char *const programs[] = {
        "",
        "fn main() -> i32 { return 42; }",
        "fn divmod(a: i32, b: i32) -> (i32, i32) {"
        " return (a / b, a % b); }"
        " fn main() -> i32 { let (q, r) = divmod(17, 5); return q + r; }",
        "fn main() -> i32 { return twice(21); }"
        " fn twice(x: i32) -> i32 { return x + x; }",
        "fn present() -> ?i32 { return some(42); }"
        " fn absent() -> ?i32 { return none; }",
        "fn f() { let flag: bool = true;"
        " if flag { let x: i32 = 2; }"
        " else { var y: u8 = 3; y = 4; } }",
        "fn f() { var n: i32 = 0; while n < 10 {"
        " n += 1; if n == 5 { break; } else { continue; } } }",
        "fn f() { for i in 0..10 { let x: i32 = i; } }",
        "fn f(a: [2 + 3]u8) -> usize { return a.len; }",
        "fn f(a: []const u8, i: usize) -> u8 { return a[i]; }",
        "fn f(x: (i32, bool)) -> bool { return x.1; }",
        "fn f(x: *const u8) -> *const u8 { return x; }",
        "fn f(x: i8) -> i8 { return -128; }",
        "fn f() -> (i32,) { let (x,): (i32,) = (1,); return (x,); }",
        "fn f() -> bool { return (1 < 2) && true; }",
        "fn f() { return; }",
        "fn f() -> i32 { let x = 1;"
        " if true { let x: bool = false; } return x; }",
        "fn f(x: f32) -> f32 { return x; }",
        "fn s() -> []const u8 { return \"hello\"; }"
        " fn cs() -> *const u8 { return c\"hello\"; }",
        "fn f() -> u8 { return 255; }",
        "fn f() -> ?i32 { let x = some(1); return x; }",
        "fn f() -> () { return (); }",
        "fn f() -> ((i32, i32), ?bool) { return ((1, 2), none); }",
        "fn f() -> i32 { if true { return 1; } else { return 2; } }",
        "fn f() -> i32 { if true { return 1; }"
        " else if false { return 2; } else { return 3; } }",
        "fn f(x: i32) -> i32 { return x; }"
        " fn g() -> i32 { return f(3); }",
        "fn f() { var a: i32 = 1; a = 2; a *= 3; }",
        "fn f(a: (i32, (bool, i32))) -> i32 {"
        " let (x, (flag, y)) = a;"
        " if flag { return x; } else { return y; } }",
        "fn f() { let x: ?*const u8 = none; }",
        "fn f(x: bool) -> bool { return !x; }",
        "fn f(a: i32) -> bool { return a == 1; }",
        "fn f() -> bool { return (1 + 2) == 3; }",
        "fn f() { let x = (1, 2); let y: i32 = x.0; }",
        "fn f() -> i32 { let x: i64 = 42; return 1; }",
        "fn f(x: f32) -> f32 { var y: f32 = x; y += x; return y; }",
        "fn f() -> i64 { return 2147483648; }",
        "fn f() -> i32 { return 0xff; }",
        "fn f(x: i32) -> i32 {"
        " if x == 0 { return 0; } else { return f(x - 1); } }",
        "fn f(a: (i32, i32)) -> i32 { return a.0 + a.1; }",
        "fn f() { let () = (); }",
        "fn f() -> ?*const u8 { return none; }",
        "fn f() -> i32 { let m = -2147483648; return m; }",
        "fn f() -> i32 { var sum: i32 = 0;"
        " for i in -2147483648..=-2147483647 { sum += 1; }"
        " return sum; }",
        "fn f() -> i32 { var n: i32 = 1; n &= 3; n ^= 2;"
        " n |= 4; return ~n + (2147483647 +% 1); }",
        "fn f() -> [2]i32 { return [20, 22]; }",
        "fn f() { let a = [1, 2,]; let x = a[0]; }",
        "fn f() -> [0]bool { return []; }",
        "fn f() { let a: [2]?i32 = [some(1), none]; }",
        "fn f() { var a: [2]i32 = [1, 2]; a[0] = 10;"
        " a[1] += 1; }",
        "fn f(a: [2][2]i32) -> i32 { return a[0][1]; }",
        "fn f(a: [3]i32) -> usize { return a.len; }",
        "fn f() { let data = [20, 22];"
        " let v: []const i32 = slice(data);"
        " let first = v[0]; let size: usize = v.len; }",
        "fn f() { var data = [20, 22];"
        " let v: []i32 = slice(data); v[0] = 1; v[1] += 3; }",
        "fn f(view: []i32) -> []i32 { return view; }",
        "fn f() { var empty: [0]bool = [];"
        " let v: []bool = slice(empty); let n = v.len; }",
        "fn f() { var data = [0, 20, 22, 0];"
        " let view: []i32 = data[1..3];"
        " let narrow = view[0..2]; narrow[0] = 1; }",
        "fn f() { let data = [0, 20, 22, 0];"
        " let view: []const i32 = data[1..3];"
        " let middle: []const i32 = view[0..1]; }",
        "fn f() { var empty: [0]i32 = [];"
        " let view: []i32 = empty[0..0]; }",
        "fn f() { let a = [0, 20, 22, 0];"
        " let v = slice(a)[1..3][0..1]; }",
        "struct Marker {}",
        "pub struct Vec { x: i32, y: i32, }"
        "fn f() -> Vec { return Vec { y: 22, x: 20 }; }",
        "fn f() -> i32 { return Thing { n: 42 }.n; }"
        "struct Thing { n: i32 }",
        "struct P { n: i32 }"
        "fn f() { var p = P { n: 1 }; p.n += 2; }",
        "struct P { child: ?i32 }"
        "fn f() { let arr: [1]P = [P { child: none }]; }",
        "struct One { value: i32 } struct Two { value: i32 }"
        "fn a(value: One) -> i32 { return value.value; }"
        "fn b(value: Two) -> i32 { return value.value; }",
        "fn f() -> i32 { var n = 42; let p = &n;"
        " unsafe { return *p; } }",
        "fn f() -> i32 { let n = 42; let p: *const i32 = &n;"
        " unsafe { return *p; } }",
        "fn f(p: *i32) -> i32 { unsafe { *p += 2; return *p; } }",
        "fn f() { var n: i32 = 1; let p: *i32 = &n;"
        " unsafe { *p = 2; *p *= 3; } }",
        "fn f() { let flag: bool = true; let p = &flag;"
        " unsafe { let b = *p; } }",
        "fn f() { var n = 1; let p = &n; let pp = &p;"
        " unsafe { let value = **pp; } }",
        "fn f() -> ?*i32 { var x = 1; return some(&x); }",
        "fn f() { var n: i32 = 3; unsafe { while n < 4 {"
        " let p = &n; *p += 1; } } }"
    };

    for (size_t i = 0; i < sizeof(programs) / sizeof(programs[0]); i++)
        valid(programs[i]);
}

static void test_invalid_programs(void)
{
    static const struct {
        const char *source;
        const char *message;
        const char *at;
    } cases[] = {
        { "fn f() -> i32 { return missing; }", "undefined name",
          "missing" },
        { "fn f(x: Missing) {}", "unknown type", "Missing" },
        { "fn f() {} fn f() {}", "duplicate function name", "f" },
        { "fn f(x: i32, x: i32) {}", "duplicate parameter name", "x" },
        { "fn f() { let a = 1; let a = 2; }",
          "duplicate binding in scope", "a" },
        { "fn f(x: i32) { let x = 1; }",
          "duplicate binding in scope", "x" },
        { "fn f() { let (a, a) = (1, 2); }",
          "duplicate binding in scope", "a" },
        { "fn f() { let x = x; }", "undefined name", "x" },
        { "fn f() { if true { let x = 1; } let y = x; }",
          "undefined name", "x" },
        { "fn f() { if 1 { return; } }", "type mismatch", "1" },
        { "fn f() -> i32 { return true; }", "type mismatch", "true" },
        { "fn f() { return 1; }",
          "void function cannot return a value", "1" },
        { "fn f() -> i32 { return; }",
          "return value required", NULL },
        { "fn f() -> i32 {}", "not all control-flow paths return a value",
          NULL },
        { "fn f() -> i32 { if true { return 1; } }",
          "not all control-flow paths return a value", NULL },
        { "fn f() -> i32 { return g(); } fn g(x: i32) -> i32 {return x;}",
          "wrong number of call arguments", "g()" },
        { "fn f() -> i32 { return g(true); }"
          " fn g(x: i32) -> i32 { return x; }",
          "type mismatch", "true" },
        { "fn f() -> i32 { return g(1); }",
          "undefined function", "g" },
        { "fn f() { let x: i32 = some(1); }",
          "some requires an optional type", "some(1)" },
        { "fn f() { let x = none; }",
          "none requires an optional type context", "none" },
        { "fn f() { let x = some(none); }",
          "none requires an optional type context", "none" },
        { "fn f() { let x: (i32, i32) = (1,); }",
          "tuple arity mismatch", "(1,)" },
        { "fn f() { let (a, b) = (1,); }",
          "tuple destructuring arity mismatch", "(a, b)" },
        { "fn f() { let (a, b) = 1; }",
          "tuple pattern requires a tuple value", "(a, b)" },
        { "fn f() { let x = 1; x = 2; }",
          "cannot assign to immutable binding", "x" },
        { "fn f() { let x: u8 = 256; }",
          "integer literal out of range for target type", "256" },
        { "fn f() { let x = 2147483648; }",
          "integer literal does not fit i32", "2147483648" },
        { "fn f() -> u8 { return -1; }",
          "negative integer out of range", "-1" },
        { "fn f() -> i8 { return -129; }",
          "negative integer out of range", "-129" },
        { "fn f() -> u64 { return 18446744073709551616; }",
          "integer literal exceeds 64 bits", "18446744073709551616" },
        { "fn f(p: *i32) -> i32 { return *p; }",
          "raw pointer dereference requires unsafe block", "*p" },
        { "fn f() { let n = 42; let v = slice(n); }",
          "slice requires a fixed array", "n" },
        { "fn f() { let v = slice([20, 22]); }",
          "slice requires a named local array", "[20, 22]" },
        { "fn f() { let v = slice(); }",
          "slice expects one local array", "slice()" },
        { "fn f() { let v = slice(1, 2); }",
          "slice expects one local array", "slice(1, 2)" },
        { "fn f() { let v = slice(missing); }",
          "undefined name", "missing" },
        { "fn f() { let data = [20, 22];"
          " let v = slice(data); v[0] = 1; }",
          "cannot write through const slice", "v[0]" },
        { "fn f() { var data = [20, 22];"
          " let v: []const i32 = slice(data); }",
          "type mismatch", "slice(data)" },
        { "fn f() { var data = [20, 22];"
          " let v = slice(data); v[true] = 1; }",
          "index must be an integer", "true" },
        { "fn f() { var data = [true];"
          " let v = slice(data); v[0] += true; }",
          "compound assignment requires a numeric variable", "v[0]" },
        { "fn f() { let a = [1, 2]; let v = a[0..=1]; }",
          "slice subranges require exclusive .. bounds", "0..=1" },
        { "fn f() { let v = [1, 2][0..1]; }",
          "array subrange requires a named local array", "[1, 2]" },
        { "fn f() { let a = [1, 2]; let v = a[true..1]; }",
          "type mismatch", "true" },
        { "fn f() { let a = [1, 2]; let v = a[0..false]; }",
          "type mismatch", "false" },
        { "fn f() { let a = [1, 2]; let v: []i32 = a[0..1]; }",
          "type mismatch", "a[0..1]" },
        { "fn f() { let a = [1, 2]; let v = a[0..1];"
          " v[0] = 3; }",
          "cannot write through const slice", "v[0]" },
        { "fn f() { var a = [1, 2]; let v = a[0..1];"
          " let wrong: i32 = v; }",
          "type mismatch", "v" },
        { "fn f() { var n = 1; let p = &n; *p = 3; }",
          "raw pointer write requires unsafe block", "*p" },
        { "fn f() { var n = 1; let p = &n; let x = *p; }",
          "raw pointer dereference requires unsafe block", "*p" },
        { "fn f() { let n = 1; let p = &n; unsafe { *p = 2; } }",
          "cannot write through const raw pointer", "*p" },
        { "fn f() { var n = 1; let p: *const i32 = &n; }",
          "type mismatch", "&n" },
        { "fn f() { let p = &1; }",
          "address-of currently requires a named local", "1" },
        { "fn f() { let p = &missing; }",
          "undefined name", "missing" },
        { "fn f() { unsafe { let n = 1; let p = &n;"
          " *p = 4; } }",
          "cannot write through const raw pointer", "*p" },
        { "fn f() { unsafe { let n = 1; let p = &n;"
          " *p += 1; } }",
          "cannot write through const raw pointer", "*p" },
        { "fn f() { unsafe { let n = 1; let p = &n;"
          " let wrong = *1; } }",
          "dereference requires raw pointer", "*1" },
        { "fn f() { unsafe { var n: bool = true; let p = &n;"
          " *p += false; } }",
          "compound pointer assignment requires numeric pointee", "*p" },
        { "fn f() { break; }", "break or continue outside a loop", "break;" },
        { "fn f() { continue; }",
          "break or continue outside a loop", "continue;" },
        { "fn f(x: [n]i32) {}",
          "array length must be a nonnegative constant integer", "n" },
        { "fn f(x: [1 / 0]i32) {}",
          "array length must be a nonnegative constant integer", "1 / 0" },
        { "fn f(x: [1 - 2]i32) {}",
          "array length must be a nonnegative constant integer", "1 - 2" },
        { "fn f(a: []i32) -> i32 { return a[true]; }",
          "index must be an integer", "true" },
        { "fn f(x: (i32, i32)) -> i32 { return x.2; }",
          "invalid tuple element index", "2" },
        { "fn f(x: i32) -> usize { return x.len; }",
          "unknown member", "len" },
        { "fn f() { let x = some(1); var y = 2; x = y; }",
          "cannot assign to immutable binding", "x" },
        { "fn f() { var x: bool = true; x += 1; }",
          "compound assignment requires a numeric variable", "x" },
        { "fn f(a: [3]u8, b: [4]u8) { let x: [3]u8 = b; }",
          "type mismatch", "b" },
        { "fn f(a: *u8) { let b: *const u8 = a; }",
          "type mismatch", "a" },
        { "fn f(a: []u8) { let b: []const u8 = a; }",
          "type mismatch", "a" },
        { "fn f() { for i in 1 { return; } }",
          "for expects an integer range", "1" },
        { "fn f() { for i in 0..4 { i += 1; } }",
          "cannot assign to immutable binding", "i" },
        { "fn f() { var n: bool = true; n &= false; }",
          "compound assignment requires a numeric variable", "n" },
        { "fn f() { let n: i32 = ~true; }",
          "type mismatch", "true" },
        { "fn f() { let a = []; }",
          "empty array requires a type context", "[]" },
        { "fn f() { let a: [3]i32 = [1, 2]; }",
          "array literal length mismatch", "[1, 2]" },
        { "fn f() { let a = [1, true]; }",
          "type mismatch", "true" },
        { "fn f() { let a: [2]i32 = [1,2]; a[0] = 3; }",
          "cannot assign to immutable binding", "a[0]" },
        { "fn f() { var a: [2]bool = [true,false]; a[0] += true; }",
          "compound assignment requires a numeric variable", "a[0]" },
        { "fn f() { var a: [2]i32 = [1,2]; a[true] = 3; }",
          "index must be an integer", "true" },
        { "fn f() { var a: [2]i32 = [1,2]; a[0] = true; }",
          "type mismatch", "true" },
        { "struct P { x: i32 } struct P { y: i32 }",
          "duplicate struct name", "P" },
        { "struct i32 {}",
          "struct name conflicts with builtin", "i32" },
        { "struct P { x: i32, x: i32 }",
          "duplicate struct field", "x" },
        { "struct P { x: void }",
          "struct field cannot have void type", "void" },
        { "struct P { child: ?P }",
          "recursive struct value type", "P" },
        { "struct P { x: Missing }",
          "unknown type", "Missing" },
        { "struct P { x: i32 } fn f() { let p = P { z: 2 }; }",
          "unknown struct field", "z" },
        { "struct P { x: i32, y: i32 }"
          " fn f() { let p = P { x: 1 }; }",
          "missing struct initializer field", NULL },
        { "struct P { x: i32 } fn f() { let p = P { x: 1, x: 2 }; }",
          "duplicate struct initializer field", NULL },
        { "struct P { x: i32 } fn f() { let p = P { x: true }; }",
          "type mismatch", "true" },
        { "struct P { x: i32 } fn f() { let p = P { x: 1 };"
          " p.x = 2; }",
          "cannot assign to immutable binding", "p.x" },
        { "struct P { ok: bool } fn f() { var p = P { ok: true };"
          " p.ok += true; }",
          "compound assignment requires a numeric variable", "p.ok" },
        { "struct P { x: i32 } fn f() -> i32 { let p = P { x: 1 };"
          " return p.other; }",
          "unknown struct field", "other" },
        { "struct X {} fn X() {}",
          "function name conflicts with struct", "X" },
        { "struct A { x: i32 } struct B { x: i32 }"
          " fn f() -> A { return B { x: 1 }; }",
          "struct literal type mismatch", NULL },
        { "fn f() { let p = Unknown {}; }",
          "unknown struct type", "Unknown" },
        { "fn f(x: void) {}", "parameter cannot have void type", "void" },
        { "fn f() { let x: void = (); }",
          "type mismatch: expected a tuple", "()" },
        { "fn f() { let x = f; }",
          "function values are not yet supported", "f" },
        { "fn call() -> i32 { return 1; }"
          " fn f() -> i32 { let call = 2; return call(); }",
          "local variable cannot be called as a function", "call" },
        { "fn f() { let a: bool = false; let x = a + 1; }",
          "type mismatch", "1" },
        { "fn f() { let x: i32 = 2; let y: bool = x; }",
          "type mismatch", "x" },
        { "fn f() -> bool { return 2147483648 == 1; }",
          "integer literal does not fit i32", "2147483648 == 1" },
        { "fn f(a: []i32) -> i32 { return a[2147483648]; }",
          "integer literal does not fit i32", "2147483648" },
        { "fn f(x: f64) { var y: f64 = x; y %= x; }",
          "compound operator requires an integer variable", "y" }
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        invalid(cases[i].source, cases[i].message, cases[i].at);
}

static void test_api_errors(void)
{
    CcArena arena;
    CcSemanticError error;

    cc_arena_init(&arena);
    assert(!cc_semantic_check(NULL, NULL, &arena, &error));
    assert(strcmp(error.message, "invalid semantic analyzer input") == 0);
    assert(!cc_semantic_check(NULL, NULL, NULL, &error));
    assert(!cc_semantic_check(NULL, NULL, &arena, NULL));
    cc_arena_destroy(&arena);
}

int main(void)
{
    test_valid_programs();
    test_invalid_programs();
    test_api_errors();

    puts("semantic: all tests passed");
    return 0;
}
