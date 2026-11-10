#include "module/link.h"

#include "ast/ast.h"
#include "lexer/lexer.h"
#include "parser/parser.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Module Module;
typedef struct Import Import;
typedef struct Edit Edit;
typedef struct Binding Binding;

struct Import {
    char *alias;
    Module *target;
    CcSourceSpan span;
    Import *next;
};

struct Edit {
    uint32_t start, end;
    const char *replacement;
    Edit *next;
};

struct Module {
    char *name;
    CcFileId file_id;
    const CcFunctionDeclaration *program;
    const CcImportDeclaration *imports;
    uint32_t header_end;
    unsigned int state; /* visiting = 1, finished = 2 */
    unsigned int id;
    Import *bindings;
    Edit *edits;
    Module *next;
};

struct Binding {
    CcSourceSpan name;
    Binding *next;
};

typedef struct {
    CcSourceManager *sources;
    CcArena *arena;
    CcModuleError *error;
    char *root_dir;
    Module *modules;
    Module *last;
    Module *entry;
    unsigned int module_count;
    bool failed;
    CcFileId root_id;
    const char *root_name;
} Linker;

typedef struct {
    Linker *linker;
    Module *module;
    Binding *locals;
} Visitor;

typedef struct {
    char *data;
    size_t count;
    size_t capacity;
    CcModuleOffset *first;
    CcModuleOffset *last;
} Output;

static void *allocate(Linker *ctx, size_t size, CcSourceSpan span)
{
    void *value = cc_arena_alloc(ctx->arena, size);
    if (value == NULL && !ctx->failed) {
        ctx->failed = true;
        ctx->error->message = "out of memory";
        ctx->error->stage = "module";
        ctx->error->span = span;
    }
    return value;
}

static void failure(Linker *ctx, CcSourceSpan span, const char *message)
{
    if (ctx->failed) return;
    ctx->failed = true;
    ctx->error->span = span;
    ctx->error->stage = "module";
    ctx->error->message = message;
}

static CcSourceSpan invalid_span(void)
{
    return (CcSourceSpan){CC_FILE_ID_INVALID, 0, 0};
}

static const CcSourceFile *source(Linker *ctx, CcFileId id)
{
    return cc_source_get(ctx->sources, id);
}

static char *copy_text(Linker *ctx, CcSourceSpan span, bool compact)
{
    const CcSourceFile *file = source(ctx, span.file_id);
    if (file == NULL || span.start > span.end || span.end > file->length) {
        failure(ctx, span, "invalid module source span");
        return NULL;
    }
    size_t len = span.end - span.start;
    char *text = allocate(ctx, len + 1, span);
    if (text == NULL) return NULL;
    size_t pos = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = file->bytes[span.start + i];
        if (compact && isspace(c)) continue;
        text[pos++] = (char)c;
    }
    text[pos] = '\0';
    return text;
}

static bool span_equals(Linker *ctx, CcSourceSpan span, const char *text)
{
    const CcSourceFile *file = source(ctx, span.file_id);
    size_t len = strlen(text);
    return file != NULL && span.start <= span.end &&
           span.end <= file->length && span.end - span.start == len &&
           memcmp(file->bytes + span.start, text, len) == 0;
}

static bool same_span(Linker *ctx, CcSourceSpan a, CcSourceSpan b)
{
    if (a.start > a.end || b.start > b.end) return false;
    size_t len = a.end - a.start;
    const CcSourceFile *first = source(ctx, a.file_id);
    const CcSourceFile *second = source(ctx, b.file_id);
    return first && second && a.end <= first->length &&
           b.end <= second->length && b.end - b.start == len &&
           memcmp(first->bytes + a.start, second->bytes + b.start, len) == 0;
}

static bool name_of(const Module *module, const char *name)
{
    return strcmp(module->name, name) == 0;
}

static Module *find_module(Linker *ctx, const char *name)
{
    for (Module *module = ctx->modules; module != NULL;
         module = module->next)
        if (name_of(module, name)) return module;
    return NULL;
}

static const CcFunctionDeclaration *find_symbol(Linker *ctx,
                                                const Module *module,
                                                const char *name)
{
    for (const CcFunctionDeclaration *decl = module->program;
         decl != NULL; decl = decl->next)
        if (span_equals(ctx, decl->name, name)) return decl;
    return NULL;
}

static bool reserved_identifier(Linker *ctx, CcSourceSpan name)
{
    const CcSourceFile *file = source(ctx, name.file_id);
    if (file == NULL || name.end > file->length ||
        name.end - name.start < 6) return false;
    const unsigned char *text = file->bytes + name.start;
    if (memcmp(text, "cc_m", 4) != 0) return false;
    size_t offset = 4;
    while (name.start + offset < name.end &&
           text[offset] >= '0' && text[offset] <= '9')
        offset++;
    return offset > 4 && name.start + offset < name.end &&
           text[offset] == '_';
}

