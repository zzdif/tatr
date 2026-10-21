# Record/replay testing

- STATUS: OPEN
- PRIORITY: 80
- TAGS:

Basically a test case should be a simple script that runs `tatr` commands. We record the state of a sandbox folder after the script and then replay the script and check if the state is expected.

The only problem I see is the Task IDs are based on time. We need to record the outcomes so it because irrelevant somehow.

---

Nothing prevents us from using parts of tatr in the tests like we do in `test_tatr_tag_task_without_tags_property` where we call to `temp_new_huid()`. We can even maybe call to `create_new_task()` in the tests.
