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
#ifndef QUERY_H_
#define QUERY_H_

#include "task.h"

bool chop_next_query_token(String_View *src, String_View *token);

// TASK(20261015-203224): all the op names related to the query language should change their names from `op` to `qop` similarly to update ops being `uop`

typedef enum {
    QOP_ANY,
    QOP_TAG,
    QOP_NOT,
    QOP_OR,
    QOP_AND,
    QOP_TAGGED,
    QOP_ID,
    QOP_PRIORITY,
    QOP_INTEGER,
    QOP_LT,
    QOP_GT,
    QOP_LTE,
    QOP_GTE,
    QOP_EQ,
    QOP_NEQ,
} QOp_Kind;

typedef struct {
    QOp_Kind kind;
    union {
        String_View tag;
        String_View id;
        long integer;
    } as;
    String_View src;
} QOp;

void print_qop(QOp op);

QOp qop(QOp_Kind kind, String_View src);
QOp qop_set_tag(QOp op, String_View tag);
QOp qop_set_id(QOp op, String_View id);
QOp qop_set_integer(QOp op, long integer);

#define qop_tag(src, tag)         qop_set_tag(qop(QOP_TAG, (src)), (tag))
#define qop_integer(src, integer) qop_set_integer(qop(QOP_INTEGER, (src)), (integer))
#define qop_id(src, id)           qop_set_id(qop(QOP_ID, (src)), (id))
#define qop_not(src)              qop(QOP_NOT, (src))
#define qop_any(src)              qop(QOP_ANY, (src))
#define qop_tagged(src)           qop(QOP_TAGGED, (src))
#define qop_priority(src)         qop(QOP_PRIORITY, (src))
#define qop_lt(src)               qop(QOP_LT, (src))
#define qop_gt(src)               qop(QOP_GT, (src))
#define qop_lte(src)              qop(QOP_LTE, (src))
#define qop_gte(src)              qop(QOP_GTE, (src))
#define qop_eq(src)               qop(QOP_EQ, (src))
#define qop_neq(src)              qop(QOP_NEQ, (src))
#define qop_and(src)              qop(QOP_AND, (src))
#define qop_or(src)               qop(QOP_OR, (src))

typedef struct {
    QOp *items;
    size_t count;
    size_t capacity;
} Query;

bool compile_query(String_View original_src, String_View *src, Query *query);

typedef enum {
    TYPE_BOOLEAN,
    TYPE_INTEGER,
    __type_count,
} Type;

const char *type_name(Type type);

_Static_assert(__type_count == 2, "Amount of stack item tags has changed");
typedef struct {
    Type type;
    String_View src;
    union {
        bool boolean;
        long integer;
    } as;
} Stack_Item;

_Static_assert(__type_count == 2, "Amount of stack item tags has changed");
Stack_Item stack_bool(String_View src, bool value);
Stack_Item stack_int(String_View src, int value);

typedef struct {
    Stack_Item *items;
    size_t count;
    size_t capacity;
} Stack;

typedef enum {
    QUERY_MATCHED,
    QUERY_MISMATCHED,
    QUERY_ERROR,
} Query_Match_Result;

Query_Match_Result task_matches_query(String_View original_src, Task task, Query query, Stack *stack);

void report_compile_query_diagnostic(String_View original_src, String_View src, const char *format, ...) NOB_PRINTF_FORMAT(3, 4);

#endif // QUERY_H_