static bool check_identifiers(Linker *ctx, Module *module)
{
    const CcSourceFile *file = source(ctx, module->file_id);
    CcLexer lexer;
    cc_lexer_init(&lexer, file, module->file_id);
    for (;;) {
        CcToken token = cc_lexer_next(&lexer);
        if (token.kind == CC_TOKEN_EOF) break;
        if (token.kind == CC_TOKEN_IDENTIFIER &&
            reserved_identifier(ctx, token.span)) {
            failure(ctx, token.span,
                    "reserved internal module identifier prefix");
            return false;
        }
    }
    static const char *const builtins[] = {
        "void", "bool", "i8", "i16", "i32", "i64", "isize",
        "u8", "u16", "u32", "u64", "usize", "f32", "f64"
    };
    for (const CcFunctionDeclaration *decl = module->program;
         decl != NULL; decl = decl->next) {
        if (decl->struct_decl == NULL && decl->enum_decl == NULL) continue;
        for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++)
            if (span_equals(ctx, decl->name, builtins[i])) {
                failure(ctx, decl->name,
                        decl->struct_decl != NULL
                          ? "struct name conflicts with builtin"
                          : "enum name conflicts with builtin");
                return false;
            }
    }
    return true;
}

static char *mangle(Linker *ctx, const Module *module,
                    const CcFunctionDeclaration *decl)
{
    if (module == ctx->entry && decl->struct_decl == NULL &&
        decl->enum_decl == NULL &&
        span_equals(ctx, decl->name, "main")) {
        char *main_name = allocate(ctx, 5, decl->name);
        if (main_name != NULL) memcpy(main_name, "main", 5);
        return main_name;
    }
    char *name = copy_text(ctx, decl->name, false);
    if (name == NULL) return NULL;
    size_t len = strlen(name);
    if (len > SIZE_MAX - 32) {
        failure(ctx, decl->name, "module identifier too long");
        return NULL;
    }
    char *result = allocate(ctx, len + 32, decl->name);
    if (result != NULL)
        snprintf(result, len + 32, "cc_m%u_%s", module->id, name);
    return result;
}

static bool add_edit(Linker *ctx, Module *module,
                     CcSourceSpan span, const char *replacement)
{
    if (ctx->failed) return false;
    if (span.file_id != module->file_id ||
        span.start < module->header_end ||
        span.end > source(ctx, module->file_id)->length ||
        span.start >= span.end) {
        failure(ctx, span, "invalid module rewrite span");
        return false;
    }
    Edit **ptr = &module->edits;
    Edit *previous = NULL;
    while (*ptr != NULL && (*ptr)->start < span.start) {
        previous = *ptr;
        ptr = &(*ptr)->next;
    }
    if ((*ptr != NULL && span.end > (*ptr)->start) ||
        (previous != NULL && previous->end > span.start)) {
        failure(ctx, span, "overlapping module symbol rewrites");
        return false;
    }
    Edit *entry = allocate(ctx, sizeof(*entry), span);
    if (entry == NULL) return false;
    entry->start = span.start;
    entry->end = span.end;
    entry->replacement = replacement;
    entry->next = *ptr;
    *ptr = entry;
    return true;
}

/* The linker uses CcSourceSpan file IDs and never compares unrelated files
 * by offset. The emitted synthetic source is reparsed for semantic/IR work. */

static char *module_path(Linker *ctx, const char *module)
{
    size_t prefix = strlen(ctx->root_dir);
    size_t count = strlen(module);
    if (prefix > SIZE_MAX - count - 9) {
        failure(ctx, invalid_span(), "module path too long");
        return NULL;
    }
    char *path = allocate(ctx, prefix + count + 9, invalid_span());
    if (path == NULL) return NULL;
    memcpy(path, ctx->root_dir, prefix);
    size_t k = prefix;
    if (k != 0 && path[k - 1] != '/') path[k++] = '/';
    for (size_t i = 0; i < count; i++)
        path[k++] = module[i] == '.' ? '/' : module[i];
    memcpy(path + k, ".caret", 7);
    return path;
}

static bool set_root_directory(Linker *ctx, const char *entry,
                               const char *name, CcSourceSpan span)
{
    char *suffix = module_path(ctx, name);
    if (suffix == NULL) return false;
    /* module_path uses an empty root when inferring the root directory. */
    if (suffix[0] == '/') {
        failure(ctx, span, "invalid entry module path");
        return false;
    }
    size_t length = strlen(entry);
    size_t suffix_len = strlen(suffix);
    if (length < suffix_len ||
        strcmp(entry + length - suffix_len, suffix) != 0 ||
        (length > suffix_len && entry[length - suffix_len - 1] != '/')) {
        failure(ctx, span,
                "entry filename must match module path (<root>/a/b.caret)");
        return false;
    }
    size_t root_len = length - suffix_len;
    while (root_len != 0 && entry[root_len - 1] == '/') root_len--;
    ctx->root_dir = allocate(ctx, root_len + 1, span);
    if (ctx->root_dir == NULL) return false;
    memcpy(ctx->root_dir, entry, root_len);
    ctx->root_dir[root_len] = '\0';
    return true;
}

