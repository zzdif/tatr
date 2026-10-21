// Copyright (C) 2026  Alexey Kutepov <reximkut@gmail.com>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, see <https://www.gnu.org/licenses/>.
#include "query.h"

int is_bracket(int x)
{
    if (x == '[') return true;
    if (x == ']') return true;
    return false;
}

int is_not_bracket_nor_space(int x)
{
    return !is_bracket(x) && !isspace(x);
}

bool chop_next_query_token(String_View *src, String_View *token)
{
    *src = sv_trim_left(*src);
    if (src->count == 0) return false;
    if (sv_starts_with(*src, SVLIT("["))) {
        *token = sv_chop_left(src, 1);
        return true;
    }
    if (sv_starts_with(*src, SVLIT("]"))) {
        *token = sv_chop_left(src, 1);
        return true;
    }
    *token = sv_chop_while(src, is_not_bracket_nor_space);
    return token->count > 0;
}

void print_qop(QOp op)
{
    switch (op.kind) {
    case QOP_ANY:      printf("QOP_ANY\n");                             break;
    case QOP_TAG:      printf("QOP_TAG "SV_Fmt"\n", SV_Arg(op.as.tag)); break;
    case QOP_NOT:      printf("QOP_NOT\n");                             break;
    case QOP_OR:       printf("QOP_OR\n");                              break;
    case QOP_AND:      printf("QOP_AND\n");                             break;
    case QOP_TAGGED:   printf("QOP_TAGGED\n");                          break;
    case QOP_ID:       printf("QOP_ID\n");                              break;
    case QOP_PRIORITY: printf("QOP_PRIORITY\n");                        break;
    case QOP_INTEGER:  printf("QOP_INTEGER %ld\n", op.as.integer);      break;
    case QOP_LT:       printf("QOP_LT\n");                              break;
    case QOP_GT:       printf("QOP_GT\n");                              break;
    case QOP_LTE:      printf("QOP_LTE\n");                             break;
    case QOP_GTE:      printf("QOP_GTE\n");                             break;
    case QOP_EQ:       printf("QOP_EQ\n");                              break;
    case QOP_NEQ:      printf("QOP_NEQ\n");                             break;
    default: UNREACHABLE("QOp_Kind");
    }
}

bool pop_type(String_View original_src, Stack *stack, Type type, Stack_Item *item)
{
    *item = da_pop(stack);
    if (item->type != type) {
        report_compile_query_diagnostic(original_src, item->src, "ERROR: Expected %s but got %s", type_name(type), type_name(item->type));
        return false;
    }
    return true;
}

