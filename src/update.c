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
#include "update.h"

#define report_compile_update_diagnostic report_compile_query_diagnostic

bool compile_update(String_View original_src, String_View *src, Update *update)
{
    String_View action = {0};
    while (chop_next_update_token(src, &action)) {
        String_View token = action;
        if (sv_eq(action, SVLIT("open"))) {
            da_append(update, uop_open(token));
        } else if (sv_eq(action, SVLIT("close"))) {
            da_append(update, uop_close(token));
        } else if (sv_starts_with(action, SVLIT("+"))) {
            sv_chop_left(&action, 1);
            da_append(update, uop_tag(token, action));
        } else if (sv_starts_with(action, SVLIT("-"))) {
            sv_chop_left(&action, 1);
            da_append(update, uop_untag(token, action));
        } else {
            report_compile_update_diagnostic(original_src, token, "Unknown action "SV_Fmt, SV_Arg(action));
            return false;
        }
    }
    return true;
}

UOp uop(UOp_Kind kind, String_View src)
{
    return (UOp) {
        .kind = kind,
        .src = src,
    };
}

UOp uop_set_tag(UOp op, String_View tag)
{
    op.tag = tag;
    return op;
}

void print_uop(UOp op)
{
    switch (op.kind) {
    case UOP_CLOSE: printf("UOP_CLOSE\n");                          break;
    case UOP_OPEN:  printf("UOP_OPEN\n");                           break;
    case UOP_TAG:   printf("UOP_TAG "SV_Fmt"\n", SV_Arg(op.tag));   break;
    case UOP_UNTAG: printf("UOP_UNTAG "SV_Fmt"\n", SV_Arg(op.tag)); break;
    default: UNREACHABLE("UOp_Kind");
    }
}

bool update_task(Task *task, Update update)
{
    bool updated = false;
    da_foreach(UOp, op, &update) {
        switch (op->kind) {
        case UOP_CLOSE:
            if (!sv_eq(task->status, SVLIT("CLOSED"))) {
                task->status = SVLIT("CLOSED");
                updated = true;
            }
            break;
        case UOP_OPEN:
            if (!sv_eq(task->status, SVLIT("OPEN"))) {
                task->status = SVLIT("OPEN");
                updated = true;
            }
            break;
        case UOP_TAG:
            if (!parsed_tags_contains(task->parsed_tags, op->tag)) {
                da_append(&task->parsed_tags, op->tag);
                updated = true;
            }
            break;
        case UOP_UNTAG:
            for (size_t i = 0; i < task->parsed_tags.count;) {
                String_View tag = task->parsed_tags.items[i];
                if (sv_eq(tag, op->tag)) {
                    // TASK(20261015-212300): UOP_UNTAG should probably perform an ordered remove instead of an unordered one
                    da_remove_unordered(&task->parsed_tags, i);
                    updated = true;
                } else {
                    i += 1;
                }
            }
            break;
        default: UNREACHABLE("UOp_Kind");
        }
    }
    return updated;
}

void render_update_to_sb(String_Builder *sb, Update update)
{
    da_foreach(UOp, op, &update) {
        if (op != update.items) sb_append(sb, ',');
        switch (op->kind) {
        case UOP_CLOSE: sb_append_cstr(sb, "close");                break;
        case UOP_OPEN:  sb_append_cstr(sb, "open");                 break;
        case UOP_TAG:   sb_appendf(sb, "+"SV_Fmt, SV_Arg(op->tag)); break;
        case UOP_UNTAG: sb_appendf(sb, "-"SV_Fmt, SV_Arg(op->tag)); break;
        default: UNREACHABLE("UOp_Kind");
        }
    }
}