static Module *load_module(Linker *ctx, const char *name,
                           CcSourceSpan referenced)
{
    Module *existing = find_module(ctx, name);
    if (existing != NULL) {
        if (existing->state == 1)
            failure(ctx, referenced, "cyclic module import");
        return existing;
    }
    if (ctx->module_count >= 256) {
        failure(ctx, referenced, "too many imported modules");
        return NULL;
    }
    char *path = module_path(ctx, name);
    if (path == NULL) return NULL;
    CcFileId id = CC_FILE_ID_INVALID;
    if (ctx->root_name != NULL && strcmp(name, ctx->root_name) == 0) {
        id = ctx->root_id;
    } else {
        CcSourceError source_error;
        if (!cc_source_add_file(ctx->sources, path, &id, &source_error)) {
            failure(ctx, referenced, "cannot load imported module source");
            return NULL;
        }
    }
    const CcSourceFile *file = source(ctx, id);
    CcLexer lexer;
    CcParser parser;
    cc_lexer_init(&lexer, file, id);
    cc_parser_init(&parser, &lexer, ctx->arena);
    CcFunctionDeclaration *program = cc_parser_parse_program(&parser);
    if (parser.failed) {
        bool lexical = parser.current.kind == CC_TOKEN_INVALID &&
                       lexer.error != CC_LEX_ERROR_NONE;
        ctx->error->stage = lexical ? "lexer" : "parser";
        ctx->error->span = lexical ? parser.current.span : parser.error_span;
        ctx->error->message = lexical
            ? cc_lex_error_message(lexer.error) : parser.error;
        ctx->failed = true;
        return NULL;
    }
    if (!parser.has_module) {
        failure(ctx, referenced, "imported file lacks module declaration");
        return NULL;
    }
    char *actual = copy_text(ctx, parser.module_path, true);
    if (actual == NULL) return NULL;
    if (strcmp(actual, name) != 0) {
        failure(ctx, parser.module_path, "imported module name mismatch");
        return NULL;
    }
    Module *module = allocate(ctx, sizeof(*module), parser.module_path);
    if (module == NULL) return NULL;
    memset(module, 0, sizeof(*module));
    module->name = actual;
    module->file_id = id;
    module->header_end = parser.header_end;
    module->imports = parser.imports;
    module->program = program;
    module->state = 1;
    module->id = ctx->module_count++;
    if (ctx->last != NULL) ctx->last->next = module;
    else ctx->modules = module;
    ctx->last = module;
    for (const CcImportDeclaration *item = module->imports; item != NULL;
         item = item->next) {
        char *alias = copy_text(ctx, item->alias, false);
        char *target_name = copy_text(ctx, item->path, true);
        if (alias == NULL || target_name == NULL) return NULL;
        for (Import *other = module->bindings; other != NULL;
             other = other->next)
            if (strcmp(other->alias, alias) == 0) {
                failure(ctx, item->alias, "duplicate module import alias");
                return NULL;
            }
        if (find_symbol(ctx, module, alias) != NULL) {
            failure(ctx, item->alias,
                    "import alias conflicts with local declaration");
            return NULL;
        }
        Import *binding = allocate(ctx, sizeof(*binding), item->alias);
        if (binding == NULL) return NULL;
        binding->alias = alias;
        binding->span = item->path;
        binding->target = load_module(ctx, target_name, item->path);
        if (ctx->failed || binding->target == NULL) return NULL;
        binding->next = module->bindings;
        module->bindings = binding;
    }
    for (const CcFunctionDeclaration *node = module->program;
         node != NULL; node = node->next)
        for (const CcFunctionDeclaration *previous = module->program;
             previous != node; previous = previous->next)
            if (same_span(ctx, node->name, previous->name)) {
                failure(ctx, node->name,
                        "duplicate top-level module declaration");
                return NULL;
            }
    module->state = 2;
    return module;
}

static Import *find_import(const Module *module, const char *alias)
{
    for (Import *binding = module->bindings; binding != NULL;
         binding = binding->next)
        if (strcmp(binding->alias, alias) == 0) return binding;
    return NULL;
}

