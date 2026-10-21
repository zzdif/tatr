# Crash during `tatr-new`

- STATUS: CLOSED
- PRIORITY: 200
- TAGS: bug

```console
$ tatr new -p 50 -t scope Stream songs from a remote service
[INFO] created directory `./tasks/20260921-112854`
MemorySanitizer:DEADLYSIGNAL
==26268==ERROR: MemorySanitizer: SEGV on unknown address 0x000000000000 (pc 0x55873fd1d0a2 bp 0x7ffd737b6cd0 sp 0x7ffd737b6820 T26268)
==26268==The signal is caused by a WRITE memory access.
==26268==Hint: address points to the zero page.
    #0 0x55873fd1d0a2 in render_task_md /home/rexim/Programming/tsoding/tatr/src/task.c:211:33
    #1 0x55873fd28038 in new_run /home/rexim/Programming/tsoding/tatr/src/tatr.c:514:5
    #2 0x55873fd3c4a8 in main /home/rexim/Programming/tsoding/tatr/src/tatr.c:1038:18
    #3 0x7fc968189bfb in __libc_start_call_main csu/../sysdeps/nptl/libc_start_call_main.h:58:16
    #4 0x7fc968189cb4 in __libc_start_main@GLIBC_2.2.5 csu/../csu/libc-start.c:360:3
    #5 0x55873fc66420 in _start /builddir/glibc-2.41/csu/../sysdeps/x86_64/start.S:115

MemorySanitizer can not provide additional info.
SUMMARY: MemorySanitizer: SEGV /home/rexim/Programming/tsoding/tatr/src/task.c:211:33 in render_task_md
==26268==ABORTING
$ tatr version
tatr commit 7d5934f
Build time: Sun, 20 Sep 2026 16:20:23 +07
Compiler: clang version 18.1.8
```

---

Can't reproduce this in the current version:

```console
$ tatr version
tatr commit f125408
Build time: Fri, 16 Oct 2026 07:16:42 +07
Compiler: clang version 18.1.8
```
