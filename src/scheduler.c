// Native scheduler contract. Included by tatr.c; task prose is never executable.
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include "store.h"
#include "../thirdparty/sha256.h"

typedef struct {
    Task task;
    char *raw;
    size_t len;
    struct stat st;
    char fingerprint[72];
    String_View spec, resolution;
    Parsed_Tags depends;
    bool container, selected, inspected, strict;
    int color, satisfied;
} Scheduler_Record;
typedef struct { Scheduler_Record *items; size_t count, capacity; } Scheduler_Records;
static const char *scheduler_code, *scheduler_message, *scheduler_id;
static String_Builder scheduler_cycle;
static Parsed_Tags scheduler_cycle_ids;

static bool scheduler_error(const char *code, const char *message, const char *id)
{
    scheduler_code = code; scheduler_message = message; scheduler_id = id;
    return false;
}
static bool scheduler_store_error(void)
{
    return scheduler_error(scheduler_store.code, scheduler_store.error, NULL);
}
static bool scheduler_ascii_alnum(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}
static bool scheduler_id_valid(String_View s)
{
    if (s.count < 15 || s.count > 255) return false;
    for (size_t i = 0; i < 15; ++i) {
        if (i == 8) { if (s.data[i] != '-') return false; }
        else if (s.data[i] < '0' || s.data[i] > '9') return false;
    }
    if (s.count == 15) return true;
    if (s.data[15] != '-') return false;
    for (size_t i = 16; i < s.count; ++i)
        if (!scheduler_ascii_alnum((unsigned char)s.data[i]) && s.data[i] != '-') return false;
    return true;
}
static bool scheduler_suffix_valid(const char *suffix)
{
    if (!suffix) return true;
    if (!*suffix || strlen(suffix) > 239) return false;
    for (; *suffix; ++suffix)
        if (!scheduler_ascii_alnum((unsigned char)*suffix) && *suffix != '-') return false;
    return true;
}
static bool scheduler_integer(String_View s, int *out)
{
    s = sv_trim(s);
    bool negative = s.count && s.data[0] == '-';
    size_t i = negative ? 1 : 0;
    if (i == s.count) return false;
    uint64_t n = 0, limit = negative ? 2147483648ULL : 2147483647ULL;
    for (; i < s.count; ++i) {
        if (s.data[i] < '0' || s.data[i] > '9') return false;
        unsigned d = (unsigned)(s.data[i] - '0');
        if (n > (limit - d)/10) return false;
        n = n*10 + d;
    }
    *out = negative ? (int)(-(int64_t)n) : (int)n;
    return true;
}
static bool scheduler_utf8(const char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)s[i++];
        if (!c) return false;
        if (c < 128) continue;
        unsigned value, count, min;
        if (c >= 0xc2 && c <= 0xdf) { value = c & 31; count = 1; min = 128; }
        else if (c >= 0xe0 && c <= 0xef) { value = c & 15; count = 2; min = 2048; }
        else if (c >= 0xf0 && c <= 0xf4) { value = c & 7; count = 3; min = 65536; }
        else return false;
        if (count > n-i) return false;
        while (count--) {
            c = (unsigned char)s[i++]; if ((c & 0xc0) != 0x80) return false;
            value = (value << 6) | (c & 63);
        }
        if (value < min || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
static bool scheduler_line(String_View s)
{
    return scheduler_utf8(s.data, s.count) && !memchr(s.data, '\r', s.count) && !memchr(s.data, '\n', s.count);
}
static String_View scheduler_property(Task *task, const char *key)
{
    String_View *v = properties_get(&task->properties, sv_from_cstr(key));
    return v ? *v : SVNULL;
}
static int scheduler_sv_compare(const void *a, const void *b)
{
    String_View x = *(const String_View *)a, y = *(const String_View *)b;
    size_t n = x.count < y.count ? x.count : y.count;
    int c = memcmp(x.data, y.data, n);
    return c ? c : (x.count > y.count) - (x.count < y.count);
}
static void scheduler_parse_record(Scheduler_Record *r)
{
    r->task.properties.count = 0; r->task.parsed_tags.count = 0; r->depends.count = 0;
    task_md_parse(r->raw, &r->task.title, &r->task.properties, &r->task.body);
    r->task.task_md_content = sv_from_parts(r->raw, r->len);
    r->task.status = scheduler_property(&r->task, "STATUS");
    scheduler_integer(scheduler_property(&r->task, "PRIORITY"), &r->task.priority);
    parse_tags(&r->task.parsed_tags, scheduler_property(&r->task, "TAGS"));
    r->spec = scheduler_property(&r->task, "SPEC");
    r->resolution = scheduler_property(&r->task, "RESOLUTION");
    scheduler_sha256(r->raw, r->len, r->fingerprint);
}
// Check the contiguous header before the legacy parser collapses duplicate keys.
static bool scheduler_header(Scheduler_Record *r, bool strict)
{
    if (strict && !scheduler_utf8(r->raw, r->len)) return scheduler_error("invalid_task", "Task must be UTF-8 without NUL bytes", r->task.id);
    Md md = {.source = r->raw};
    String_View title;
    if (!task_md_extract_title(&md, &title) || !sv_trim(title).count || !scheduler_line(title))
        return scheduler_error("invalid_task", "Invalid task title", r->task.id);
    md_trim_spaces(&md);
    Properties seen = {0};
    while (!md_end(&md)) {
        size_t start = md.cur;
        md_trim_spaces_except_newline(&md);
        if (!md_expect_char(&md, '-')) break;
        md_trim_spaces_except_newline(&md);
        String_View line = md_chop_until_newline(&md);
        if (line.count && line.data[line.count-1] == '\r') line.count--;
        if (!scheduler_utf8(line.data, line.count)) return scheduler_error("invalid_task", "Unclassifiable header encoding", r->task.id);
        size_t colon = 0; while (colon < line.count && line.data[colon] != ':') ++colon;
        String_View key = sv_trim(sv_from_parts(line.data, colon));
        char normalized[32]; size_t k = 0;
        for (size_t i = 0; i < key.count && k+1 < sizeof(normalized); ++i)
            if (scheduler_ascii_alnum((unsigned char)key.data[i])) normalized[k++] = (char)toupper((unsigned char)key.data[i]);
        normalized[k] = 0;
        bool near = !strncmp(normalized, "SPEC", 4) || !strncmp(normalized, "DEPEND", 6) || !strncmp(normalized, "RESOL", 5);
        bool valid = key.count && colon < line.count;
        for (size_t i = 0; i < key.count; ++i) if (!scheduler_ascii_alnum((unsigned char)key.data[i])) valid = false;
        if (near && (strict || !strncmp(normalized, "SPEC", 4)) && (!valid || !(sv_eq(key, SVLIT("SPEC")) || sv_eq(key, SVLIT("DEPENDS")) || sv_eq(key, SVLIT("RESOLUTION")))))
            return scheduler_error("invalid_task", "Malformed or case-variant scheduling header", r->task.id);
        if (!valid) { md.cur = start; break; }
        if (properties_get(&seen, key) && (strict || sv_eq(key, SVLIT("SPEC")))) return scheduler_error("invalid_task", "Duplicate header key", r->task.id);
        properties_put(&seen, key, SVNULL);
        if (memchr(line.data, '\r', line.count)) return scheduler_error("invalid_task", "Invalid header text", r->task.id);
        md_expect_char(&md, '\n');
    }
    free(seen.items);
    if (!strict && md_end(&md) && md.cur < r->len)
        return scheduler_error("invalid_task", "NUL prevents classifying task header", r->task.id);
    return true;
}
static bool scheduler_strict(Scheduler_Record *r)
{
    if (r->strict) return true;
    if (!scheduler_header(r, true)) return false;
    if (!(sv_eq(r->task.status, SVLIT("OPEN")) || sv_eq(r->task.status, SVLIT("CLOSED"))) ||
        !scheduler_integer(scheduler_property(&r->task, "PRIORITY"), &r->task.priority))
        return scheduler_error("invalid_task", "Explicit STATUS and signed 32-bit decimal PRIORITY required", r->task.id);
    if (r->spec.count && !scheduler_id_valid(r->spec)) return scheduler_error("invalid_relationship", "Malformed SPEC ID", r->task.id);
    if (r->resolution.count && !(sv_eq(r->resolution, SVLIT("DONE")) || sv_eq(r->resolution, SVLIT("CANCELLED"))))
        return scheduler_error("invalid_task", "Invalid RESOLUTION", r->task.id);
    if (sv_eq(r->task.status, SVLIT("OPEN")) && r->resolution.count)
        return scheduler_error("invalid_task", "OPEN tasks cannot have a resolution", r->task.id);
    r->depends.count = 0;
    String_View list = scheduler_property(&r->task, "DEPENDS");
    while (list.count) {
        String_View id = sv_trim(sv_chop_by_delim(&list, ','));
        if (!scheduler_id_valid(id) || sv_eq(id, sv_from_cstr(r->task.id)) || parsed_tags_contains(r->depends, id))
            return scheduler_error("invalid_relationship", "Malformed, self, or duplicate dependency", r->task.id);
        da_append(&r->depends, id);
        if (!list.count && list.data > id.data && list.data[-1] == ',')
            return scheduler_error("invalid_relationship", "Empty dependency element", r->task.id);
    }
    if (r->depends.count > 1) qsort(r->depends.items, r->depends.count, sizeof(String_View), scheduler_sv_compare);
    r->strict = true;
    return true;
}
static int scheduler_record_compare(const void *a, const void *b)
{
    return strcmp(((const Scheduler_Record *)a)->task.id, ((const Scheduler_Record *)b)->task.id);
}
static Scheduler_Record *scheduler_lookup(Scheduler_Records *rs, String_View id)
{
    for (size_t i = 0; i < rs->count; ++i) if (sv_eq(sv_from_cstr(rs->items[i].task.id), id)) return &rs->items[i];
    return NULL;
}
static bool scheduler_discover(Scheduler_Records *rs)
{
    DIR *dir = fdopendir(openat(scheduler_store.fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!dir) return scheduler_error("io_error", "Cannot enumerate database", NULL);
    struct dirent *e;
    for (;;) {
        errno = 0; e = readdir(dir);
        if (!e) {
            if (errno) { closedir(dir); return scheduler_error("io_error", "Database enumeration failed", NULL); }
            break;
        }
        if (!scheduler_id_valid(sv_from_cstr(e->d_name))) continue;
        Scheduler_Record r = {0}; r.task.id = strdup(e->d_name);
        da_append(rs, r);
    }
    closedir(dir);
    if (rs->count > 1) qsort(rs->items, rs->count, sizeof(*rs->items), scheduler_record_compare);
    for (size_t i = 0; i < rs->count; ++i) {
        Scheduler_Record *r = &rs->items[i];
        if (!store_read(&scheduler_store, r->task.id, &r->raw, &r->len, &r->st)) {
            scheduler_store_error(); scheduler_id = r->task.id; return false;
        }
        // Membership must be classifiable even for unrelated legacy records.
        if (!scheduler_header(r, false)) return false;
        scheduler_parse_record(r);
    }
    for (size_t i = 0; i < rs->count; ++i) {
        Scheduler_Record *r = &rs->items[i];
        if (scheduler_id_valid(r->spec)) {
            Scheduler_Record *parent = scheduler_lookup(rs, r->spec);
            if (parent) parent->container = true;
        }
    }
    return true;
}
// A generated ID may resolve a legal dangling draft even without supplied relationships.
static bool scheduler_incoming_candidate(const char *id)
{
    DIR *dir = fdopendir(openat(scheduler_store.fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!dir) return false;
    bool incoming = false; struct dirent *entry;
    while (!incoming && (entry = readdir(dir))) {
        if (!scheduler_id_valid(sv_from_cstr(entry->d_name))) continue;
        Scheduler_Record r = {0}; r.task.id = entry->d_name;
        if (!store_read(&scheduler_store, r.task.id, &r.raw, &r.len, &r.st)) continue;
        scheduler_parse_record(&r);
        incoming = sv_eq(r.spec, sv_from_cstr(id));
        String_View deps = scheduler_property(&r.task, "DEPENDS");
        while (deps.count && !incoming) incoming = sv_eq(sv_trim(sv_chop_by_delim(&deps, ',')), sv_from_cstr(id));
        free(r.raw); free(r.task.properties.items); free(r.task.parsed_tags.items);
    }
    closedir(dir);
    return incoming;
}
static bool scheduler_visit(Scheduler_Records *rs, Scheduler_Record *r, Parsed_Tags *stack)
{
    if (r->color == 2) return true;
    if (r->color == 1) {
        scheduler_cycle.count = 0; bool started = false;
        for (size_t i = 0; i < stack->count; ++i) {
            if (sv_eq(stack->items[i], sv_from_cstr(r->task.id))) started = true;
            if (started) {
                da_append(&scheduler_cycle_ids, stack->items[i]);
                if (scheduler_cycle.count) sb_append_cstr(&scheduler_cycle, " -> ");
                sb_append_sv(&scheduler_cycle, stack->items[i]);
            }
        }
        da_append(&scheduler_cycle_ids, sv_from_cstr(r->task.id));
        sb_appendf(&scheduler_cycle, " -> %s", r->task.id); sb_append_null(&scheduler_cycle);
        return scheduler_error("dependency_cycle", scheduler_cycle.items, r->task.id);
    }
    r->inspected = true;
    if (!scheduler_strict(r)) return false;
    if (r->container && (r->spec.count || r->depends.count)) return scheduler_error("invalid_relationship", "Spec containers cannot have SPEC or DEPENDS", r->task.id);
    r->color = 1; da_append(stack, sv_from_cstr(r->task.id));
    if (r->spec.count) {
        Scheduler_Record *parent = scheduler_lookup(rs, r->spec);
        if (!parent) return scheduler_error("invalid_relationship", "SPEC record does not exist", r->task.id);
        if (parent == r || parent->spec.count) return scheduler_error("invalid_relationship", "Self-membership or nested specs unsupported", r->task.id);
        if (!scheduler_visit(rs, parent, stack)) return false;
    }
    for (size_t i = 0; i < r->depends.count; ++i) {
        Scheduler_Record *dep = scheduler_lookup(rs, r->depends.items[i]);
        if (!dep) return scheduler_error("missing_dependency", temp_sprintf("Missing dependency %.*s", (int)r->depends.items[i].count, r->depends.items[i].data), r->task.id);
        if (dep->container) return scheduler_error("invalid_relationship", "Dependencies cannot target spec containers", r->task.id);
        if (!scheduler_visit(rs, dep, stack)) return false;
    }
    stack->count--; r->color = 2;
    return true;
}
static bool scheduler_graph(Scheduler_Records *rs, Scheduler_Record *root, bool scope)
{
    Parsed_Tags stack = {0};
    for (size_t i = 0; i < rs->count; ++i) { rs->items[i].color = 0; rs->items[i].inspected = false; }
    if (scope) root->container = true;
    if (!scheduler_visit(rs, root, &stack)) return false;
    // A relationship write can turn an existing dependency target into a container.
    if (!scope) for (size_t i = 0; i < rs->count; ++i) {
        String_View list = scheduler_property(&rs->items[i].task, "DEPENDS");
        while (list.count) {
            String_View id = sv_trim(sv_chop_by_delim(&list, ','));
            Scheduler_Record *target = scheduler_lookup(rs, id);
            if (target && target->container && target->inspected)
                return scheduler_error("invalid_relationship", "Dependency targets an affected spec container", rs->items[i].task.id);
        }
    }
    if (scope) for (size_t i = 0; i < rs->count; ++i) {
        Scheduler_Record *r = &rs->items[i];
        if (!sv_eq(r->spec, sv_from_cstr(root->task.id))) continue;
        r->selected = true;
        if (r->container) return scheduler_error("invalid_relationship", "Spec container cannot be a worker candidate", r->task.id);
        if (!scheduler_visit(rs, r, &stack)) return false;
    }
    free(stack.items); return true;
}
static bool scheduler_satisfied(Scheduler_Records *rs, Scheduler_Record *r)
{
    if (r->satisfied) return r->satisfied == 1;
    bool done = sv_eq(r->task.status, SVLIT("CLOSED")) && sv_eq(r->resolution, SVLIT("DONE"));
    for (size_t i = 0; i < r->depends.count && done; ++i) done = scheduler_satisfied(rs, scheduler_lookup(rs, r->depends.items[i]));
    r->satisfied = done ? 1 : -1; return done;
}
static void scheduler_json_string(String_View s)
{
    if (!scheduler_utf8(s.data, s.count)) s = SVLIT("Invalid UTF-8 diagnostic");
    putchar('"');
    for (size_t i = 0; i < s.count; ++i) {
        unsigned char c = (unsigned char)s.data[i];
        if (c == '"' || c == '\\') { putchar('\\'); putchar(c); }
        else if (c < 32) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}
static void scheduler_json_cstr(const char *s) { if (s) scheduler_json_string(sv_from_cstr(s)); else printf("null"); }
static void scheduler_json_views(Parsed_Tags vs)
{
    putchar('['); for (size_t i = 0; i < vs.count; ++i) { if (i) putchar(','); scheduler_json_string(vs.items[i]); } putchar(']');
}
static void scheduler_json_task(Scheduler_Record *r)
{
    printf("{\"id\":"); scheduler_json_cstr(r->task.id);
    printf(",\"path\":"); scheduler_json_cstr(temp_sprintf("%s/TASK.md", r->task.id));
    printf(",\"title\":"); scheduler_json_string(r->task.title);
    printf(",\"body\":"); scheduler_json_string(r->task.body);
    printf(",\"status\":"); scheduler_json_string(r->task.status);
    printf(",\"priority\":%d,\"tags\":", r->task.priority); scheduler_json_views(r->task.parsed_tags);
    printf(",\"spec\":"); if (r->spec.count) scheduler_json_string(r->spec); else printf("null");
    printf(",\"depends\":"); scheduler_json_views(r->depends);
    printf(",\"resolution\":"); if (r->resolution.count) scheduler_json_string(r->resolution); else printf("null");
    printf(",\"properties\":{");
    for (size_t i = 0; i < r->task.properties.count; ++i) {
        if (i) putchar(',');
        scheduler_json_string(r->task.properties.items[i].key);
        putchar(':');
        scheduler_json_string(r->task.properties.items[i].value);
    }
    printf("},\"fingerprint\":"); scheduler_json_cstr(r->fingerprint); putchar('}');
}
static void scheduler_envelope(const char *cmd, bool ok)
{
    printf("{\"schema_version\":1,\"command\":"); scheduler_json_cstr(cmd);
    printf(",\"database\":"); scheduler_json_cstr(scheduler_store.path);
    printf(",\"ok\":%s,\"data\":", ok ? "true" : "false");
}
static void scheduler_finish(void) { printf(",\"diagnostics\":[]}\n"); }
static bool scheduler_report_error(const char *cmd, bool json)
{
    if (!scheduler_code) scheduler_error("invalid_arguments", "Invalid arguments", NULL);
    if (json) {
        scheduler_envelope(cmd, false); printf("null,\"diagnostics\":[{\"code\":"); scheduler_json_cstr(scheduler_code);
        printf(",\"message\":"); scheduler_json_cstr(scheduler_message);
        if (scheduler_id) { printf(",\"task_id\":"); scheduler_json_cstr(scheduler_id); }
        if (scheduler_cycle_ids.count) { printf(",\"cycle\":"); scheduler_json_views(scheduler_cycle_ids); }
        if (scheduler_store.write_state) { printf(",\"write_state\":"); scheduler_json_cstr(scheduler_store.write_state); }
        printf("}]}\n");
    } else fprintf(stderr, "ERROR: %s: %s\n", scheduler_code, scheduler_message);
    return false;
}
static bool scheduler_property_args(Flag_List args, Properties *ps, bool creation)
{
    for (size_t i = 0; i < args.count; ++i) {
        String_View arg = sv_from_cstr(args.items[i]);
        if (!scheduler_line(arg)) return scheduler_error("invalid_arguments", "Property must be a single UTF-8 line", NULL);
        const char *eq = strchr(args.items[i], '=');
        if (!eq) return scheduler_error("invalid_arguments", "Property requires KEY=VALUE", NULL);
        String_View key = sv_from_parts(arg.data, eq-arg.data), value = sv_from_cstr(eq+1);
        if (!key.count) return scheduler_error("invalid_arguments", "Empty property key", NULL);
        for (size_t j = 0; j < key.count; ++j) if (!scheduler_ascii_alnum((unsigned char)key.data[j])) return scheduler_error("invalid_arguments", "Invalid property key", NULL);
        if (properties_get(ps, key)) return scheduler_error("invalid_arguments", "Repeated property argument", NULL);
        if (creation && (sv_eq(key, SVLIT("STATUS")) || sv_eq(key, SVLIT("PRIORITY")) || sv_eq(key, SVLIT("TAGS"))))
            return scheduler_error("invalid_arguments", "Creation canonical fields belong to flags/defaults", NULL);
        properties_put(ps, key, sv_trim(value));
    }
    return true;
}
static bool scheduler_effective_equal(Scheduler_Record *a, Scheduler_Record *b, Properties *changes)
{
    for (size_t i = 0; i < changes->count; ++i) {
        String_View key = changes->items[i].key;
        if (sv_eq(key, SVLIT("PRIORITY"))) { if (a->task.priority != b->task.priority) return false; }
        else if (sv_eq(key, SVLIT("DEPENDS"))) {
            if (a->depends.count != b->depends.count) return false;
            for (size_t j = 0; j < a->depends.count; ++j) if (!sv_eq(a->depends.items[j], b->depends.items[j])) return false;
        } else if (sv_eq(key, SVLIT("TAGS"))) {
            if (a->task.parsed_tags.count != b->task.parsed_tags.count) return false;
            for (size_t j = 0; j < a->task.parsed_tags.count; ++j) if (!sv_eq(a->task.parsed_tags.items[j], b->task.parsed_tags.items[j])) return false;
        } else {
            String_View *x = properties_get(&a->task.properties, key), *y = properties_get(&b->task.properties, key);
            bool recognized = sv_eq(key, SVLIT("SPEC")) || sv_eq(key, SVLIT("RESOLUTION"));
            if (!x && recognized && y && !y->count) continue;
            if (!x || !y || !sv_eq(*x, *y)) return false;
        }
    }
    return true;
}
static bool scheduler_render_changes(Scheduler_Record *r, Properties changes, Scheduler_Record *next)
{
    Task task = r->task;
    task.properties = (Properties){0};
    for (size_t i = 0; i < r->task.properties.count; ++i) da_append(&task.properties, r->task.properties.items[i]);
    String_View *status = properties_get(&changes, SVLIT("STATUS"));
    String_View *resolution = properties_get(&changes, SVLIT("RESOLUTION"));
    if (status && sv_eq(*status, SVLIT("OPEN")) && (!resolution || !resolution->count)) properties_put(&changes, SVLIT("RESOLUTION"), SVNULL);
    for (size_t i = 0; i < changes.count; ++i) properties_put(&task.properties, changes.items[i].key, changes.items[i].value);
    task.status = scheduler_property(&task, "STATUS");
    if (!scheduler_integer(scheduler_property(&task, "PRIORITY"), &task.priority)) return scheduler_error("invalid_task", "Invalid PRIORITY", task.id);
    task.parsed_tags = (Parsed_Tags){0}; parse_tags(&task.parsed_tags, scheduler_property(&task, "TAGS"));
    if (!properties_get(&task.properties, SVLIT("TAGS"))) properties_put(&task.properties, SVLIT("TAGS"), SVNULL);
    String_Builder bytes = {0}; render_task_md(task, &bytes, NULL); size_t n = bytes.count; sb_append_null(&bytes);
    *next = (Scheduler_Record){.raw = bytes.items, .len = n, .st = r->st}; next->task.id = r->task.id;
    scheduler_parse_record(next);
    return scheduler_strict(next);
}
static int scheduler_frontier_compare(const void *a, const void *b)
{
    const Scheduler_Record *x = *(Scheduler_Record *const *)a, *y = *(Scheduler_Record *const *)b;
    if (x->task.priority != y->task.priority) return (x->task.priority < y->task.priority) ? 1 : -1;
    return strcmp(x->task.id, y->task.id);
}
static void scheduler_ready_output(Scheduler_Records *rs, Scheduler_Record *root, bool ready, bool json, const char *cmd)
{
    String_Builder snapshot = {0}; size_t children = 0; bool complete = true;
    Scheduler_Record **frontier = calloc(rs->count+1, sizeof(*frontier)); size_t nready = 0;
    for (size_t i = 0; i < rs->count; ++i) {
        Scheduler_Record *r = &rs->items[i];
        if (r->inspected) sb_appendf(&snapshot, "%s %s\n", r->task.id, r->fingerprint);
        if (!r->selected) continue;
        ++children;
        if (!scheduler_satisfied(rs, r)) complete = false;
        bool eligible = sv_eq(r->task.status, SVLIT("OPEN"));
        for (size_t j = 0; j < r->depends.count && eligible; ++j) eligible = scheduler_satisfied(rs, scheduler_lookup(rs, r->depends.items[j]));
        if (eligible) frontier[nready++] = r;
    }
    qsort(frontier, nready, sizeof(*frontier), scheduler_frontier_compare);
    char token[72]; scheduler_sha256(snapshot.items, snapshot.count, token);
    const char *state = nready ? "ready" : !children ? "empty" : complete ? "complete" : "blocked";
    if (!json) { printf("%s: %s (%zu ready, %zu children) snapshot %s\n", root->task.id, ready ? state : "valid", nready, children, token); }
    else {
        scheduler_envelope(cmd, true); printf("{\"spec\":"); scheduler_json_cstr(root->task.id);
        printf(",\"snapshot\":"); scheduler_json_cstr(token); printf(",\"tasks\":[");
        bool comma = false;
        for (size_t i = 0; i < rs->count; ++i) if (rs->items[i].inspected) { if (comma) putchar(','); scheduler_json_task(&rs->items[i]); comma = true; }
        putchar(']');
        if (ready) {
            printf(",\"state\":"); scheduler_json_cstr(state); printf(",\"ready\":[");
            for (size_t i = 0; i < nready; ++i) { if (i) putchar(','); scheduler_json_task(frontier[i]); }
            printf("],\"blocked\":["); comma = false;
            for (size_t i = 0; i < rs->count; ++i) {
                Scheduler_Record *r = &rs->items[i];
                if (!r->selected || (sv_eq(r->task.status, SVLIT("CLOSED")) && !sv_eq(r->resolution, SVLIT("DONE")))) continue;
                bool emitted = false;
                for (size_t j = 0; j < r->depends.count; ++j) {
                    Scheduler_Record *dep = scheduler_lookup(rs, r->depends.items[j]);
                    if (scheduler_satisfied(rs, dep)) continue;
                    if (!emitted) { if (comma) putchar(','); printf("{\"id\":"); scheduler_json_cstr(r->task.id); printf(",\"status\":"); scheduler_json_string(r->task.status); printf(",\"prerequisites\":["); emitted = true; comma = true; }
                    else putchar(',');
                    printf("{\"id\":"); scheduler_json_cstr(dep->task.id); printf(",\"reason\":");
                    const char *reason = sv_eq(dep->task.status, SVLIT("OPEN")) ? "open" : sv_eq(dep->resolution, SVLIT("CANCELLED")) ? "cancelled" : !dep->resolution.count ? "unspecified_closure" : "recursively_unsatisfied";
                    scheduler_json_cstr(reason); printf(",\"external\":%s}", dep->selected ? "false" : "true");
                }
                if (emitted) printf("]}");
            }
            printf("],\"unresolved\":["); comma = false;
            for (size_t i = 0; i < rs->count; ++i) {
                Scheduler_Record *r = &rs->items[i];
                if (!r->selected || !sv_eq(r->task.status, SVLIT("CLOSED")) || sv_eq(r->resolution, SVLIT("DONE"))) continue;
                if (comma) putchar(',');
                comma = true;
                printf("{\"id\":"); scheduler_json_cstr(r->task.id);
                printf(",\"reason\":"); scheduler_json_cstr(r->resolution.count ? "cancelled" : "unspecified_closure");
                putchar('}');
            }
            putchar(']');
        }
        putchar('}'); scheduler_finish();
    }
    free(frontier); free(snapshot.items);
}

static bool scheduler_run(Command *self, const char *program, int argc, char **argv)
{
    bool json = false, help = false, closed = false, ascending = false, by_id = false, debug = false, path_only = false;
    char *spec = NULL, *expect = NULL, *priority = "100", *suffix = NULL, *body_file = NULL;
    Flag_List props = {0}, tags = {0}, updates = {0};
    const char *cmd = self->name;
    bool isnew = !strcmp(cmd,"new"), isset = !strcmp(cmd,"set"), isls = !strcmp(cmd,"ls"), isfind = !strcmp(cmd,"find");
    bool graph = !strcmp(cmd,"check") || !strcmp(cmd,"ready");
    void *c = flag_c_new(program);
    flag_c_bool_var(c,&help,"help",false,"Print help without accessing a database");
    flag_c_bool_var(c,&json,"json",false,"Emit schema_version 1 JSON");
    // -db is removed by the shared dispatcher before command parsing.
    char *unused_db = NULL; flag_c_str_var(c,&unused_db,"db",NULL,"Explicit tasks directory (required for set/check/ready)");
    if (isnew || isset) flag_c_list_var(c,&props,"property","KEY=VALUE (repeatable)");
    if (isnew) {
        flag_c_str_var(c,&priority,"p","100","Signed 32-bit decimal priority"); flag_c_str_var(c,&suffix,"s",NULL,"Nonempty ASCII alphanumeric/hyphen suffix");
        flag_c_str_var(c,&body_file,"body-file",NULL,"Regular UTF-8 body file"); flag_c_list_var(c,&tags,"t","Tags");
    }
    if (isset) flag_c_str_var(c,&expect,"expect",NULL,"Expected raw SHA-256 fingerprint (required)");
    if (graph) flag_c_str_var(c,&spec,"spec",NULL,"Selected spec HUID (required)");
    if (isls) {
        flag_c_bool_var(c,&closed,"c",false,"Closed tasks only"); flag_c_bool_var(c,&ascending,"a",false,"Reverse complete order"); flag_c_bool_var(c,&by_id,"id",false,"Sort by ID");
        flag_c_bool_var(c,&debug,"debug",false,"Not supported with JSON"); flag_c_list_var(c,&updates,"update","Not supported with JSON");
    }
    if (isfind) flag_c_bool_var(c,&path_only,"path-only",false,"Not supported with JSON");
    // Recognize JSON even when parsing a preceding option fails.
    for (int i=0;i<argc && argv[i][0]=='-';++i) { if (!strcmp(argv[i],"-json")) json=true; }
    bool parsed = flag_c_parse(c,argc,argv);
    if (help) { print_command_usage(stdout,self,program,c); return true; }
    if (!parsed) return scheduler_report_error(cmd,json);
    argc=flag_c_rest_argc(c); argv=flag_c_rest_argv(c);
    if (debug || updates.count || path_only || ((isset || graph) && !scheduler_explicit)) return scheduler_report_error(cmd,json);
    if (graph && (!spec || argc || !scheduler_id_valid(sv_from_cstr(spec)))) return scheduler_report_error(cmd,json);
    if ((isset || isfind) && (argc!=1 || !scheduler_id_valid(sv_from_cstr(argv[0])))) return scheduler_report_error(cmd,json);
    if (isset && (!expect || strlen(expect)!=71 || strncmp(expect,"sha256:",7) || !props.count)) return scheduler_report_error(cmd,json);
    if (isset) for (size_t i = 7; i < 71; ++i)
        if (!((expect[i] >= '0' && expect[i] <= '9') || (expect[i] >= 'a' && expect[i] <= 'f'))) return scheduler_report_error(cmd,json);
    if (scheduler_code) return scheduler_report_error(cmd,json);
    Scheduler_Records rs = {0}; Scheduler_Record *result = NULL;
    Properties changes = {0};
    if ((isset || isnew) && !scheduler_property_args(props,&changes,isnew)) return scheduler_report_error(cmd,json);
    if (isnew) {
        int p;
        if (!scheduler_integer(sv_from_cstr(priority),&p)) return scheduler_report_error(cmd,json);
        if (!scheduler_suffix_valid(suffix)) return scheduler_report_error(cmd,json);
        String_Builder title={0}; for (int i=0;i<argc;++i) { if (i) sb_append(&title,' '); sb_append_cstr(&title,argv[i]); }
        String_View t=title.count ? sb_to_sv(title) : SVLIT("New Task");
        if (!scheduler_line(t)) return scheduler_report_error(cmd,json);
        String_Builder body={0};
        if (body_file) {
            int fd=open(body_file,O_RDONLY|O_NOFOLLOW|O_NONBLOCK); struct stat st;
            if (fd<0 || fstat(fd,&st)<0 || !S_ISREG(st.st_mode)) { if(fd>=0) close(fd); scheduler_error("unsafe_path","Body input must be an ordinary regular file",NULL); return scheduler_report_error(cmd,json); }
            char buf[4096]; ssize_t n; while ((n=read(fd,buf,sizeof(buf)))>0) da_append_many(&body,buf,(size_t)n);
            close(fd); if(n<0 || !scheduler_utf8(body.items,body.count)) { scheduler_error("invalid_arguments","Body input must be readable UTF-8 without NUL",NULL); return scheduler_report_error(cmd,json); }
        }
        Task task={.title=t,.status=SVLIT("OPEN"),.priority=p};
        properties_put(&task.properties,SVLIT("STATUS"),SVNULL); properties_put(&task.properties,SVLIT("PRIORITY"),SVNULL); properties_put(&task.properties,SVLIT("TAGS"),SVNULL);
        for(size_t i=0;i<tags.count;++i) { if(!scheduler_line(sv_from_cstr(tags.items[i]))) return scheduler_report_error(cmd,json); parse_tags(&task.parsed_tags,sv_from_cstr(tags.items[i])); }
        for(size_t i=0;i<changes.count;++i) properties_put(&task.properties,changes.items[i].key,changes.items[i].value);
        String_Builder payload={0}; sb_append(&payload,'\n');
        if (body.count) sb_append_sb(&payload,body);
        task.body=sb_to_sv(payload);
        String_Builder raw={0}; render_task_md(task,&raw,NULL); size_t len=raw.count; sb_append_null(&raw);
        bool relationships = properties_get(&changes, SVLIT("SPEC")) || properties_get(&changes, SVLIT("DEPENDS"));
        if(relationships && !scheduler_discover(&rs)) return scheduler_report_error(cmd,json);
        char *id=strdup(temp_new_huid(suffix));
        struct stat existing;
        if(fstatat(scheduler_store.fd,id,&existing,AT_SYMLINK_NOFOLLOW)==0) { sleep(1); id=strdup(temp_new_huid(suffix)); }
        if (!relationships && scheduler_incoming_candidate(id) && !scheduler_discover(&rs)) return scheduler_report_error(cmd,json);
        Scheduler_Record r={.raw=raw.items,.len=len}; r.task.id=id; scheduler_parse_record(&r);
        if(!scheduler_strict(&r)) return scheduler_report_error(cmd,json);
        da_append(&rs,r); result=&rs.items[rs.count-1];
        for(size_t i=0;i<rs.count;++i) if(scheduler_id_valid(rs.items[i].spec)) { Scheduler_Record *parent=scheduler_lookup(&rs,rs.items[i].spec); if(parent) parent->container=true; }
        if(!scheduler_graph(&rs,result,result->container)) return scheduler_report_error(cmd,json);
        for (size_t i = 0; i+1 < rs.count; ++i) {
            String_View deps = scheduler_property(&rs.items[i].task, "DEPENDS");
            while (deps.count) if (sv_eq(sv_trim(sv_chop_by_delim(&deps, ',')), sv_from_cstr(id))) {
                if (!scheduler_graph(&rs, &rs.items[i], false)) return scheduler_report_error(cmd,json);
                break;
            }
        }
        if(!store_create(&scheduler_store,id,raw.items,len)) { scheduler_store_error(); return scheduler_report_error(cmd,json); }
    } else if (isfind) {
        Scheduler_Record r={0}; r.task.id=argv[0];
        if(!store_read(&scheduler_store,r.task.id,&r.raw,&r.len,&r.st)) { scheduler_store_error(); return scheduler_report_error(cmd,json); }
        if (!scheduler_utf8(r.raw, r.len)) { scheduler_error("invalid_task", "Task must be UTF-8 without NUL bytes", r.task.id); return scheduler_report_error(cmd,json); }
        scheduler_parse_record(&r); if(!scheduler_strict(&r)) return scheduler_report_error(cmd,json);
        da_append(&rs,r); result=&rs.items[0];
    } else {
        if(!scheduler_discover(&rs)) return scheduler_report_error(cmd,json);
        if(isset || graph) {
            result=scheduler_lookup(&rs,sv_from_cstr(isset?argv[0]:spec));
            if(!result) { scheduler_error("task_not_found","Task not found",isset?argv[0]:spec); return scheduler_report_error(cmd,json); }
        }
        if(isset) {
            if(strcmp(expect,result->fingerprint)) { scheduler_error("conflict","Expected fingerprint does not match",result->task.id); return scheduler_report_error(cmd,json); }
            if(!scheduler_strict(result)) return scheduler_report_error(cmd,json);
            Scheduler_Record original=*result, next={0};
            if(!scheduler_render_changes(result,changes,&next)) return scheduler_report_error(cmd,json);
            if(sv_eq(next.task.status,SVLIT("OPEN")) && original.resolution.count && !next.resolution.count && !properties_get(&changes,SVLIT("RESOLUTION"))) properties_put(&changes,SVLIT("RESOLUTION"),SVNULL);
            *result=next;
            for(size_t i=0;i<rs.count;++i) rs.items[i].container=false;
            for(size_t i=0;i<rs.count;++i) if(scheduler_id_valid(rs.items[i].spec)) { Scheduler_Record *parent=scheduler_lookup(&rs,rs.items[i].spec); if(parent) parent->container=true; }
            if(!scheduler_graph(&rs,result,false)) return scheduler_report_error(cmd,json);
            if(scheduler_effective_equal(&original,result,&changes)) *result=original;
            else if(!store_replace(&scheduler_store,result->task.id,original.raw,original.len,&original.st,result->raw,result->len)) { scheduler_store_error(); return scheduler_report_error(cmd,json); }
        } else if(graph) {
            if(!scheduler_graph(&rs,result,true)) return scheduler_report_error(cmd,json);
            scheduler_ready_output(&rs,result,!strcmp(cmd,"ready"),json,cmd); return true;
        } else if(isls) {
            String_Builder src={0}; for(int i=0;i<argc;++i) { if(i) sb_append(&src,' '); sb_append_cstr(&src,argv[i]); }
            String_View query_src=src.count?sb_to_sv(src):SVLIT("any"), original=query_src; Query query={0}; Stack stack={0};
            query_quiet_diagnostics = true;
            if(!compile_query(original,&query_src,&query)) return scheduler_report_error(cmd,json);
            Scheduler_Record **list=calloc(rs.count+1,sizeof(*list)); size_t count=0;
            for(size_t i=0;i<rs.count;++i) {
                Scheduler_Record *r=&rs.items[i];
                bool isclosed=sv_eq(r->task.status,SVLIT("CLOSED")); if(isclosed!=closed) continue;
                Query_Match_Result match=task_matches_query(original,r->task,query,&stack);
                if(match==QUERY_ERROR) return scheduler_report_error(cmd,json);
                if(match==QUERY_MATCHED) {
                    if(!scheduler_strict(r)) return scheduler_report_error(cmd,json);
                    list[count++]=r;
                }
            }
            // ID default is descending; priority default is descending with ascending ID ties.
            qsort(list,count,sizeof(*list),scheduler_frontier_compare);
            if(by_id) { for(size_t i=0;i<count;++i) for(size_t j=i+1;j<count;++j) if(strcmp(list[i]->task.id,list[j]->task.id)<0) { Scheduler_Record *tmp=list[i];list[i]=list[j];list[j]=tmp; } }
            scheduler_envelope(cmd,true); printf("{\"tasks\":[");
            for(size_t i=0;i<count;++i) { if(i) putchar(','); scheduler_json_task(list[ascending?count-1-i:i]); }
            printf("]}"); scheduler_finish(); free(list); return true;
        }
    }
    if(json) { scheduler_envelope(cmd,true); printf("{\"task\":"); scheduler_json_task(result); putchar('}'); scheduler_finish(); }
    else print_task_report(find_relative_tasks_directory(),&result->task,SVNULL);
    return true;
}

static bool scheduler_dispatch(Command *self, const char *program, int argc, char **argv)
{
    const char *cmd=self->name;
    bool relevant=!strcmp(cmd,"ls") || !strcmp(cmd,"find") || !strcmp(cmd,"new") || !strcmp(cmd,"todo") || !strcmp(cmd,"set") || !strcmp(cmd,"check") || !strcmp(cmd,"ready");
    if(!relevant) return self->run(self,program,argc,argv);
    signal(SIGPIPE, SIG_IGN);
    bool json=false, help=false, debug_only=false, mutation=!strcmp(cmd,"new") || !strcmp(cmd,"todo") || !strcmp(cmd,"set");
    const char *db=NULL; char **args=calloc((size_t)argc+1,sizeof(*args)); int count=0;
    bool options=true;
    for(int i=0;i<argc;++i) {
        if(options && !strcmp(argv[i],"-db")) { scheduler_explicit=true; if(i+1>=argc || db) scheduler_error("invalid_arguments","-db requires exactly one directory",NULL); else db=argv[++i]; continue; }
        if(options && !strcmp(argv[i],"-json")) json=true;
        if(options && !strcmp(argv[i],"-help")) help=true;
        if(options && !strcmp(argv[i],"-debug") && !strcmp(cmd,"ls")) debug_only=true;
        if(options && !strcmp(argv[i],"-update")) mutation=true;
        args[count++]=argv[i];
        if(options && argv[i][0]=='-') {
            bool value=!strcmp(argv[i],"-property") || !strcmp(argv[i],"-expect") || !strcmp(argv[i],"-spec") || !strcmp(argv[i],"-p") || !strcmp(argv[i],"-t") || !strcmp(argv[i],"-s") || !strcmp(argv[i],"-body-file") || !strcmp(argv[i],"-update");
            if(value && i+1<argc) args[count++]=argv[++i];
        } else options=false;
    }
    if (debug_only && !json) return self->run(self,program,count,args);
    bool machine=json || !strcmp(cmd,"new") || !strcmp(cmd,"set") || !strcmp(cmd,"check") || !strcmp(cmd,"ready");
    if(help) return (machine || !strcmp(cmd,"ls") || !strcmp(cmd,"find")) ? scheduler_run(self,program,count,args) : self->run(self,program,count,args);
    if ((!strcmp(cmd,"set") || !strcmp(cmd,"check") || !strcmp(cmd,"ready")) && !scheduler_explicit)
        scheduler_error("invalid_arguments", "This command requires explicit -db", NULL);
    if (!scheduler_code && !db && !scheduler_explicit) db=find_relative_tasks_directory();
    if(!db && !scheduler_code) scheduler_error("database_not_found","No tasks database found",NULL);
    if(!scheduler_code && !store_open(&scheduler_store,db,mutation)) scheduler_store_error();
    if (json && scheduler_store.path && !scheduler_utf8(scheduler_store.path, strlen(scheduler_store.path))) {
        free(scheduler_store.path); scheduler_store.path = NULL;
        scheduler_error("unsafe_path", "Database identity must be UTF-8 for JSON", NULL);
    }
    bool ok;
    if(scheduler_code) ok=scheduler_report_error(cmd,json);
    else ok=machine?scheduler_run(self,program,count,args):self->run(self,program,count,args);
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "ERROR: io_error: output failed%s; reread before retry\n", scheduler_store.write_state ? " (write_state: committed)" : "");
        ok = false;
    }
    store_close(&scheduler_store); free(args); return ok;
}

static char *scheduler_create_legacy(const char *dir, const char *suffix, String_View title, int priority, Parsed_Tags tags)
{
    if(!scheduler_line(title)) { fprintf(stderr,"ERROR: invalid title\n"); return NULL; }
    if (!scheduler_suffix_valid(suffix)) { fprintf(stderr,"ERROR: invalid_arguments: invalid task suffix\n"); return NULL; }
    char *id=strdup(temp_new_huid(suffix));
    struct stat st;
    if(fstatat(scheduler_store.fd,id,&st,AT_SYMLINK_NOFOLLOW)==0) { sleep(1); id=strdup(temp_new_huid(suffix)); }
    Task task={.id=id,.title=title,.status=SVLIT("OPEN"),.priority=priority,.parsed_tags=tags};
    properties_put(&task.properties,SVLIT("STATUS"),SVNULL); properties_put(&task.properties,SVLIT("PRIORITY"),SVNULL); properties_put(&task.properties,SVLIT("TAGS"),SVNULL);
    String_Builder bytes={0}; render_task_md(task,&bytes,NULL);
    if(!store_create(&scheduler_store,id,bytes.items,bytes.count)) { fprintf(stderr,"ERROR: %s: %s\n",scheduler_store.code,scheduler_store.error); return NULL; }
    print_task_report(dir,&task,SVNULL); return id;
}
static bool scheduler_legacy_replace(Task *task, const char *dir, const void *bytes, size_t n)
{
    (void)dir;
    char *old=NULL; size_t len=0; struct stat st;
    if(!store_read(&scheduler_store,task->id,&old,&len,&st)) goto fail;
    if(task->task_md_content.count!=len+1 || memcmp(task->task_md_content.data,old,len)) {
        free(old); fprintf(stderr,"ERROR: conflict: %s changed since selection\n",task->id); return false;
    }
    bool ok=store_replace(&scheduler_store,task->id,old,len,&st,bytes,n); free(old);
    if(ok) return true;
fail:
    fprintf(stderr,"ERROR: %s: %s%s\n",scheduler_store.code,scheduler_store.error,scheduler_store.write_state?" (write committed; reread before retry)":"");
    return false;
}