static char *rewrite_name(Visitor *visitor, CcSourceSpan span,
                          bool expect_struct, bool expect_function)
{
    Linker *ctx = visitor->linker;
    char *name = copy_text(ctx, span, true);
    if (name == NULL) return NULL;
    char *dot = strchr(name, '.');
    Module *target = visitor->module;
    if (dot != NULL) {
        if (strchr(dot + 1, '.') != NULL) {
            failure(ctx, span, "only one import qualifier is supported");
            return NULL;
        }
        *dot = '\0';
        Import *import = find_import(visitor->module, name);
        if (import == NULL) {
            failure(ctx, span, "unknown module import alias");
            return NULL;
        }
        target = import->target;
        name = dot + 1;
    }
    const CcFunctionDeclaration *decl = find_symbol(ctx, target, name);
    if (decl == NULL) {
        if (dot != NULL)
            failure(ctx, span, "unknown imported declaration");
        return NULL;
    }
    if (dot != NULL && !decl->is_public) {
        failure(ctx, span, "imported declaration is private");
        return NULL;
    }
    if ((expect_struct && decl->struct_decl == NULL &&
         decl->enum_decl == NULL) ||
        (expect_function &&
         (decl->struct_decl != NULL || decl->enum_decl != NULL))) {
        failure(ctx, span, "imported declaration kind mismatch");
        return NULL;
    }
    if (dot != NULL && decl->struct_decl != NULL) {
        for (const CcStructField *field = decl->struct_decl->fields;
             field != NULL; field = field->next)
            if (!field->is_public) {
                failure(ctx, span,
                        "imported struct requires public fields");
                return NULL;
            }
    }
    return mangle(ctx, target, decl);
}

static bool is_local(Visitor *visitor, CcSourceSpan name)
{
    for (Binding *bind = visitor->locals; bind != NULL; bind = bind->next)
        if (same_span(visitor->linker, bind->name, name)) return true;
    return false;
}

static bool add_local(Visitor *visitor, CcSourceSpan name)
{
    Binding *binding = allocate(visitor->linker, sizeof(*binding), name);
    if (binding == NULL) return false;
    binding->name = name;
    binding->next = visitor->locals;
    visitor->locals = binding;
    return true;
}

static bool walk_type(Visitor *visitor, const CcType *type);
static bool walk_expr(Visitor *visitor, const CcExpression *expr);
static bool walk_block(Visitor *visitor, const CcBlock *block);

static bool walk_type(Visitor *visitor, const CcType *type)
{
    if (type == NULL) return true;
    switch (type->kind) {
    case CC_AST_TYPE_NAMED: {
        static const char *const primitives[] = {
            "void", "bool", "i8", "i16", "i32", "i64", "isize",
            "u8", "u16", "u32", "u64", "usize", "f32", "f64"
        };
        for (size_t i = 0; i < sizeof(primitives)/sizeof(primitives[0]); i++)
            if (span_equals(visitor->linker, type->span, primitives[i]))
                return true;
        char *name = rewrite_name(visitor, type->span, true, false);
        if (visitor->linker->failed) return false;
        return name == NULL ||
               add_edit(visitor->linker, visitor->module, type->span, name);
    }
    case CC_AST_TYPE_PARENTHESIZED:
        return walk_type(visitor, type->parenthesized_inner);
    case CC_AST_TYPE_OPTIONAL:
        return walk_type(visitor, type->optional_inner);
    case CC_AST_TYPE_POINTER:
        return walk_type(visitor, type->pointer_inner);
    case CC_AST_TYPE_SLICE:
        return walk_type(visitor, type->slice_inner);
    case CC_AST_TYPE_ARRAY:
        return walk_expr(visitor, type->array.length) &&
               walk_type(visitor, type->array.element);
    case CC_AST_TYPE_TUPLE:
        for (const CcTupleTypeElement *item = type->tuple_elements;
             item != NULL; item = item->next)
            if (!walk_type(visitor, item->type)) return false;
        return true;
    }
    return true;
}

/* In source syntax, a constructor is Enum.Variant or import.Enum.Variant.
 * Rewrite just the enum type qualifier; the variant name stays unmangled.
 * Check lexical locals first, so local x.field is not interpreted as enum. */
