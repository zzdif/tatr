# Scheduler implementation: contract and reproducible evidence

## Baseline and scope

Implemented as uncommitted working-tree changes on baseline
`f26bcf3350579642a38014e92329f8045e96453f`, with no initial source drift.
No commits, pushes, installation, activation, tracker migration, real-ticket execution,
real task-record edits or attachment changes were performed.

Native implementation paths:

- `src/scheduler.c`: strict records, JSON v1, property mutation, graph checks/readiness,
  scope snapshots, command-local dispatch and database selection.
- `src/store.h`, `src/store.c`: safe descriptor-based access, directory advisory locks,
  exclusive creation, guarded atomic replacement and recovery diagnostics.
- `thirdparty/sha256.h`, `thirdparty/sha256.c`: native RFC 6234-derived SHA-256 with
  retained BSD license; no crypto executable or dynamic runtime dependency.
- `src/tatr.c`, `src/task.c`, `src/md.c`, `src/huid.c`, `src/query.c`, `src/update.c`:
  integration with existing CLI, parser, safe task writers, checked creation inputs,
  legacy reopen semantics, CRLF handling and quiet JSON query diagnostics.
- `nob.c`, `tests/hash.c`, `tests/store.c`, `tests/scheduler.py`,
  `tests/verify-scheduler.sh`: existing-harness integration and disposable verification.
- `README.md`, `skills/tatr/SKILL.md`: exact interface, safeguards and inert graph example.

No controller, claims/leases, worktrees, agents, review/integration runner, GitHub
synchronization or deployment automation is implemented.

## What the evidence proves—and does not

These are executable assertions against native binaries and actual files/processes,
not mocked success reports. A test pass demonstrates the recorded cases on the listed
platform/filesystem. It is not mathematical proof of every possible input, race,
filesystem, crash or hardware failure. Source checksums bind the logs to tested inputs;
they are not signatures or an independent attestation.

Final retained artifacts:

- [macOS raw log](evidence/scheduler/native.log)
- [Linux raw log](evidence/scheduler/linux.log)
- [tested-source SHA-256 manifest](evidence/scheduler/source-manifest.sha256)

Artifact SHA-256 digests:

```text
a71ab1ae20a201c205c313b6b2bb52ec4768ad6aad95a7992769b9fbd3293b16  native.log
784fe2362deaa2b98849f19819c7f1e210722c6abf52b3a4e3b25783ee890ab5  linux.log
f1393ab66639c55c437dcaa25c1b8ffaefa4fd63a67831e5e85a6d014d88b27c  source-manifest.sha256
```

The manifest covers the native/test sources used in the run, bundled skill/templates
and the copied `tasks/README.md`. This evidence document, retained log artifacts and
main README prose are not compiled inputs and are not in that manifest. The manifest was checked against the current
checkout after the final run.

## Actual final runs

| Platform | Toolchain/filesystem | Results |
| --- | --- | --- |
| macOS/Darwin 25.6.0 arm64 | Apple clang 21.0.0; Python 3.14.8; local temporary directories | Existing tests and Neovim integration passed; native hash/store tests passed; **24 black-box contract groups passed**; ASan+UBSan native hash/store plus **21 sanitizer CLI groups passed** |
| Linux 6.8.0-64-generic aarch64 container | GCC 14.3.0; Python 3.11.2; Linux tmpfs | Existing native/CLI/setup tests passed; native hash/store tests passed; **24 black-box contract groups passed**; ASan+UBSan native hash/store plus **21 sanitizer CLI groups passed**; Neovim integration explicitly skipped because nvim is absent |

The three extra ordinary-run groups use separately compiled deterministic fault
binaries: pre/post-rename JSON errors, generated-ID incoming draft validation, and
partial bulk replacement failure. Sanitizer runs exercise the normal binary plus
native store failure tests; they do not repeat those three fault-binary groups.

CLI leak detection is disabled (`ASAN_OPTIONS=detect_leaks=0`): tatr uses process-lifetime
allocations, including existing temp/parsed structures. Address and undefined-behavior
checks remain enabled; UBSan is configured to halt on errors. This is not a leak audit.

