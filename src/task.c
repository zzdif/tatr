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
#include "task.h"
#include "ht.h"

bool parsed_tags_contains(Parsed_Tags parsed_tags, String_View tag)
{
    for (size_t i = 0; i < parsed_tags.count; ++i) {
        if (sv_eq(parsed_tags.items[i], tag)) return true;
    }
    return false;
}

void print_task_report(const char *rel_path, Task *task, String_View updated)
{
    String_View status        = task->status;
    int priority              = task->priority;
    Parsed_Tags parsed_tags   = task->parsed_tags;

    String_Builder sb_unparsed_tags = {0};
    unparse_tags(parsed_tags, &sb_unparsed_tags);
    String_View unparsed_tags = sb_to_sv(sb_unparsed_tags);

    printf("%s/%s/TASK.md:1:", rel_path, task->id); // Location
    if (updated.count) {
        printf(" <UPDATED: "SV_Fmt">", SV_Arg(updated));
    }
    printf(" "SV_Fmt, SV_Arg(status));              // Status
    printf(" [PRIORITY: %3d]", priority);           // Priority
    if (!unparsed_tags_empty(unparsed_tags)) {      // Tags
        printf(" ["SV_Fmt"]", SV_Arg(unparsed_tags));
    }
    printf(" "SV_Fmt, SV_Arg(task->title));         // Title
    printf("\n");
}

void print_task_tags_update_report(const char *rel_path, int tags_property_line, Task task, String_View old_unparsed_tags)
{
    String_Builder new_unparsed_tags = {0};
    unparse_tags(task.parsed_tags, &new_unparsed_tags);

    printf("%s/%s/TASK.md:%d:", rel_path, task.id, tags_property_line);                            // Location
    printf(" "SV_Fmt, SV_Arg(task.status));                                                        // Status
    printf(" [PRIORITY: %3d]", task.priority);                                                     // Priority
    printf(" [%"SV_Fmt2"] -> [%"SB_Fmt2"]", SV_Arg(old_unparsed_tags), SB_Arg(new_unparsed_tags)); // Tags
    printf(" "SV_Fmt, SV_Arg(task.title));                                                         // Title
    printf("\n");

}

bool load_tasks(Tasks *tasks, const char *dir_path)
{
    bool result = true;
    File_Paths children = {0};

    if (!read_entire_dir(dir_path, &children)) return_defer(false);
    size_t checkpoint = temp_save();
    for (size_t i = 0; i < children.count; ++i) {
        temp_rewind(checkpoint);
        const char *id = children.items[i];
        if (*id == '.') continue;
        if (!is_valid_huid(id)) continue;
        const char *task_path = temp_sprintf("%s/%s", dir_path, id);
        File_Type type = get_file_type(task_path);
        if (type < 0) return_defer(false);
        if (type != FILE_DIRECTORY) {
            fprintf(stderr, "ERROR: %s is not a directory\n", id);
            return_defer(false);
        }
        // This String_Builder becomes owned by Task.task_md_content.
        // So no memory deallocation is needed for it in here.
        String_Builder sb_content = {0};
        const char *task_md_path = temp_sprintf("%s/%s/TASK.md", dir_path, id);
        // TASK(20260308-171346): there should be a command that reports all the skipped weird folders and files found in the tasks/ folder
        if (!file_exists(task_md_path)) continue;
        if (!read_entire_file(task_md_path, &sb_content)) return_defer(false);
        sb_append_null(&sb_content);

        Task task = {
            .id = strdup(id),
            .task_md_content = sb_to_sv(sb_content),
            .status = SVLIT("OPEN"), // Task is open until explicitly stated otherwise
            .priority = 999999,      // Unset priority is super high so you don't forget to set it
        };

        task_md_parse(sb_content.items, &task.title, &task.properties, &task.body);

        String_View *status = properties_get(&task.properties, SVLIT("STATUS"));
        if (status) {
            task.status = *status;
        } else {
            properties_put(&task.properties, SVLIT("STATUS"), SVNULL);
        }

        String_View *priority = properties_get(&task.properties, SVLIT("PRIORITY"));
        if (priority) {
            task.priority = atoi(temp_sv_to_cstr(*priority));
        } else {
            properties_put(&task.properties, SVLIT("PRIORITY"), SVNULL);
        }

        String_View *unparsed_tags = properties_get(&task.properties, SVLIT("TAGS"));
        if (unparsed_tags) {
            parse_tags(&task.parsed_tags, *unparsed_tags);
        } else {
            properties_put(&task.properties, SVLIT("TAGS"), SVNULL);
        }

        da_append(tasks, task);
    }

defer:
    free(children.items);
    return result;
}