static bool rewrite_enum_base(Visitor *visitor, const CcExpression *member,
                              bool *handled)
{
    *handled = false;
    if (member->kind != CC_AST_EXPRESSION_MEMBER) return true;
    const CcExpression *owner = member->member.operand;
    CcSourceSpan name;
    if (owner->kind == CC_AST_EXPRESSION_IDENTIFIER) {
        if (is_local(visitor, owner->identifier_name)) return true;
        name = owner->identifier_name;
    } else if (owner->kind == CC_AST_EXPRESSION_MEMBER &&
               owner->member.operand->kind == CC_AST_EXPRESSION_IDENTIFIER) {
        if (is_local(visitor,
                     owner->member.operand->identifier_name)) return true;
        char *alias = copy_text(visitor->linker,
            owner->member.operand->identifier_name, false);
        if (alias == NULL) return false;
        if (find_import(visitor->module, alias) == NULL) return true;
        name = owner->span;
    } else return true;

    char *written = rewrite_name(visitor, name, true, false);
    if (visitor->linker->failed) return false;
    if (written == NULL) return true;

    /* Only rewrite enum constructors, not fields of a struct type. */
    char *source_name = copy_text(visitor->linker, name, true);
    if (source_name == NULL) return false;
    char *dot = strchr(source_name, '.');
    Module *module = visitor->module;
    if (dot != NULL) {
        *dot = '\0';
        Import *import = find_import(module, source_name);
        if (import == NULL) return true;
        module = import->target;
        source_name = dot + 1;
    }
    const CcFunctionDeclaration *decl =
        find_symbol(visitor->linker, module, source_name);
    if (decl == NULL || decl->enum_decl == NULL) return true;
    if (!add_edit(visitor->linker, visitor->module, name, written))
        return false;
    *handled = true;
    return true;
}

static bool walk_expr(Visitor *visitor, const CcExpression *expr)
{
    if (expr == NULL) return true;
    switch (expr->kind) {
    case CC_AST_EXPRESSION_SOME:
        return walk_expr(visitor, expr->some_value);
    case CC_AST_EXPRESSION_PARENTHESIZED:
        return walk_expr(visitor, expr->parenthesized_inner);
    case CC_AST_EXPRESSION_TUPLE:
    case CC_AST_EXPRESSION_ARRAY:
        for (const CcTupleExpressionElement *part =
                 expr->kind == CC_AST_EXPRESSION_ARRAY
                     ? expr->array_elements : expr->tuple_elements;
             part != NULL; part = part->next)
            if (!walk_expr(visitor, part->value)) return false;
        return true;
    case CC_AST_EXPRESSION_STRUCT: {
        char *name = rewrite_name(visitor, expr->struct_literal.name,
                                  true, false);
        if (visitor->linker->failed) return false;
        if (name != NULL &&
            !add_edit(visitor->linker, visitor->module,
                      expr->struct_literal.name, name))
            return false;
        for (const CcStructFieldValue *part = expr->struct_literal.fields;
             part != NULL; part = part->next)
            if (!walk_expr(visitor, part->value)) return false;
        return true;
    }
    case CC_AST_EXPRESSION_UNARY:
        return walk_expr(visitor, expr->unary.operand);
    case CC_AST_EXPRESSION_BINARY:
        return walk_expr(visitor, expr->binary.left) &&
               walk_expr(visitor, expr->binary.right);
    case CC_AST_EXPRESSION_CALL: {
        const CcExpression *callee = expr->call.callee;
        bool handled = false;
        if (callee->kind == CC_AST_EXPRESSION_MEMBER &&
            !rewrite_enum_base(visitor, callee, &handled)) return false;
        if (callee->kind == CC_AST_EXPRESSION_IDENTIFIER &&
            !is_local(visitor, callee->identifier_name)) {
            char *name = rewrite_name(visitor, callee->identifier_name,
                                      false, true);
            if (visitor->linker->failed) return false;
            if (name != NULL) {
                if (!add_edit(visitor->linker, visitor->module,
                              callee->identifier_name, name))
                    return false;
                handled = true;
            }
        } else if (!handled && callee->kind == CC_AST_EXPRESSION_MEMBER &&
                   callee->member.operand->kind ==
                       CC_AST_EXPRESSION_IDENTIFIER &&
                   !is_local(visitor,
                             callee->member.operand->identifier_name)) {
            char *alias = copy_text(visitor->linker,
                    callee->member.operand->identifier_name, false);
            if (alias == NULL) return false;
            if (find_import(visitor->module, alias) != NULL) {
                char *name = rewrite_name(visitor, callee->span,
                                          false, true);
                if (visitor->linker->failed || name == NULL ||
                    !add_edit(visitor->linker, visitor->module,
                              callee->span, name))
                    return false;
                handled = true;
            }
        }
        if (!handled && !walk_expr(visitor, callee)) return false;
        for (const CcCallArgument *arg = expr->call.arguments; arg != NULL;
             arg = arg->next)
            if (!walk_expr(visitor, arg->value)) return false;
        return true;
    }
    case CC_AST_EXPRESSION_INDEX:
        return walk_expr(visitor, expr->index.operand) &&
               walk_expr(visitor, expr->index.index);
    case CC_AST_EXPRESSION_MEMBER:
    {
        bool handled = false;
        if (!rewrite_enum_base(visitor, expr, &handled)) return false;
        return handled || walk_expr(visitor, expr->member.operand);
    }
    case CC_AST_EXPRESSION_TUPLE_MEMBER:
        return walk_expr(visitor, expr->tuple_member.operand);
    default: return true;
    }
}