The Linux image is the official GCC image pinned to:
`gcc@sha256:5e927c284bf55a7dc796262e311a0703344f62f41f5621eb56843111b1d37e15`.
Tests run with no network, read-only container root, all capabilities dropped,
no-new-privileges, disposable tmpfs, and no host bind mounts. An archive transfers
only the disposable source copy. No Docker socket or credentials enter the container.

The copied source deliberately has no `.git`; the existing build script logs a failed
optional Git-version probe and explicitly continues without the version hash. That
warning is expected, not a hidden test failure. Final C builds emitted no compiler
warnings. All real task records remain outside the test copies.

## Acceptance evidence map

The A-numbers refer to the implementation handoff. A row maps observed subcases, not
an assertion that every permutation of its prose has been exhaustively tested.

| Acceptance | Observable assertions and source |
| --- | --- |
| A01 | Existing `./nob -test` regressions, setup/Neovim tests; text query reports and `find -path-only`; both help spellings identical; legacy debug works without a database. Linux Neovim gap noted above. |
| A02 | Commands from a different cwd target a spaced explicit database; nonexistent paths never fall back; relative/parent-component paths report the correct absolute identity. |
| A03 | Python `json.loads` consumes each complete stdout; success stderr is empty; empty arrays, Unicode, quotes, backslashes, controls, nulls, errors, invalid combinations and database-free help are asserted. Non-UTF-8 database identity is rejected on Linux; macOS refuses creation of that filename at the filesystem boundary. |
| A04 | Independent Python `hashlib` checks raw task and snapshot hashes, repeated reads, whitespace/final-newline changes and CRLF. Native KATs cover empty, `abc`, 56-byte padding boundary and million-`a` vectors. |
| A05 | Missing, malformed, duplicate, self and empty-element dependencies fail; the cycle diagnostic is asserted as concrete `[A,D,B,A]`; body prose is preserved rather than treated as graph edges. |
| A06 | Unrelated legacy records may omit canonical fields, duplicate non-membership properties or have invalid body encoding; scope checks still work. Missing TASK.md or a NUL that prevents header classification in a discoverable HUID directory fails rather than yielding false completion. |
| A07 | A diamond releases its middle nodes only after the upstream node is DONE, and its leaf only after both middle nodes are DONE. |
| A08 | Cross-spec prerequisite IDs/reasons are returned but never scheduled; container dependency targets are rejected; a fixed-time test proves creation cannot activate an invalid dangling container/dependency draft. |
| A09 | CANCELLED and unspecified closure do not satisfy dependents; OPEN+resolution is rejected; a closed root does not complete open children. |
| A10 | Reopening upstream invalidates closed-DONE downstream nodes, which remain explained in blocked output even with another ready node. Ready/blocked/complete/empty are distinguished. |
| A11 | Reverse-created directories still yield ascending-HUID priority ties; JSON primary/reversed/ID orders are checked; signed-32-bit extremes work and overflow/junk fail. |
| A12 | Duplicate critical headers and case/near-match scheduling keys fail before collapse; malformed UTF-8/NUL fail; syntactically valid dangling references are readable as drafts but fail graph validation. |
| A13 | Atomic STATUS+RESOLUTION, reopen clearing, contradictory outcome rejection, cycle rejection without writes, unknown values containing `=`, exact body/attachment preservation and typed no-op bytes/mtime are asserted. |
| A14 | Stale tokens preserve exact bytes. A pipe barrier launches two real CLI writers with one token: exactly one changes the task; the other reports busy/conflict, and replay of the old token conflicts. No sleep-based race assertion. |
| A15 | Shared locks permit reads but block mutation; an exclusive holder yields prompt busy for reads. Read-only enumeration/reads preserve directory/task mtimes and create no artifacts. A pipe-coordinated raw reader observes only complete 32KB old/new files across six CLI replacements. |
| A16 | Native duplicate-directory reservation cannot overwrite the original. Unsafe/empty suffixes, title/property injection and duplicate creation properties fail without publishing. A test-only fixed UTC clock makes incoming-draft creation validation deterministic. |
| A17 | Database/task-directory/file symlinks, hardlinked TASK.md and FIFO TASK.md are refused. Permission bits survive replacement; new publication respects a restrictive umask. |
| A18 | Native and black-box compile-time injections prove pre-rename byte preservation/temp cleanup and post-rename committed state plus fresh-read reconciliation. A kernel RLIMIT_FSIZE failure proves creation cleanup. A pipe barrier forces broken stdout after commit: exit 1, bounded committed stderr diagnostic and fresh-read confirmation are asserted. |
| A19 | Legacy TUL reopen clears DONE; close does not invent it; tag updates preserve relationships. An injected second replacement failure reports one completed ID, the failed ID and unchanged remainder, with no blind rollback. Existing todo regressions exercise protected creation/source rewriting. |
| A20 | An independent snapshot hash matches the documented concatenation. Membership addition/removal changes/restores it; unrelated identifiable edits do not affect it. Dependency closure/grouping roots appear among inspected records. |
| A21 | Directory-fd locks, atomic replacement, permission preservation, cleanup, JSON and help tests ran separately on macOS and Linux. Linux filesystem coverage is tmpfs, not every Linux filesystem or storage device. |

