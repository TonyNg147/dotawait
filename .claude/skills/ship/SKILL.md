---
name: ship
description: Run the full architect -> developer -> tester loop on one dotawait ticket using the project subagents, iterating until the tester passes it. Invoke as /ship T-NNNN (or /ship next to take the top ready ticket).
disable-model-invocation: true
argument-hint: T-NNNN | next
---

# Ship a ticket

Target: `$ARGUMENTS`. You are the orchestrator. Delegate each role to its
subagent and don't do their work yourself. Your job is to pass context
between them and to stop when a human decision is needed.

1. **Resolve the ticket.** For `next`, run
   `python3 .claude/skills/ticket/scripts/ticket.py next`. It skips tickets
   whose `depends_on` aren't `done`. If it exits non-zero, **stop** and show
   what each ready ticket is waiting on. For an explicit ID, check
   `ticket.py show` for `depends_on`. If any dependency isn't `done`,
   **stop** and ask before going on. Show the ticket.
2. **Architect** (only if status is `proposed`): ask the `architect`
   subagent to refine it to `ready`. If it comes back still `proposed` with
   open questions or a design choice, **stop**, show the person the
   options and the architect's recommendation, and wait for their answer.
3. **Developer:** ask the `developer` subagent to implement the ticket ID.
   Pass only the ticket ID plus, on later rounds, the tester's failure list
   verbatim. Expect status `in-review` and a green `verify.sh full`. If the
   developer reports that the blueprint doesn't match the code, go back to
   the architect with that report.
4. **Tester:** ask the `tester` subagent to verify the ticket ID. Don't
   pass along the developer's explanation. The tester must judge the diff
   on its own.
5. **Loop:** on `VERDICT: FAIL`, send the failures to the developer
   (step 3). Stop after 3 failed rounds and report what keeps failing.
6. **Finish:** on PASS, show the person:
   - the ticket ID and title, with final status
   - `git diff --stat`
   - tests added (developer + tester)
   - out-of-scope findings from the tester. Offer to have the architect
     file them as `proposed` tickets.

   Don't commit unless asked. If asked, use the message
   `T-NNNN: <ticket title>`.
