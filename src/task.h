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
#ifndef TASK_H_
#define TASK_H_

bool unparsed_tags_next(String_View *unparsed_tags, String_View *tag);
bool unparsed_tags_contains(String_View unparsed_tags, String_View tag);
bool unparsed_tags_empty(String_View unparsed_tags);

typedef struct {
    String_View *items;
    size_t count;
    size_t capacity;
} Parsed_Tags;

void parse_tags(Parsed_Tags *parsed_tags, String_View unparsed_tags);
void unparse_tags(Parsed_Tags parsed_tags, String_Builder *unparsed_tags);
bool parsed_tags_contains(Parsed_Tags parsed_tags, String_View tag);

typedef struct {
    char *id;
    String_View title;
    String_View status;
    int priority;
    Parsed_Tags parsed_tags;
    Properties properties;
    String_View body;
    String_View task_md_content;
} Task;

void print_task_report(const char *rel_path, Task *task, String_View updated);
void print_task_tags_update_report(const char *rel_path, int tags_property_line, Task task, String_View old_unparsed_tags);
void append_task_md_content(String_Builder *sb, Task task);
void render_task_md(Task task, String_Builder *sb, int *tags_property_line);

typedef struct {
    Task *items;
    size_t count;
    size_t capacity;
} Tasks;

bool load_tasks(Tasks *tasks, const char *dir_path);

typedef int (*Task_Compare)(const void *a, const void *b);

Task_Compare task_sorter(bool by_id);

#endif // TASK_H_
