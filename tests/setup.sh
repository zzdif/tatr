#!/bin/sh
# Run from the repository root: sh tests/setup.sh [path/to/tatr]
set -eu
repo=$(pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/bin" "$tmp/project with spaces"
cp "${1:-build/tatr}" "$tmp/bin/tatr"
binary="$tmp/bin/tatr"
project="$tmp/project with spaces"
log="$tmp/output"
# Keep Git fixtures independent of user config, hooks, and repository overrides.
unset GIT_DIR GIT_WORK_TREE GIT_COMMON_DIR GIT_INDEX_FILE
export GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null
ln -s "$(command -v git)" "$tmp/bin/git"

fail() { printf 'FAIL: %s\n' "$*" >&2; tail -40 "$log" >&2; exit 1; }
succeeds() { "$@" >"$log" 2>&1 || fail "expected success: $*"; }
fails() { if "$@" >"$log" 2>&1; then fail "expected failure: $*"; fi; }
contains() { grep -F "$1" "$2" >/dev/null || fail "missing $1 in $2"; }
init_repo() { git -c init.templateDir= -c init.defaultBranch=main init -q "$1"; }
compact_output() {
    test "$(wc -l < "$log")" -le 5 || fail 'setup success output exceeds five lines'
    if grep -E 'CMD:|directory `' "$log" >/dev/null; then fail 'setup leaked internal command/directory chatter'; fi
}
reject_setup_here() {
    for command in nvim-setup skill-setup; do
        fails "$binary" "$command"
        contains 'Setup requires the root of a Git worktree' "$log"
        if grep -F 'Checking Neovim' "$log" >/dev/null; then fail 'probed Neovim before checking Git root'; fi
        test ! -e .nvim.lua && test ! -e .agents && test ! -e .gitignore || fail 'root check wrote files'
    done
}

cd "$project"
succeeds "$binary" skill-setup -help
succeeds "$binary" nvim-setup -help
test ! -e .agents && test ! -e .nvim.lua || fail 'help wrote files'
fails "$binary" skill-setup unexpected
test ! -e .agents || fail 'invalid arguments wrote files'
reject_setup_here
mkdir .git
reject_setup_here # An empty .git directory is not a repository.
rmdir .git
init_repo "$project"
for command in nvim-setup skill-setup; do
    fails env PATH= "$binary" "$command"
    contains 'Setup requires the root of a Git worktree' "$log"
    test ! -e .nvim.lua && test ! -e .agents && test ! -e .gitignore || fail 'missing Git wrote files'
done
mkdir nested
cd nested
reject_setup_here
# An inherited Git directory must not allow setup in a non-root folder.
export GIT_DIR="$project/.git"
reject_setup_here
unset GIT_DIR
cd "$project/.git"
reject_setup_here

git -c init.templateDir= init -q --bare "$tmp/bare"
cd "$tmp/bare"
reject_setup_here
mkdir "$tmp/bare-marker"
git -c init.templateDir= init -q --bare "$tmp/bare-marker/.git"
cd "$tmp/bare-marker"
reject_setup_here

cd "$project"
printf 'existing-rule' > .gitignore
succeeds env PATH="$tmp/bin" "$binary" skill-setup
compact_output
cmp "$repo/skills/tatr/SKILL.md" .agents/skills/tatr/SKILL.md || fail 'skill content'
printf 'existing-rule\n/.agents/skills\n' > "$tmp/expected-ignore"
cmp "$tmp/expected-ignore" .gitignore || fail 'existing ignore contents changed'
contains 'Created .agents/skills/tatr/SKILL.md' "$log"
touch -t 200001010000 .agents/skills/tatr/SKILL.md .gitignore
cp -p .agents/skills/tatr/SKILL.md "$tmp/skill-before-repeat"
cp -p .gitignore "$tmp/ignore-before-repeat"
for attempt in 1 2 3; do
    succeeds "$binary" skill-setup
    compact_output
    contains 'Unchanged .agents/skills/tatr/SKILL.md' "$log"
    cmp "$tmp/expected-ignore" .gitignore || fail 'repeat duplicated ignore entry'
    cmp "$tmp/skill-before-repeat" .agents/skills/tatr/SKILL.md || fail 'repeat changed skill'
    test ! .gitignore -nt "$tmp/ignore-before-repeat" || fail 'repeat rewrote gitignore'
    test ! .agents/skills/tatr/SKILL.md -nt "$tmp/skill-before-repeat" || fail 'repeat rewrote skill'
done
printf '\nPersonal addition\n' >> .agents/skills/tatr/SKILL.md
cp .agents/skills/tatr/SKILL.md "$tmp/personal-skill"
fails "$binary" skill-setup
cmp "$tmp/personal-skill" .agents/skills/tatr/SKILL.md || fail 'personal skill overwritten'
cmp "$tmp/expected-ignore" .gitignore || fail 'conflict changed gitignore'

mkdir "$tmp/outside"
for path in .agents .agents/skills .agents/skills/tatr .agents/skills/tatr/SKILL.md .gitignore; do
    case_dir="$tmp/link-$(printf '%s' "$path" | tr / _)"
    init_repo "$case_dir"
    cd "$case_dir"
    mkdir -p "$(dirname "$path")"
    ln -s "$tmp/outside" "$path"
    fails "$binary" skill-setup
    contains 'Refusing' "$log"
    test -z "$(ls -A "$tmp/outside")" || fail 'wrote through symlink'
done
init_repo "$tmp/dangling"
cd "$tmp/dangling"
ln -s "$tmp/missing" .gitignore
fails "$binary" skill-setup
test ! -e .agents || fail 'created directories before invalid gitignore check'
test ! -e "$tmp/missing" || fail 'followed dangling symlink'

init_repo "$tmp/hardlink"
cd "$tmp/hardlink"
printf 'outside contents\n' > "$tmp/shared-ignore"
ln "$tmp/shared-ignore" .gitignore
fails "$binary" skill-setup
test ! -e .agents || fail 'created directories before hardlink check'
printf 'outside contents\n' > "$tmp/expected-shared"
cmp "$tmp/expected-shared" "$tmp/shared-ignore" || fail 'modified hardlinked file'

# Linked worktrees have a .git file rather than a .git directory.
git -C "$project" -c user.name=Test -c user.email=test@example.invalid \
    -c core.hooksPath=/dev/null -c commit.gpgsign=false commit -q --allow-empty -m fixture
git -C "$project" -c core.hooksPath=/dev/null worktree add -q --detach "$tmp/worktree with spaces"
cd "$tmp/worktree with spaces"
test -f .git || fail 'worktree fixture has no .git file'
succeeds "$binary" skill-setup
cmp "$repo/skills/tatr/SKILL.md" .agents/skills/tatr/SKILL.md || fail 'worktree skill setup'
succeeds "$binary" skill-setup

init_repo "$tmp/no-nvim"
cd "$tmp/no-nvim"
fails env PATH="$tmp/bin" "$binary" nvim-setup
contains 'vim.o.exrc = true' "$log"
test ! -e .nvim.lua && test ! -e .gitignore || fail 'missing nvim wrote files'
printf 'PASS: Git-root gate, linked worktrees, skill setup, idempotence, conflicts, links, missing Neovim\n'

if ! command -v nvim >/dev/null 2>&1; then
    printf 'SKIP: Neovim integration tests (nvim not installed)\n'
    exit 0
fi

# No real user config, plugins, trust database, or editor state is changed.
unset VIMINIT EXINIT MYVIMRC
export XDG_CONFIG_HOME="$tmp/config" XDG_CONFIG_DIRS="$tmp/config-dirs" XDG_DATA_HOME="$tmp/data"
export XDG_STATE_HOME="$tmp/state" XDG_CACHE_HOME="$tmp/cache"
export NVIM_APPNAME=tatr-setup-test
export PATH="$tmp/bin:$PATH"
mkdir -p "$XDG_CONFIG_HOME/$NVIM_APPNAME"
config="$XDG_CONFIG_HOME/$NVIM_APPNAME/init.lua"
printf '%s\n' '-- vim.o.exrc = true is only a comment' 'vim.o.exrc = false' > "$config"
init_repo "$tmp/nvim disabled"
cd "$tmp/nvim disabled"
printf 'keep-me\n' > .gitignore
succeeds "$binary" nvim-setup -help
test ! -e .nvim.lua || fail 'nvim help wrote config'
fails "$binary" nvim-setup
contains 'vim.o.exrc = true' "$log"
test ! -e .nvim.lua || fail 'disabled exrc installed config'
printf 'keep-me\n' > "$tmp/expected-ignore"
cmp "$tmp/expected-ignore" .gitignore || fail 'disabled exrc changed gitignore'

printf '%s\n' 'vim.o.exrc = true' 'error("BROKEN_STARTUP")' > "$config"
fails "$binary" nvim-setup
contains 'BROKEN_STARTUP' "$log"
test ! -e .nvim.lua || fail 'broken user config passed the gate'
cmp "$tmp/expected-ignore" .gitignore || fail 'broken user config changed gitignore'

# Check the actual option, not a literal source-code spelling.
printf 'vim.opt.exrc = true\n' > "$config"
cp "$config" "$tmp/expected-user-config"
succeeds "$binary" nvim-setup
compact_output
cmp "$repo/contrib/nvim/tatr.lua" .nvim.lua || fail 'nvim template content'
cmp "$tmp/expected-user-config" "$config" || fail 'modified user configuration'
printf 'keep-me\n/.nvim.lua\n' > "$tmp/expected-ignore"
cmp "$tmp/expected-ignore" .gitignore || fail 'nvim ignore entry'
touch -t 200001010000 .nvim.lua .gitignore
cp -p .nvim.lua "$tmp/nvim-before-repeat"
cp -p .gitignore "$tmp/nvim-ignore-before-repeat"
for attempt in 1 2 3; do
    succeeds "$binary" nvim-setup
    compact_output
    contains 'Unchanged .nvim.lua' "$log"
    cmp "$tmp/expected-ignore" .gitignore || fail 'repeat nvim setup changed gitignore'
    cmp "$tmp/nvim-before-repeat" .nvim.lua || fail 'repeat changed nvim config'
    test ! .gitignore -nt "$tmp/nvim-ignore-before-repeat" || fail 'repeat rewrote gitignore'
    test ! .nvim.lua -nt "$tmp/nvim-before-repeat" || fail 'repeat rewrote nvim config'
done
succeeds "$binary" skill-setup
printf 'keep-me\n/.nvim.lua\n/.agents/skills\n' > "$tmp/expected-ignore"
succeeds "$binary" nvim-setup
succeeds "$binary" skill-setup
cmp "$tmp/expected-ignore" .gitignore || fail 'combined setup duplicated ignore entries'
test ! -f "$XDG_STATE_HOME/$NVIM_APPNAME/trust" || fail 'setup modified trust'

# A project config must not be executed by the exrc probe.
printf 'error("PROJECT_CONFIG_MUST_NOT_RUN")\n' >> .nvim.lua
cp .nvim.lua "$tmp/personal-nvim"
fails "$binary" nvim-setup
contains 'differs from the bundled template' "$log"
if grep -F 'PROJECT_CONFIG_MUST_NOT_RUN' "$log" >/dev/null; then fail 'probe loaded project config'; fi
cmp "$tmp/personal-nvim" .nvim.lua || fail 'overwrote local nvim config'
cp "$repo/contrib/nvim/tatr.lua" .nvim.lua

succeeds "$binary" init
cmp "$repo/tasks/README.md" tasks/README.md || fail 'embedded tasks README changed'
succeeds "$binary" new -t bug Quickfix fixture
# Explicitly source the reviewed test template; never change Neovim's trust list.
succeeds env TATR_TEST_LUA="$repo/tests/nvim-setup.lua" nvim --headless -u NORC -i NONE \
    -c 'lua dofile(vim.env.TATR_TEST_LUA)' -c 'cquit 1'
succeeds env TATR_TEST_LUA="$repo/tests/nvim-setup.lua" TATR_TEST_MISSING=1 nvim --headless -u NONE -i NONE \
    -c 'lua dofile(vim.env.TATR_TEST_LUA)' -c 'cquit 1'

init_repo "$tmp/nvim linked"
cd "$tmp/nvim linked"
ln -s "$tmp/missing-config" .nvim.lua
fails "$binary" nvim-setup
test ! -e .gitignore && test ! -e "$tmp/missing-config" || fail 'nvim setup followed link'
printf 'PASS: exrc gate, installed template, file preservation, task/folder navigation, missing tatr\n'