static bool bind_pattern(Visitor *visitor, const CcPattern *pattern)
{
    if (pattern == NULL) return true;
    switch (pattern->kind) {
    case CC_AST_PATTERN_NAME:
        return add_local(visitor, pattern->name);
    case CC_AST_PATTERN_PARENTHESIZED:
        return bind_pattern(visitor, pattern->parenthesized_inner);
    case CC_AST_PATTERN_TUPLE:
        for (const CcTuplePatternElement *part = pattern->tuple_elements;
             part != NULL; part = part->next)
            if (!bind_pattern(visitor, part->pattern)) return false;
        return true;
    }
    return true;
}

static bool walk_if(Visitor *visitor, const CcIf *branch)
{
    if (!walk_expr(visitor, branch->condition) ||
        !walk_block(visitor, branch->then_block))
        return false;
    if (branch->else_if != NULL && !walk_if(visitor, branch->else_if))
        return false;
    return branch->else_block == NULL ||
           walk_block(visitor, branch->else_block);
}

static bool walk_block(Visitor *visitor, const CcBlock *block)
{
    Binding *saved = visitor->locals;
    for (const CcStatement *stmt = block->first; stmt != NULL;
         stmt = stmt->next) {
        switch (stmt->kind) {
        case CC_AST_STATEMENT_LET:
            if (!walk_type(visitor, stmt->let.type) ||
                !walk_expr(visitor, stmt->let.initializer) ||
                !bind_pattern(visitor, stmt->let.pattern)) goto failed;
            break;
        case CC_AST_STATEMENT_RETURN:
            if (!walk_expr(visitor, stmt->return_expression)) goto failed;
            break;
        case CC_AST_STATEMENT_EXPRESSION:
            if (!walk_expr(visitor, stmt->expression)) goto failed;
            break;
        case CC_AST_STATEMENT_ASSIGNMENT:
            if (!walk_expr(visitor, stmt->assignment.target) ||
                !walk_expr(visitor, stmt->assignment.value)) goto failed;
            break;
        case CC_AST_STATEMENT_IF:
            if (!walk_if(visitor, stmt->if_statement)) goto failed;
            break;
        case CC_AST_STATEMENT_MATCH: {
            const CcMatch *match = stmt->match_statement;
            if (!walk_expr(visitor, match->scrutinee)) goto failed;
            for (const CcMatchArm *arm = match->arms; arm != NULL;
                 arm = arm->next) {
                Binding *before = visitor->locals;
                if (arm->enum_type.file_id != CC_FILE_ID_INVALID) {
                    char *name = rewrite_name(visitor, arm->enum_type, true,
                                              false);
                    if (visitor->linker->failed ||
                        (name != NULL &&
                         !add_edit(visitor->linker, visitor->module,
                                   arm->enum_type, name))) goto failed;
                }
                if ((arm->payload != NULL &&
                     !bind_pattern(visitor, arm->payload)) ||
                    !walk_block(visitor, arm->body)) goto failed;
                visitor->locals = before;
            }
            break;
        }
        case CC_AST_STATEMENT_WHILE:
            if (!walk_expr(visitor, stmt->while_statement->condition) ||
                !walk_block(visitor, stmt->while_statement->body))
                goto failed;
            break;
        case CC_AST_STATEMENT_LOOP:
            if (!walk_block(visitor, stmt->loop_statement->body))
                goto failed;
            break;
        case CC_AST_STATEMENT_FOR: {
            if (!walk_expr(visitor, stmt->for_statement->range)) goto failed;
            Binding *before = visitor->locals;
            if (!add_local(visitor, stmt->for_statement->name) ||
                !walk_block(visitor, stmt->for_statement->body))
                goto failed;
            visitor->locals = before;
            break;
        }
        case CC_AST_STATEMENT_UNSAFE:
            if (!walk_block(visitor, stmt->unsafe_block)) goto failed;
            break;
        default: break;
        }
    }
    visitor->locals = saved;
    return true;
failed:
    visitor->locals = saved;
    return false;
}

static bool prepare_module(Linker *ctx, Module *module)
{
    Visitor visitor = {ctx, module, NULL};
    for (const CcFunctionDeclaration *decl = module->program;
         decl != NULL; decl = decl->next) {
        char *name = mangle(ctx, module, decl);
        if (name == NULL ||
            !add_edit(ctx, module, decl->name, name)) return false;
        if (decl->struct_decl != NULL) {
            for (const CcStructField *field = decl->struct_decl->fields;
                 field != NULL; field = field->next)
                if (!walk_type(&visitor, field->type)) return false;
        } else if (decl->enum_decl != NULL) {
            for (const CcEnumVariant *variant = decl->enum_decl->variants;
                 variant != NULL; variant = variant->next)
                if (!walk_type(&visitor, variant->payload)) return false;
        } else {
            visitor.locals = NULL;
            for (const CcParameter *param = decl->parameters;
                 param != NULL; param = param->next) {
                if (!walk_type(&visitor, param->type) ||
                    !add_local(&visitor, param->name)) return false;
            }
            if (!walk_type(&visitor, decl->return_type) ||
                !walk_block(&visitor, decl->body)) return false;
        }
    }
    return true;
}