int task_compare_id_reverse(const void *a, const void *b)
{
    const Task *ta = a;
    const Task *tb = b;
    return strcmp(tb->id, ta->id);
}

int task_compare_priority_reverse(const void *a, const void *b)
{
    const Task *ta = a;
    const Task *tb = b;
    return tb->priority - ta->priority;
}

Task_Compare task_sorter(bool by_id)
{
    if (by_id) {
        return task_compare_id_reverse;
    } else {
        return task_compare_priority_reverse;
    }
    UNREACHABLE("task_sorter");
    return NULL;
}

bool unparsed_tags_next(String_View *unparsed_tags, String_View *tag)
{
    while (unparsed_tags->count && (isspace(*unparsed_tags->data) || *unparsed_tags->data == ',')) {
        sv_chop_left(unparsed_tags, 1);
    }
    if (unparsed_tags->count == 0) {
        *tag = sv_from_parts(unparsed_tags->data, 0);
        return false;
    }
    const char *start = unparsed_tags->data;
    while (unparsed_tags->count && !(isspace(*unparsed_tags->data) || *unparsed_tags->data == ',')) {
        sv_chop_left(unparsed_tags, 1);
    }
    *tag = sv_from_parts(start, unparsed_tags->data - start);
    return true;
}

void parse_tags(Parsed_Tags *parsed_tags, String_View unparsed_tags)
{
    String_View tag = {0};
    while (unparsed_tags_next(&unparsed_tags, &tag)) {
        da_append(parsed_tags, tag);
    }
}

void unparse_tags(Parsed_Tags parsed_tags, String_Builder *unparsed_tags)
{
    for (size_t i = 0; i < parsed_tags.count; ++i) {
        if (i > 0) sb_append_cstr(unparsed_tags, ",");
        sb_append_sv(unparsed_tags, parsed_tags.items[i]);
    }
}

bool unparsed_tags_contains(String_View unparsed_tags, String_View tag)
{
    String_View current_tag = {0};
    while (unparsed_tags_next(&unparsed_tags, &current_tag)) {
        if (sv_eq(current_tag, tag)) return true;
    }
    return false;
}

bool unparsed_tags_empty(String_View unparsed_tags)
{
    String_View tag = {0};
    if (unparsed_tags_next(&unparsed_tags, &tag)) return false;
    return true;
}

void render_task_md(Task task, String_Builder *sb, int *tags_property_line)
{
    // The `Task.status`, `Task.priority`, and `Task.parsed_tags` have priority over whatever is in `Task.properties`.
    // But the corresponding properties must at least exist there because they define the order in the rendered `TASK.md`
    // file.
    assert(properties_get(&task.properties, SVLIT("STATUS")));
    assert(properties_get(&task.properties, SVLIT("TAGS")));
    assert(properties_get(&task.properties, SVLIT("PRIORITY")));

    int line = 1;
    sb_appendf(sb, "# "SV_Fmt"\n", SV_Arg(task.title));
    line += 1;
    sb_appendf(sb, "\n");
    line += 1;
    da_foreach(Property, property, &task.properties) {
        if (sv_eq(property->key, SVLIT("STATUS"))) {
            sb_appendf(sb, "- STATUS: %"SV_Fmt2"\n", SV_Arg(task.status));
        } else if (sv_eq(property->key, SVLIT("PRIORITY"))) {
            sb_appendf(sb, "- PRIORITY: %d\n", task.priority);
        } else if (sv_eq(property->key, SVLIT("TAGS"))) {
            if (tags_property_line) *tags_property_line = line;
            sb_append_cstr(sb, "- TAGS:");
            if (task.parsed_tags.count) {
                String_Builder unparsed_tags = {0};
                unparse_tags(task.parsed_tags, &unparsed_tags);
                sb_appendf(sb, " %"SB_Fmt2, SB_Arg(unparsed_tags));
            }
            sb_append(sb, '\n');
        } else {
            sb_appendf(sb, "- "SV_Fmt": "SV_Fmt"\n", SV_Arg(property->key), SV_Arg(property->value));
        }
        line += 1;
    }
    sb_appendf(sb, SV_Fmt, SV_Arg(task.body));
}
