# An ability to actually store `tasks` folder outside of the main repo

- STATUS: OPEN
- PRIORITY: 50
- TAGS: spec,scope

For security purposes.

---

The way our system works (HUIDs being relatively unique identifiers)
means we can actually accumulate info tasks from several folders at
once. Which might enable interesting usecases.

---

Sometimes I want to augment the tasks/ from one project with the
tasks/ of another, because I copy-pasted significant chunks of code
between them and they share relevant issues.

But the exact locations of the tasks/ folders are very much user
specific. So we can't easily commit this information to the git
repos. So this should be something stored in the user config which we
will introduce in TASK(20260901-051338)