Query_Match_Result task_matches_query(String_View original_src, Task task, Query query, Stack *stack)
{
    stack->count = 0;
    da_foreach(QOp, op, &query) {
        switch (op->kind) {
        case QOP_ANY: {
            da_append(stack, stack_bool(op->src, true));
        } break;
        case QOP_TAG: {
            bool result = parsed_tags_contains(task.parsed_tags, op->as.tag);
            da_append(stack, stack_bool(op->src, result));
        } break;
        case QOP_NOT: {
            Stack_Item item = {0};
            if (!pop_type(original_src, stack, TYPE_BOOLEAN, &item)) return QUERY_ERROR;
            da_append(stack, stack_bool(op->src, !item.as.boolean));
        } break;
        case QOP_OR: {
            Stack_Item a = {0};
            Stack_Item b = {0};
            if (!pop_type(original_src, stack, TYPE_BOOLEAN, &a)) return QUERY_ERROR;
            if (!pop_type(original_src, stack, TYPE_BOOLEAN, &b)) return QUERY_ERROR;
            da_append(stack, stack_bool(op->src, a.as.boolean || b.as.boolean));
        } break;
        case QOP_AND: {
            Stack_Item a = {0};
            Stack_Item b = {0};
            if (!pop_type(original_src, stack, TYPE_BOOLEAN, &a)) return QUERY_ERROR;
            if (!pop_type(original_src, stack, TYPE_BOOLEAN, &b)) return QUERY_ERROR;
            da_append(stack, stack_bool(op->src, a.as.boolean && b.as.boolean));
        } break;
        case QOP_ID: {
            da_append(stack, stack_bool(op->src, sv_eq(sv_from_cstr(task.id), op->as.id)));
        } break;
        case QOP_TAGGED: {
            da_append(stack, stack_bool(op->src, task.parsed_tags.count > 0));
        } break;
        case QOP_PRIORITY: {
            da_append(stack, stack_int(op->src, task.priority));
        } break;
        case QOP_INTEGER: {
            da_append(stack, stack_int(op->src, op->as.integer));
        } break;
        // TASK(20260829-225753): All comparison operators could work with booleans too
        case QOP_LT: {
            Stack_Item a = {0};
            Stack_Item b = {0};
            if (!pop_type(original_src, stack, TYPE_INTEGER, &b)) return QUERY_ERROR;
            if (!pop_type(original_src, stack, TYPE_INTEGER, &a)) return QUERY_ERROR;
            da_append(stack, stack_bool(op->src, a.as.integer < b.as.integer));
        } break;
        case QOP_GT: {
            Stack_Item a = {0};
            Stack_Item b = {0};
            if (!pop_type(original_src, stack, TYPE_INTEGER, &b)) return QUERY_ERROR;
            if (!pop_type(original_src, stack, TYPE_INTEGER, &a)) return QUERY_ERROR;
            da_append(stack, stack_bool(op->src, a.as.integer > b.as.integer));
        } break;
        case QOP_LTE: {
            Stack_Item a = {0};
            Stack_Item b = {0};
            if (!pop_type(original_src, stack, TYPE_INTEGER, &b)) return QUERY_ERROR;
            if (!pop_type(original_src, stack, TYPE_INTEGER, &a)) return QUERY_ERROR;
            da_append(stack, stack_bool(op->src, a.as.integer <= b.as.integer));
        } break;
        case QOP_GTE: {
            Stack_Item a = {0};
            Stack_Item b = {0};
            if (!pop_type(original_src, stack, TYPE_INTEGER, &b)) return QUERY_ERROR;
            if (!pop_type(original_src, stack, TYPE_INTEGER, &a)) return QUERY_ERROR;
            da_append(stack, stack_bool(op->src, a.as.integer >= b.as.integer));
        } break;
        case QOP_EQ: {
            Stack_Item a = {0};
            Stack_Item b = {0};
            if (!pop_type(original_src, stack, TYPE_INTEGER, &b)) return QUERY_ERROR;
            if (!pop_type(original_src, stack, TYPE_INTEGER, &a)) return QUERY_ERROR;
            da_append(stack, stack_bool(op->src, a.as.integer == b.as.integer));
        } break;
        case QOP_NEQ: {
            Stack_Item a = {0};
            Stack_Item b = {0};
            if (!pop_type(original_src, stack, TYPE_INTEGER, &b)) return QUERY_ERROR;
            if (!pop_type(original_src, stack, TYPE_INTEGER, &a)) return QUERY_ERROR;
            da_append(stack, stack_bool(op->src, a.as.integer != b.as.integer));
        } break;
        default: UNREACHABLE("Op_Kind");
        }
    }
    Stack_Item result = {0};
    if (!pop_type(original_src, stack, TYPE_BOOLEAN, &result)) {
        return QUERY_ERROR;
    }
    if (result.as.boolean) {
        return QUERY_MATCHED;
    }
    return QUERY_MISMATCHED;
}

