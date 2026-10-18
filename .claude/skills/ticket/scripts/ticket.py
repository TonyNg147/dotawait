#!/usr/bin/env python3
"""Manage the markdown tickets in docs/tickets/ (stdlib only).

  ticket.py new "<title>" --area transpiler --type limitation [--priority P2]
  ticket.py list [--status ready]
  ticket.py next                  # top ready ticket whose depends_on are all done
  ticket.py show T-0003
  ticket.py status T-0003 in-progress [--force]
  ticket.py index                 # rewrite the table in docs/tickets/README.md

Dependencies are a front matter line `depends_on: [T-0010, T-0004]`. A
ticket can't move to in-progress until every dependency is done.

Every command that changes a ticket also regenerates the index.
"""

import argparse
import re
import sys
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
TICKETS = ROOT / "docs" / "tickets"
INDEX = TICKETS / "README.md"
TEMPLATE = Path(__file__).resolve().parents[1] / "templates" / "ticket.md"

STATUSES = ["proposed", "ready", "in-progress", "in-review", "done", "rejected"]
AREAS = ["transpiler", "runtime", "cmake", "examples", "docs", "cross-cutting"]
TYPES = ["feature", "limitation", "bug", "chore"]
PRIORITIES = ["P1", "P2", "P3"]
BEGIN, END = "<!-- BEGIN TICKET INDEX -->", "<!-- END TICKET INDEX -->"
FRONT = re.compile(r"\A---\n(.*?)\n---\n", re.S)


def parse(path):
    text = path.read_text()
    m = FRONT.match(text)
    if not m:
        sys.exit(f"{path}: missing front matter")
    meta = {}
    for line in m.group(1).splitlines():
        key, _, value = line.partition(":")
        meta[key.strip()] = value.strip().strip('"')
    return meta, text


def all_tickets():
    out = []
    for p in sorted(TICKETS.glob("T-[0-9][0-9][0-9][0-9]-*.md")):
        meta, _ = parse(p)
        out.append((p, meta))
    return out


def find(ticket_id):
    for p, meta in all_tickets():
        if meta.get("id") == ticket_id:
            return p, meta
    sys.exit(f"no ticket {ticket_id}")


def deps(meta):
    value = meta.get("depends_on", "").strip()
    if not value:
        return []
    if not (value.startswith("[") and value.endswith("]")):
        sys.exit(f"{meta['id']}: depends_on must look like [T-0001, T-0002], got {value!r}")
    ids = [d.strip() for d in value[1:-1].split(",") if d.strip()]
    for d in ids:
        if not re.fullmatch(r"T-\d{4}", d):
            sys.exit(f"{meta['id']}: bad dependency id {d!r}")
    return ids


def unmet_deps(meta, by_id):
    """Dependencies of `meta` that are not done. Unknown IDs count as unmet."""
    out = []
    for d in deps(meta):
        if d not in by_id:
            out.append(f"{d} (missing)")
        elif by_id[d]["status"] != "done":
            out.append(f"{d} ({by_id[d]['status']})")
    return out


def slugify(title):
    return re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")[:48].rstrip("-")


def write_index():
    rows = ["| ID | Title | Status | Area | Type | Pri |", "|---|---|---|---|---|---|"]
    # Active work first, then the backlog, then closed tickets.
    order = {s: i for i, s in enumerate(["in-progress", "in-review", "ready", "proposed", "done", "rejected"])}
    for p, m in sorted(all_tickets(), key=lambda t: (order.get(t[1]["status"], 99), t[1]["priority"], t[1]["id"])):
        rows.append(f"| [{m['id']}]({p.name}) | {m['title']} | {m['status']} | {m['area']} | {m['type']} | {m['priority']} |")
    text = INDEX.read_text()
    if BEGIN not in text or END not in text:
        sys.exit(f"{INDEX}: index markers missing")
    head, rest = text.split(BEGIN, 1)
    _, tail = rest.split(END, 1)
    INDEX.write_text(f"{head}{BEGIN}\n{chr(10).join(rows)}\n{END}{tail}")


def cmd_new(args):
    ids = [int(m["id"][2:]) for _, m in all_tickets()]
    ticket_id = f"T-{(max(ids) if ids else 0) + 1:04d}"
    body = TEMPLATE.read_text()
    for key, value in {"id": ticket_id, "title": args.title, "area": args.area, "type": args.type,
                       "priority": args.priority, "created": date.today().isoformat()}.items():
        body = body.replace("{{" + key + "}}", value)
    path = TICKETS / f"{ticket_id}-{slugify(args.title)}.md"
    path.write_text(body)
    write_index()
    print(path.relative_to(ROOT))


def cmd_list(args):
    tickets = all_tickets()
    by_id = {m["id"]: m for _, m in tickets}
    for p, m in tickets:
        if args.status and m["status"] != args.status:
            continue
        blocked = unmet_deps(m, by_id) if m["status"] in ("proposed", "ready") else []
        suffix = f"  [blocked by {', '.join(blocked)}]" if blocked else ""
        print(f"{m['id']}  {m['status']:<11} {m['area']:<13} {m['priority']}  {m['title']}{suffix}")


def cmd_next(args):
    tickets = all_tickets()
    by_id = {m["id"]: m for _, m in tickets}
    ready = sorted((m for _, m in tickets if m["status"] == "ready"), key=lambda m: (m["priority"], m["id"]))
    for m in ready:
        if not unmet_deps(m, by_id):
            print(m["id"])
            return
    if ready:
        sys.exit("no unblocked ready ticket; " + "; ".join(
            f"{m['id']} waits on {', '.join(unmet_deps(m, by_id))}" for m in ready))
    sys.exit("no ready tickets")


def cmd_show(args):
    p, _ = find(args.id)
    print(p.relative_to(ROOT))
    print(p.read_text())


def cmd_status(args):
    p, meta = find(args.id)
    if args.status == "in-progress" and not args.force:
        blocked = unmet_deps(meta, {m["id"]: m for _, m in all_tickets()})
        if blocked:
            sys.exit(f"{args.id} is blocked by {', '.join(blocked)} (use --force to override)")
    _, text = parse(p)
    text = re.sub(r"^status:.*$", f"status: {args.status}", text, count=1, flags=re.M)
    p.write_text(text)
    write_index()
    print(f"{args.id}: {meta['status']} -> {args.status}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    n = sub.add_parser("new")
    n.add_argument("title")
    n.add_argument("--area", choices=AREAS, required=True)
    n.add_argument("--type", choices=TYPES, required=True)
    n.add_argument("--priority", choices=PRIORITIES, default="P2")
    n.set_defaults(fn=cmd_new)
    ls = sub.add_parser("list")
    ls.add_argument("--status", choices=STATUSES)
    ls.set_defaults(fn=cmd_list)
    sub.add_parser("next").set_defaults(fn=cmd_next)
    sh = sub.add_parser("show")
    sh.add_argument("id")
    sh.set_defaults(fn=cmd_show)
    st = sub.add_parser("status")
    st.add_argument("id")
    st.add_argument("status", choices=STATUSES)
    st.add_argument("--force", action="store_true", help="move to in-progress even if dependencies aren't done")
    st.set_defaults(fn=cmd_status)
    sub.add_parser("index").set_defaults(fn=lambda _: write_index())
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