static bool check_export_type(Linker *ctx, Module *module,
                              const CcType *type)
{
    if (type == NULL) return true;
    switch (type->kind) {
    case CC_AST_TYPE_NAMED: {
        char *name = copy_text(ctx, type->span, true);
        if (name == NULL) return false;
        char *dot = strchr(name, '.');
        Module *target = module;
        if (dot != NULL) {
            *dot = '\0';
            Import *binding = find_import(module, name);
            if (binding == NULL) return true; /* Semantic pass reports it. */
            target = binding->target;
            name = dot + 1;
        }
        const CcFunctionDeclaration *decl = find_symbol(ctx, target, name);
        if (decl != NULL && decl->enum_decl != NULL) {
            if (!decl->is_public) {
                failure(ctx, type->span,
                        "public signature exposes a private enum type");
                return false;
            }
        } else if (decl != NULL && decl->struct_decl != NULL) {
            if (!decl->is_public) {
                failure(ctx, type->span,
                        "public signature exposes a private struct type");
                return false;
            }
            for (const CcStructField *field = decl->struct_decl->fields;
                 field != NULL; field = field->next)
                if (!field->is_public) {
                    failure(ctx, type->span,
                            "public signature exposes nonexportable struct");
                    return false;
                }
        }
        return true;
    }
    case CC_AST_TYPE_OPTIONAL:
        return check_export_type(ctx, module, type->optional_inner);
    case CC_AST_TYPE_POINTER:
        return check_export_type(ctx, module, type->pointer_inner);
    case CC_AST_TYPE_SLICE:
        return check_export_type(ctx, module, type->slice_inner);
    case CC_AST_TYPE_ARRAY:
        return check_export_type(ctx, module, type->array.element);
    case CC_AST_TYPE_PARENTHESIZED:
        return check_export_type(ctx, module, type->parenthesized_inner);
    case CC_AST_TYPE_TUPLE:
        for (const CcTupleTypeElement *item = type->tuple_elements;
             item != NULL; item = item->next)
            if (!check_export_type(ctx, module, item->type)) return false;
        return true;
    }
    return true;
}

static bool check_exported_interfaces(Linker *ctx, Module *module)
{
    for (const CcFunctionDeclaration *decl = module->program;
         decl != NULL; decl = decl->next) {
        if (!decl->is_public) continue;
        if (decl->struct_decl != NULL) {
            for (const CcStructField *field = decl->struct_decl->fields;
                 field != NULL; field = field->next)
                if (field->is_public &&
                    !check_export_type(ctx, module, field->type))
                    return false;
        } else if (decl->enum_decl != NULL) {
            for (const CcEnumVariant *variant = decl->enum_decl->variants;
                 variant != NULL; variant = variant->next)
                if (!check_export_type(ctx, module, variant->payload))
                    return false;
        } else {
            if (!check_export_type(ctx, module, decl->return_type))
                return false;
            for (const CcParameter *param = decl->parameters;
                 param != NULL; param = param->next)
                if (!check_export_type(ctx, module, param->type))
                    return false;
        }
    }
    return true;
}

static bool append_output(Linker *ctx, Output *out,
                          CcFileId original_file, uint32_t original_start,
                          uint32_t original_end,
                          const char *content, size_t length)
{
    if (length == 0) return true;
    if (length > UINT32_MAX || out->count > UINT32_MAX - length ||
        out->count > SIZE_MAX - length - 1) {
        failure(ctx, invalid_span(), "linked module source too large");
        return false;
    }
    size_t wanted = out->count + length + 1;
    if (wanted > out->capacity) {
        size_t capacity = out->capacity == 0 ? 4096 : out->capacity;
        while (capacity < wanted) {
            if (capacity > SIZE_MAX / 2) {
                failure(ctx, invalid_span(), "linked module source too large");
                return false;
            }
            capacity *= 2;
        }
        char *grown = realloc(out->data, capacity);
        if (grown == NULL) {
            failure(ctx, invalid_span(), "out of memory");
            return false;
        }
        out->data = grown;
        out->capacity = capacity;
    }
    CcModuleOffset *offset = allocate(ctx, sizeof(*offset),
        (CcSourceSpan){original_file, original_start, original_end});
    if (offset == NULL) return false;
    offset->start = (uint32_t)out->count;
    offset->end = (uint32_t)(out->count + length);
    offset->original_file = original_file;
    offset->original_start = original_start;
    offset->original_end = original_end;
    offset->next = NULL;
    if (out->last == NULL) out->first = offset;
    else out->last->next = offset;
    out->last = offset;
    memcpy(out->data + out->count, content, length);
    out->count += length;
    out->data[out->count] = '\0';
    return true;
}

