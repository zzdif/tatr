# Hit assertion on sowon2

- STATUS: CLOSED
- PRIORITY: 200
- TAGS: bug

```console
-*- mode: compilation; default-directory: "~/Programming/tsoding/sowon2/" -*-
Compilation started at Fri Oct 16 14:40:29

$ tatr ls -update +aboba not tagged
tatr: src/task.c:203: void render_task_md(Task, Nob_String_Builder *, int *): Assertion `properties_get(&task.properties, SVLIT("TAGS"))' failed.

Compilation aborted at Fri Oct 16 14:40:29, duration 0.03 s
```

```console
tatr version
tatr commit e93298f
Build time: Fri, 16 Oct 2026 13:35:00 +07
Compiler: clang version 18.1.8
```

---

I managed to reproduce this on this exact repo with TASK(20260308-171346):

```console
tatr ls -update +aboba 20260308-171346
tatr: src/task.c:203: void render_task_md(Task, Nob_String_Builder *, int *): Assertion `properties_get(&task.properties, SVLIT("TAGS"))' failed.
```

TASK(20260308-171346) doesn't have a `TAGS` property which I thought would be handled by the parsing process somehow...
