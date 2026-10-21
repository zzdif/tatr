#!/usr/bin/env bash
# Retain logs and a source manifest; never copy or operate on real task records.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd -P)
evidence=$(mktemp -d "${TMPDIR:-/tmp}/tatr-scheduler-proof.XXXXXX")
evidence=$(cd "$evidence" && pwd -P)
cp -R "$root/src" "$root/thirdparty" "$root/tests" "$root/skills" "$root/contrib" "$evidence/"
cp "$root/nob.c" "$evidence/"
mkdir "$evidence/tasks"
cp "$root/tasks/README.md" "$evidence/tasks/"
cd "$evidence"
python3 - <<'PY' > source-manifest.sha256
from pathlib import Path
import hashlib
paths = [Path('nob.c')]
for folder in ['src', 'thirdparty', 'tests', 'skills', 'contrib', 'tasks']:
    paths.extend(p for p in Path(folder).rglob('*') if p.is_file())
for p in sorted(paths):
    print(hashlib.sha256(p.read_bytes()).hexdigest(), p)
PY
{
    uname -a
    cc --version
    python3 --version
    cc -o nob nob.c
    ./nob -test
    cc -fsanitize=address,undefined -fno-omit-frame-pointer -g -Ithirdparty -Ibuild src/tatr.c -o build/tatr-sanitize
    ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tests/scheduler.py build/tatr-sanitize
    cc -fsanitize=address,undefined -fno-omit-frame-pointer -g -DSTORE_TESTING tests/store.c src/store.c -o build/store-sanitize
    build/store-sanitize
    cc -fsanitize=address,undefined -fno-omit-frame-pointer -g tests/hash.c thirdparty/sha256.c -o build/hash-sanitize
    build/hash-sanitize
} > native.log 2>&1 || { tail -60 native.log; echo "FAILED; evidence: $evidence"; exit 1; }
tail -22 native.log
if [[ ${1:-} == --linux ]]; then
    # A pinned, preinstalled official image; no network, privileged access or host bind mounts.
    image='gcc@sha256:5e927c284bf55a7dc796262e311a0703344f62f41f5621eb56843111b1d37e15'
    docker image inspect "$image" >/dev/null
    python3 - <<'PY' | docker run -i --rm --network none --read-only --cap-drop ALL \
        --security-opt no-new-privileges --tmpfs /work:exec,size=256m --tmpfs /tmp:exec,size=64m \
        -w /work "$image" sh -c 'tar --no-same-owner -xf - && uname -a && cc --version && python3 --version && cc -o nob nob.c && ./nob -test && cc -fsanitize=address,undefined -fno-omit-frame-pointer -g -Ithirdparty -Ibuild src/tatr.c -o build/tatr-sanitize && ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tests/scheduler.py build/tatr-sanitize && cc -fsanitize=address,undefined -fno-omit-frame-pointer -g -DSTORE_TESTING tests/store.c src/store.c -o build/store-sanitize && ./build/store-sanitize && cc -fsanitize=address,undefined -g tests/hash.c thirdparty/sha256.c -o build/hash-sanitize && ./build/hash-sanitize' > linux.log 2>&1 || { tail -60 linux.log; echo "FAILED; evidence: $evidence"; exit 1; }
import sys, tarfile
with tarfile.open(fileobj=sys.stdout.buffer, mode='w|') as archive:
    for name in ['src', 'thirdparty', 'tests', 'skills', 'contrib', 'tasks', 'nob.c']:
        archive.add(name)
PY
    tail -22 linux.log
fi
printf 'PASS; retained evidence and source checksums: %s\n' "$evidence"
