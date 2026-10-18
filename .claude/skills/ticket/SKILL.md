---
name: ticket
description: Create, read, list, and move dotawait tickets in docs/tickets/ (markdown + front matter, auto-maintained index). Use when writing up a feature/bug/limitation as a ticket, triaging the backlog, picking the next ticket to work on, or changing a ticket's status.
---

# Tickets

Tickets live in `docs/tickets/T-NNNN-<slug>.md`. The status flow and
meaning of each field are in `docs/tickets/README.md`. All mutations go
through the script so the index table stays correct:

```sh
S=.claude/skills/ticket/scripts/ticket.py
python3 $S new "<imperative title>" --area transpiler|runtime|cmake|examples|docs|cross-cutting \
                                   --type feature|limitation|bug|chore [--priority P1|P2|P3]
python3 $S list [--status ready]
python3 $S show T-0003
python3 $S status T-0003 in-progress    # proposed|ready|in-progress|in-review|done|rejected
python3 $S index                        # after hand-editing front matter
```

`new` copies `templates/ticket.md` and prints the path. Then fill in every
section of the file with Edit.

## Writing a good ticket

- **Problem** holds observed behavior: a minimal `.async.cpp` input and the
  actual `asynctp` output or error, copied from a real run.
- **Blueprint** names files and functions, gives ordered steps, and states
  which CLAUDE.md invariant is at risk and how the design preserves it.
- **Acceptance criteria** are checkboxes, each naming a test suite or golden
  fixture and the input it covers. Always end with README/header limitation
  updates (if relevant) and `verify.sh all` passes.
- Keep each ticket to one concern that leaves the tree green. Split anything bigger.
- **Log** is append-only, one line per event:
  `YYYY-MM-DD <role>: <what happened>`.

## Dependencies

Hard dependencies go in front matter: `depends_on: [T-0010, T-0004]`
(`depends_on: []` when none). List only tickets that must be `done` before
work starts. `status <id> in-progress` refuses while any is unfinished
(`--force` overrides), and `list` marks blocked tickets.

Picking the next ticket: run `next`. It prints the highest-priority,
lowest-ID `ready` ticket whose dependencies are all `done`, or exits
non-zero explaining what each ready ticket waits on. A `proposed` ticket
must go through the architect before anyone implements it.
