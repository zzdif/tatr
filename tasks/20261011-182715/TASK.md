# The update language

- STATUS: CLOSED
- PRIORITY: 100
- TAGS:

Instead of the zoo of commands like `tatr-untag`, `tatr-tag`, just have the Update Language which supports things like `-scope,+bug,close`. Comma separated list of actions to perform on the task.

---

Some design ideas:

```c
$ tatr ls -update -scope,close :scope and :bug
$ tatr update -untag scope -close :scope and :bug
```

---

I don't like the `-untag`, `-close`, etc flags because they go through `flag.h` and thus we lose their order. And order is kinda important for when we do `-tag,+tag`. Do we first remove the tag or add it? Depending on the order the result might be different.