static bool assemble(Linker *ctx, Output *out)
{
    for (Module *module = ctx->modules; module != NULL;
         module = module->next) {
        const CcSourceFile *file = source(ctx, module->file_id);
        uint32_t cursor = module->header_end;
        for (const Edit *edit = module->edits; edit != NULL;
             edit = edit->next) {
            if (cursor > edit->start) {
                failure(ctx, (CcSourceSpan){module->file_id,
                        edit->start, edit->end},
                        "overlapping module symbol rewrites");
                return false;
            }
            if (!append_output(ctx, out, module->file_id,
                    cursor, edit->start,
                    (const char *)file->bytes + cursor,
                    edit->start - cursor)) return false;
            if (!append_output(ctx, out, module->file_id,
                    edit->start, edit->end, edit->replacement,
                    strlen(edit->replacement))) return false;
            cursor = edit->end;
        }
        if (!append_output(ctx, out, module->file_id, cursor, file->length,
                (const char *)file->bytes + cursor, file->length - cursor))
            return false;
        if (!append_output(ctx, out, module->file_id, file->length,
                file->length, "\n", 1)) return false;
    }
    return true;
}

CcSourceSpan cc_module_origin(const CcModuleLink *link, CcSourceSpan span)
{
    if (link == NULL || !link->modular ||
        span.file_id != link->linked_file) return span;
    for (const CcModuleOffset *item = link->offsets; item != NULL;
         item = item->next) {
        if (span.start < item->start || span.start >= item->end) continue;
        uint32_t start = item->original_start;
        uint32_t end = item->original_end;
        if (item->end - item->start == end - start) {
            start += span.start - item->start;
            size_t len = span.end - span.start;
            end = len > item->original_end - start
                ? item->original_end : start + (uint32_t)len;
        }
        return (CcSourceSpan){item->original_file, start, end};
    }
    return span;
}

bool cc_module_link(CcSourceManager *sources, CcArena *arena,
                    CcFileId entry, CcModuleLink *result,
                    CcModuleError *error)
{
    if (result == NULL || error == NULL) return false;
    *result = (CcModuleLink){entry, false, NULL};
    *error = (CcModuleError){invalid_span(), NULL, NULL};
    if (sources == NULL || arena == NULL ||
        cc_source_get(sources, entry) == NULL) {
        error->stage = "module";
        error->message = "invalid module linker arguments";
        return false;
    }
    Linker ctx = {0};
    ctx.sources = sources;
    ctx.arena = arena;
    ctx.error = error;
    ctx.root_id = entry;
    ctx.root_dir = "";
    const CcSourceFile *root = cc_source_get(sources, entry);
    CcLexer lexer;
    CcParser parser;
    cc_lexer_init(&lexer, root, entry);
    cc_parser_init(&parser, &lexer, arena);
    cc_parser_parse_program(&parser);
    if (parser.failed) {
        bool lexical = parser.current.kind == CC_TOKEN_INVALID &&
                       lexer.error != CC_LEX_ERROR_NONE;
        error->span = lexical ? parser.current.span : parser.error_span;
        error->stage = lexical ? "lexer" : "parser";
        error->message = lexical
            ? cc_lex_error_message(lexer.error) : parser.error;
        return false;
    }
    if (!parser.has_module) return true;
    char *name = copy_text(&ctx, parser.module_path, true);
    if (name == NULL || !set_root_directory(&ctx, root->path,
                                            name, parser.module_path))
        return false;
    ctx.root_name = name;
    ctx.entry = load_module(&ctx, name, parser.module_path);
    if (ctx.failed || ctx.entry == NULL) return false;
    for (Module *module = ctx.modules; module != NULL;
         module = module->next) {
        if (!check_identifiers(&ctx, module) ||
            !check_exported_interfaces(&ctx, module) ||
            !prepare_module(&ctx, module))
            return false;
    }
    Output out = {0};
    bool ok = assemble(&ctx, &out);
    if (ok) {
        CcFileId linked_id;
        CcSourceError source_error;
        ok = cc_source_add_memory(sources, "<linked modules>",
               (const unsigned char *)out.data, out.count,
               &linked_id, &source_error);
        if (ok) {
            result->linked_file = linked_id;
            result->modular = true;
            result->offsets = out.first;
        } else {
            failure(&ctx, parser.module_path,
                    "unable to allocate linked module source");
        }
    }
    free(out.data);
    return ok;
}
