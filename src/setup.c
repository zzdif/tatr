// Project-local editor and agent setup. Included by tatr.c.

// Check existing paths with lstat so setup never follows a pre-existing link.
static bool setup_check_path(const char *path, bool directory, bool *exists)
{
    struct stat st;
    if (lstat(path, &st) < 0) {
        *exists = false;
        if (errno == ENOENT) return true;
        nob_log(ERROR, "Could not inspect %s: %s", path, strerror(errno));
        return false;
    }
    *exists = true;
    if (directory ? S_ISDIR(st.st_mode) : (S_ISREG(st.st_mode) && st.st_nlink == 1)) return true;
    nob_log(ERROR, "Refusing %s: expected an ordinary %s, not a link or special file", path, directory ? "directory" : "file");
    return false;
}

static bool setup_write_file(const char *path, const void *data, size_t size, bool append)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_NOFOLLOW | (append ? O_APPEND : O_EXCL), 0666);
    if (fd < 0) {
        nob_log(ERROR, "Could not open %s: %s", path, strerror(errno));
        return false;
    }
    FILE *file = fdopen(fd, append ? "ab" : "wb");
    if (!file) {
        nob_log(ERROR, "Could not open stream for %s: %s", path, strerror(errno));
        close(fd);
        return false;
    }
    bool ok = fwrite(data, 1, size, file) == size;
    if (fclose(file) != 0) ok = false;
    if (!ok) nob_log(ERROR, "Could not finish writing %s; inspect the file before retrying", path);
    return ok;
}

bool setup_run(Command *self, const char *program_name, int argc, char **argv)
{
    bool help = false;
    void *c = flag_c_new(program_name);
    flag_c_bool_var(c, &help, "help", false, "Print this help message");
    if (!flag_c_parse(c, argc, argv)) {
        print_command_usage(self, program_name, c);
        flag_c_print_error(c, stderr);
        return false;
    }
    if (help) {
        print_command_usage(self, program_name, c);
        return true;
    }
    if (flag_c_rest_argc(c) != 0) {
        print_command_usage(self, program_name, c);
        nob_log(ERROR, "Run %s from the target project root; no positional arguments are accepted", self->name);
        return false;
    }

    const char *cwd = get_current_dir_temp();
    if (!cwd) return false;
    nob_log(INFO, "Project: %s", cwd);
    // Pin local metadata rather than accepting an ancestor repo or inherited GIT_DIR.
    // This fixed command contains no user input; .git may be a directory or a worktree file.
    FILE *git = popen("git --git-dir=.git rev-parse --is-inside-work-tree --show-prefix", "r");
    bool at_root = false;
    if (git) {
        const char expected[] = "true\n\n"; // Inside a worktree, with no subdirectory prefix.
        char output[sizeof(expected)];
        size_t count = fread(output, 1, sizeof(output), git);
        int status = pclose(git);
        at_root = status == 0 && count == sizeof(expected) - 1 && memcmp(output, expected, count) == 0;
    }
    if (!at_root) {
        nob_log(ERROR, "Setup requires the root of a Git worktree and git on PATH. No files changed.");
        return false;
    }

    bool nvim = strcmp(self->name, "nvim-setup") == 0;
    if (nvim) {
        nob_log(INFO, "Checking Neovim configuration...");
        Cmd cmd = {0};
        // Start outside the project: do not load/trust its local config as part of this check.
        cmd_append(&cmd, "nvim", "--headless", "-n", "-i", "NONE", "--cmd", "cd /",
                   "-c", "lua if vim.fn.has('nvim-0.9') == 1 and vim.o.exrc and vim.v.errmsg == '' then vim.cmd('qa!') else vim.cmd('cquit 1') end");
        Log_Level saved_level = nob_minimal_log_level;
        if (nob_minimal_log_level < WARNING) nob_minimal_log_level = WARNING;
        bool ok = cmd_run(&cmd, .stdin_path = "/dev/null");
        nob_minimal_log_level = saved_level;
        free(cmd.items);
        if (!ok) {
            nob_log(ERROR, "Neovim check failed. Requires Neovim 0.9+ with exrc enabled.");
            nob_log(INFO, "Set vim.o.exrc = true in your Neovim config; fix any startup errors and retry.");
            nob_log(INFO, "No project files changed.");
            return false;
        }
    }

    const char *target = nvim ? ".nvim.lua" : ".agents/skills/tatr/SKILL.md";
    const unsigned char *content = nvim ? nvim_config : agent_skill;
    size_t size = nvim ? ARRAY_LEN(nvim_config) : ARRAY_LEN(agent_skill);
    const char *ignore = nvim ? "/.nvim.lua" : "/.agents/skills";
    const char *dirs[] = {".agents", ".agents/skills", ".agents/skills/tatr"};
    bool exists = false;

    // Preflight all destinations before creating anything; preserve user-owned files.
    if (!nvim) {
        for (size_t i = 0; i < ARRAY_LEN(dirs); ++i) {
            if (!setup_check_path(dirs[i], true, &exists)) return false;
        }
    }
    bool target_exists = false;
    if (!setup_check_path(target, false, &target_exists)) return false;
    if (target_exists) {
        String_Builder current = {0};
        bool ok = read_entire_file(target, &current);
        bool matches = ok && current.count == size && memcmp(current.items, content, size) == 0;
        free(current.items);
        if (!ok) return false;
        if (!matches) {
            nob_log(ERROR, "Preserving %s: differs from the bundled template.", target);
            nob_log(INFO, "Merge it manually or move it aside and retry. No files changed.");
            return false;
        }
    }

    if (!setup_check_path(".gitignore", false, &exists)) return false;
    String_Builder gitignore = {0};
    if (exists && !read_entire_file(".gitignore", &gitignore)) {
        free(gitignore.items);
        return false;
    }
    bool ignored = false;
    String_View lines = sb_to_sv(gitignore);
    while (lines.count > 0) {
        if (sv_eq(sv_trim(sv_chop_by_delim(&lines, '\n')), sv_from_cstr(ignore))) ignored = true;
    }
    bool needs_newline = gitignore.count > 0 && da_last(&gitignore) != '\n';
    free(gitignore.items);

    if (!nvim) {
        Log_Level saved_level = nob_minimal_log_level;
        if (nob_minimal_log_level < WARNING) nob_minimal_log_level = WARNING;
        bool ok = true;
        for (size_t i = 0; ok && i < ARRAY_LEN(dirs); ++i) {
            ok = mkdir_if_not_exists(dirs[i]);
        }
        nob_minimal_log_level = saved_level;
        if (!ok) return false;
    }
    if (target_exists) {
        nob_log(INFO, "Unchanged %s", target);
    } else {
        if (!setup_write_file(target, content, size, false)) return false;
        nob_log(INFO, "Created %s", target);
    }
    if (ignored) {
        nob_log(INFO, "Unchanged .gitignore (%s)", ignore);
    } else {
        const char *entry = temp_sprintf("%s%s\n", needs_newline ? "\n" : "", ignore);
        if (!setup_write_file(".gitignore", entry, strlen(entry), exists)) return false;
        nob_log(INFO, "Updated .gitignore (+%s)", ignore);
    }
    if (nvim) {
        nob_log(INFO, "Next: review .nvim.lua; open Neovim, approve trust, then :Tasks.");
    } else {
        nob_log(INFO, "Next: restart your project agent (Pi: /skill:tatr).");
    }
    return true;
}
