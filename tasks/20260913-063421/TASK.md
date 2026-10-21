# `append_task_md_content()` doesn't take into account `Task.body`

- STATUS: CLOSED
- PRIORITY: 110
- TAGS: bug

Furthermore, looks like `append_task_md_content()` duplicates functionality of `render_task_md()`. We should probably just remove `append_task_md_content()` and use `render_task_md()` instead.

---

In the whole `TASK.md` updating/rendering system there is an important invariant that is never really checked anywhere which is `TAGS`, `STATUS`, and `PRIORITY` must be always present in `Task.properties`. We shall resolve that in the scope of this task somehow (not really sure how yet).

---

I feel like I'm encountering the same property syncing problem as I had in TASK(20260828-211200). When I'm constructing the `Task` structure not from a `TASK.md` file I need to duplicate the `Task.tags`, `Task.status` and `Task.priority` in `Task.properties` which requires allocating separate memory on the heap (in case of `TASK.md` all that memory is stored within the buffer which we read the file into). And it's generally feels really fragile. I feel like we are just using a wrong data structure here.

Plus `Task.task_md_content` just doesn't make any sense when you are constructuing the `Task` structure not from a `TASK.md` file. Another red flag.

---

`Task.task_md_content` is needed for that freaking `tatr-graph` command 9_9. Let's keep it for now.