void report_compile_query_diagnostic(String_View original_src, String_View src, const char *format, ...)
{
    int cursor = sv_utf8_len((String_View) {
        .data = original_src.data,
        .count = src.data - original_src.data,
    }, NULL) + 1;
    fprintf(stderr, SV_Fmt"\n", SV_Arg(original_src));
    fprintf(stderr, "%*s\n", cursor, "^");
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

bool compile_query_expr(String_View original_src, String_View *src, Query *query);

bool compile_query_primary(String_View original_src, String_View *src, Query *query)
{
    String_View token = {0};
    chop_next_query_token(src, &token);

    // We used to use dot to refer to a tag before. We still accept it for backward compatibility.
    // But we may remove it at some point in the future.
    if (sv_starts_with(token, SVLIT(":")) || sv_starts_with(token, SVLIT("."))) {
        if (sv_starts_with(token, SVLIT("."))) {
            report_compile_query_diagnostic(original_src, token, "WARNING: Using `.` to refer to tags is deprecated and will be removed in the future. Use `:` instead.");
        }
        if (token.count == 1) {
            report_compile_query_diagnostic(original_src, token, "ERROR: empty tag\n");
            return false;
        }
        sv_chop_left(&token, 1);
        da_append(query, qop_tag(token, token));
        return true;
    }

    if (sv_eq(token, SVLIT("["))) {
        char start = *token.data;
        if (!compile_query_expr(original_src, src, query)) return false;
        chop_next_query_token(src, &token);
        if (start == '[' && !sv_eq(token, SVLIT("]"))) {
            report_compile_query_diagnostic(original_src, token, "ERROR: Expected `]`.");
            return false;
        }
        return true;
    }

    if (sv_eq(token, SVLIT("not"))) {
        if (!compile_query_primary(original_src, src, query)) return false;
        da_append(query, qop_not(token));
        return true;
    }

    if (sv_eq(token, SVLIT("any"))) {
        da_append(query, qop_any(token));
        return true;
    }

    if (sv_eq(token, SVLIT("tagged"))) {
        da_append(query, qop_tagged(token));
        return true;
    }

    if (sv_eq(token, SVLIT("priority"))) {
        da_append(query, qop_priority(token));
        return true;
    }

    if (is_valid_huid(temp_sv_to_cstr(token))) {
        da_append(query, qop_id(token, token));
        return true;
    }

    size_t checkpoint = temp_save();
    char *endptr      = NULL;
    const char *nptr  = temp_sv_to_cstr(token);
    long integer      = strtol(nptr, &endptr, 10);
    bool ok           = nptr < endptr && *endptr == '\0';
    temp_rewind(checkpoint);
    if (ok) {
        da_append(query, qop_integer(token, integer));
        return true;
    }

    fprintf(stderr, "What are primary expressions:\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "    :<tag>         - checks if task has a tag\n");
    fprintf(stderr, "    [ <expr> ]     - same as previous but for Bash users\n");
    fprintf(stderr, "    not <primary>  - negation of a primary expression\n");
    fprintf(stderr, "    any            - expression that always returns true\n");
    fprintf(stderr, "    tagged         - checks if a task is tagged\n");
    fprintf(stderr, "    priority       - priority of a task as an integer\n");
    fprintf(stderr, "    <number>       - signed integer\n");
    fprintf(stderr, "    <huid>         - valid id of a task\n");
    fprintf(stderr, "\n");
    if (token.count == 0) {
        report_compile_query_diagnostic(original_src, token, "ERROR: Primary expression is expected here.");
    } else {
        report_compile_query_diagnostic(original_src, token, "ERROR: Unexpected start of a primary expression `"SV_Fmt"`.", SV_Arg(token));
    }
    return false;
}

bool compile_query_compare(String_View original_src, String_View *src, Query *query)
{
    // <expr> [<compare> <expr>]*
    if (!compile_query_primary(original_src, src, query)) return false;
    for (;;) {
        String_View saved_src = *src;
        String_View token = {0};
        chop_next_query_token(src, &token);
        if (sv_eq(token, SVLIT("lt"))) {
            if (!compile_query_primary(original_src, src, query)) return false;
            da_append(query, qop_lt(token));
            continue;
        }
        if (sv_eq(token, SVLIT("le"))) {
            if (!compile_query_primary(original_src, src, query)) return false;
            da_append(query, qop_lte(token));
            continue;
        }
        if (sv_eq(token, SVLIT("gt"))) {
            if (!compile_query_primary(original_src, src, query)) return false;
            da_append(query, qop_gt(token));
            continue;
        }
        if (sv_eq(token, SVLIT("ge"))) {
            if (!compile_query_primary(original_src, src, query)) return false;
            da_append(query, qop_gte(token));
            continue;
        }
        if (sv_eq(token, SVLIT("eq"))) {
            if (!compile_query_primary(original_src, src, query)) return false;
            da_append(query, qop_eq(token));
            continue;
        }
        if (sv_eq(token, SVLIT("ne"))) {
            if (!compile_query_primary(original_src, src, query)) return false;
            da_append(query, qop_neq(token));
            continue;
        }
        *src = saved_src;
        break;
    }
    return true;
}

bool compile_query_and(String_View original_src, String_View *src, Query *query)
{
    // <expr> [and <expr>]*
    if (!compile_query_compare(original_src, src, query)) return false;
    for (;;) {
        String_View saved_src = *src;
        String_View token = {0};
        chop_next_query_token(src, &token);
        if (!sv_eq(token, sv_from_cstr("and"))) {
            *src = saved_src;
            return true;
        }
        if (!compile_query_compare(original_src, src, query)) return false;
        da_append(query, qop_and(token));
    }
    return true;
}

bool compile_query_or(String_View original_src, String_View *src, Query *query)
{
    // <expr> [or <expr>]*
    if (!compile_query_and(original_src, src, query)) return false;
    for (;;) {
        String_View saved_src = *src;
        String_View token = {0};
        chop_next_query_token(src, &token);
        if (!sv_eq(token, sv_from_cstr("or"))) {
            *src = saved_src;
            return true;
        }
        if (!compile_query_and(original_src, src, query)) return false;
        da_append(query, qop_or(token));
    }
    return true;
}

bool compile_query_expr(String_View original_src, String_View *src, Query *query)
{
    if (!compile_query_or(original_src, src, query)) return false;
    return true;
}

bool compile_query(String_View original_src, String_View *src, Query *query)
{
    if (!compile_query_expr(original_src, src, query)) return false;
    String_View end = {0};
    if (chop_next_query_token(src, &end)) {
        fprintf(stderr, "Supported infix operators:\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "    and  or                  - logical operators\n");
        fprintf(stderr, "    lt  le  gt  ge  eq  ne   - comparison operators\n");
        fprintf(stderr, "\n");
        report_compile_query_diagnostic(original_src, end, "ERROR: Unexpected infix operator `"SV_Fmt"`", SV_Arg(end));
        return false;
    }
    return true;
}

Stack_Item stack_bool(String_View src, bool value)
{
    return (Stack_Item) {
        .type = TYPE_BOOLEAN,
        .src = src,
        .as = { .boolean = value },
    };
}

Stack_Item stack_int(String_View src, int value)
{
    return (Stack_Item) {
        .type = TYPE_INTEGER,
        .src = src,
        .as = { .integer = value },
    };
}

const char *type_name(Type type)
{
    switch (type) {
    case TYPE_BOOLEAN: return "boolean";
    case TYPE_INTEGER: return "integer";
    case __type_count:
    default:
        UNREACHABLE("type_name");
    }
    return NULL;
}

QOp qop(QOp_Kind kind, String_View src)
{
    return (QOp) { .kind = kind, .src = src };
}

QOp qop_set_tag(QOp op, String_View tag)
{
    op.as.tag = tag;
    return op;
}

QOp qop_set_id(QOp op, String_View id)
{
    op.as.id = id;
    return op;
}

QOp qop_set_integer(QOp op, long integer)
{
    op.as.integer = integer;
    return op;
}
