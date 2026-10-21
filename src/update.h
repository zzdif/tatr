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
#ifndef UPDATE_H_
#define UPDATE_H_

#include "task.h"

typedef enum {
    UOP_CLOSE,
    UOP_OPEN,
    UOP_TAG,
    UOP_UNTAG,
} UOp_Kind;

typedef struct {
    UOp_Kind kind;
    String_View tag;
    String_View src;
} UOp;

typedef struct {
    UOp *items;
    size_t count;
    size_t capacity;
} Update;

void render_update_to_sb(String_Builder *sb, Update update);

UOp uop(UOp_Kind kind, String_View src);
UOp uop_set_tag(UOp op, String_View tag);

#define uop_open(src)       uop(UOP_OPEN, (src))
#define uop_close(src)      uop(UOP_CLOSE, (src))
#define uop_tag(src, tag)   uop_set_tag(uop(UOP_TAG, (src)), (tag))
#define uop_untag(src, tag) uop_set_tag(uop(UOP_UNTAG, (src)), (tag))

bool compile_update(String_View original_src, String_View *src, Update *update);

void print_uop(UOp op);

#define chop_next_update_token unparsed_tags_next

bool update_task(Task *task, Update update);

#endif // UPDATE_H_