## Reproduce without touching project tickets

From this checkout:

```sh
# Native platform: retains logs and checksummed inputs in an owned temporary copy.
tests/verify-scheduler.sh

# Optional Linux verification, with Docker and the pinned image already available.
# The implementation session pulled the official gcc:14-bookworm image once.
tests/verify-scheduler.sh --linux
```

The script copies only sources/templates/tests and `tasks/README.md`, **not real task
records**. It prints the retained evidence directory. `python3` is test-only; the native
CLI does not invoke Python, a hash command, a lock service or a package manager.

Verify the recorded tested-source manifest against the checkout independently:

```sh
python3 - <<'PY'
from pathlib import Path
import hashlib
for line in Path('tests/evidence/scheduler/source-manifest.sha256').read_text().splitlines():
    expected, name = line.split(' ', 1)
    assert hashlib.sha256(Path(name).read_bytes()).hexdigest() == expected, name
print('All tested-source checksums match')
PY
```

`./nob -test` also integrates the added tests, but writes build artifacts beneath its
cwd. Use the verification script when isolation from the source checkout is desired.
The fixed-clock and failure controls exist only in separately compiled test binaries;
production builds have neither a runtime fault option nor a fixed clock.

## Deliberate clarifications and remaining limits

The proposed command names, flags, v1 envelope, metadata and CLOSED+DONE completion
policy are retained. Implementation clarifications, not hidden controller behavior:

- Native filename components cap generated suffixes at 239 bytes/full HUIDs at 255.
- CRLF headers are accepted and raw bytes remain the fingerprint authority.
- Ordinary relationship-free creation tolerates unrelated incomplete records, but
  scans safely readable headers for incoming references to its generated candidate;
  newly affected graphs are validated before publication.
- Human check/ready output is concise; full inspected records and blocking explanations
  are in JSON. Legacy Graphviz semantics and TQL grammar are unchanged.
- A broken stdout physically cannot carry a reliable JSON envelope. Output errors
  instead return nonzero and bounded stderr, explicitly identifying a committed write
  when known. The deterministic broken-pipe test covers this exception.
- Interactive todo holds its exclusive mutation lock while prompting; another process
  returns busy rather than waits. Source rewriting is still outside the task transaction.

No power-loss/reboot test, forced process-death recovery campaign, network filesystem,
Windows, x86 Linux, independent-host lock test, huge/deep-graph stress test or performance
benchmark was run. In particular, graph traversal uses native recursion; unbounded-depth
inputs are not a supported scale claim. Not every allocation, close, sync or rename
failure is independently injected. Pre/post-rename injections exercise the recovery
boundaries, not the physical reliability of a storage device.

Quiescent noncooperating editors are still required for unattended writes. There is no
filesystem-wide compare-and-swap, distributed lock, multi-task transaction, signature or
hostile same-user isolation. A stale fingerprint is a pause/reconcile condition;
post-commit errors require a fresh read, not blind replay or rollback.

The inert end-to-end graph sequence is in the README's **Native scheduler interface**
section. The external controller, actual parallel development flow, integration, PR
publication, installation and deployment remain outside this deliverable.
