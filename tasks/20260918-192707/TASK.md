# Task sorting by priority is doing a very stupid thing.

- STATUS: CLOSED
- PRIORITY: 80
- TAGS:

Grep refs.

What we are doing is essentially on each comparison during sorting converting 2 strings to 2 integers.
We need to cache the integers, but then we are introducing cache invalidation problems back.

---

We basically reverted back from TASK(20260913-063421), but at least we discovered some useful things along the way.
