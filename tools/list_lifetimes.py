#!/usr/bin/env python3
"""Lifetime-pass candidates for the firmware, listed mechanically (the AVIEW
pass pattern: never review allocations by eye). The doc with the verdict
tables lives once, in the app repo: docs/LIFETIME_PASS_2026-10.md
(MarcelineVPQ/DS5Dongle-OLED-Config-Web). See CLAUDE.md "Lifetime pass".

Every acquisition in src/ (lib/ and comments excluded), one TSV row each:

  file  line  function  ctx  kind  detail  verdict  head

  kind   heap       malloc / calloc / realloc / strdup / new / make_unique /
                    make_shared, and the SDK / library calls that allocate
                    (queue_init, opus_*_create)
         container  a std::vector / string / map / unordered_map / deque /
                    list / set object: a local (allocates per call when it
                    gets elements), a static or global (lives forever, may
                    grow), or a function returning one by value
         grow       push_back / emplace* / insert / resize / reserve / assign /
                    append, or map[key] = (inserts) on a container
         timer      add_alarm_* / add_repeating_timer_* / alarm_pool_* /
                    btstack_run_loop_add_timer / *_set_timer
         register   a handler registered with BTstack / the SDK
                    (hci_add_event_handler, l2cap_register_service,
                    sdp_register_service, irq handlers, multicore_launch_core1)
  ctx    where it runs (the loop pass's call graph, tools/list_loops.py):
         core1, core0-loop, callback, isr, boot, "-" (not reached / file scope)

Usage: python3 tools/list_lifetimes.py           the TSV
       python3 tools/list_lifetimes.py --check   the gate (CI): exit 1 on a row
                                                 without a valid
                                                 "// lifetime: VERDICT — reason"
       python3 tools/list_lifetimes.py --md      the per-file tables for the doc

A verdict is a comment on the line or in the comment block right above:
    // lifetime: NOT-WORTH — boot-time, one per firmware run, never freed by design
Verdicts: OK (the release point named), LEAK, WASTE, HOT-ALLOC, NOT-WORTH.
No dependencies beyond the Python standard library.
"""

import re
import sys

from list_loops import ROOT, build_graph, strip

VERDICTS = ["OK", "LEAK", "WASTE", "HOT-ALLOC", "NOT-WORTH"]
VERDICT_RE = re.compile(r"lifetime:\s*(" + "|".join(VERDICTS) + r")\s+—\s+\S")

CONTAINERS = r"std::(?:vector|basic_string|string|map|unordered_map|multimap|deque|list|set|unordered_set)\b"
PATTERNS = [
    ("heap", re.compile(r"\b(malloc|calloc|realloc|strdup|aligned_alloc|make_unique|make_shared|queue_init|Prealloc|"
                        r"queue_init_with_spinlock|opus_\w+_create)\s*[<(]")),
    ("heap", re.compile(r"(?<![\w.>])new\s+[A-Za-z_(]")),
    ("timer", re.compile(r"\b(add_alarm_\w+|add_repeating_timer_\w+|alarm_pool_add_\w+|alarm_pool_create\w*|"
                         r"btstack_run_loop_add_timer|btstack_run_loop_set_timer)\s*\(")),
    ("register", re.compile(r"\b(hci_add_event_handler|l2cap_register_service|sdp_register_service|"
                            r"irq_set_exclusive_handler|irq_add_shared_handler|gpio_set_irq_enabled_with_callback|"
                            r"multicore_launch_core1|sm_add_event_handler|hid_host_register_packet_handler)\s*\(")),
    ("grow", re.compile(r"[\w\]\)]\s*(?:\.|->)\s*(push_back|emplace_back|emplace|insert|resize|reserve|assign|append)\s*\(")),
]
# A container object or a function returning one: "std::vector<uint8_t> x", "vector<uint8_t> f(",
# "unordered_map<...> m" (the file may say "using std::vector").
CONTAINER_DECL = re.compile(r"(?:\bstatic\s+|\bconst\s+)*\b(?:std::)?(vector|string|unordered_map|map|deque|list|set)\b\s*(<[^;{}()]*>)?\s*&?\s*([A-Za-z_]\w*)\s*([(;={\[])")


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    fns, by_file = build_graph()
    per_file = {}
    for f in fns:
        per_file.setdefault(f.file, []).append(f)

    def fn_at(file, line):
        best = None
        for f in per_file.get(file, []):
            if f.line <= line <= getattr(f, "end", f.line) and (best is None or f.line > best.line):
                best = f
        return best

    # Functions that return a container by value: every call makes one.
    returners = set()
    for lines in by_file.values():
        for m in re.finditer(r"(?:std::)?(?:vector|string|map|unordered_map|deque|list)\s*<[^;{}()]*>\s+([A-Za-z_]\w*)\s*\(", strip("\n".join(lines))):
            returners.add(m.group(1))
    rows = []
    for file, lines in by_file.items():
        text = strip("\n".join(lines))
        usings = set(re.findall(r"using\s+std::(\w+)\s*;", text))
        maps = set()
        slines = text.split("\n")
        for ln, s in enumerate(slines, 1):
            if re.match(r"\s*#", s) or re.match(r"\s*using\b", s):
                continue
            found = []
            for kind, rx in PATTERNS:
                for m in rx.finditer(s):
                    found.append((kind, m.group(1) if m.groups() else m.group(0).strip()))
            for m in CONTAINER_DECL.finditer(s):
                typ, name, after = m.group(1), m.group(3), m.group(4)
                full = s[max(0, m.start() - 5):m.start()].endswith("std::") or "std::" in m.group(0)
                if not full and typ not in usings:
                    continue
                if name in ("const", "operator"):
                    continue
                fn = fn_at(file, ln)
                if after == "(" and fn is None:
                    continue  # a prototype; the definition is its own row
                elif after == "(" and re.search(r"\)\s*(const\s*)?\{", s[m.end():] + " {") and fn and fn.line == ln:
                    what = f"returns {typ} by value: {name}()"
                else:
                    scope = "static" if (fn is None or re.search(r"\bstatic\b", s[:m.start() + 7])) else "local"
                    if "&" in m.group(0).split(name)[0][-3:]:
                        continue  # a reference parameter / binding, not an object
                    what = f"{scope} {typ} {name}"
                if typ in ("map", "unordered_map"):
                    maps.add(name)
                found.append(("container", what))
            for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", s):
                if m.group(1) in returners and not CONTAINER_DECL.search(s) and not (fn_at(file, ln) and fn_at(file, ln).line == ln):
                    found.append(("container", f"temporary from {m.group(1)}()"))
            for name in maps:
                if re.search(r"\b" + re.escape(name) + r"\s*\[[^\]]*\]\s*=(?!=)", s):
                    found.append(("grow", f"{name}[key] = (inserts)"))
            for kind, what in found:
                fn = fn_at(file, ln)
                ctx = "+".join(sorted(fn.ctx - {"boot"}) or sorted(fn.ctx)) if fn else "-"
                rows.append(dict(file=file, line=ln, fn=fn.name if fn else "<file>", ctx=ctx, kind=kind,
                                 detail=what, verdict=verdict_of(lines, ln), head=lines[ln - 1].strip()[:100]))
    rows.sort(key=lambda r: (r["file"], r["line"]))

    if mode == "--check":
        bad = [r for r in rows if not r["verdict"] or r["verdict"].startswith("INVALID")]
        if bad:
            print(f"list_lifetimes: {len(bad)} acquisition(s) without a valid verdict comment (CLAUDE.md \"Lifetime pass\"):", file=sys.stderr)
            for r in bad:
                print(f"  {r['file']}:{r['line']}  {r['fn']}  [{r['ctx']}] {r['kind']} {r['detail']}  {r['verdict'] or 'no // lifetime: VERDICT — reason'}", file=sys.stderr)
            print("Add \"// lifetime: <" + "|".join(VERDICTS) + "> — <release point or reason>\" on its line or the comment block above.", file=sys.stderr)
            sys.exit(1)
        counts = {v: sum(word(r) == v for r in rows) for v in VERDICTS}
        print(f"list_lifetimes: {len(rows)} acquisitions, all with a verdict (" + ", ".join(f"{v} {counts[v]}" for v in VERDICTS) + ")")
    elif mode == "--md":
        cur = None
        for r in rows:
            if r["file"] != cur:
                cur = r["file"]
                print(f"\n### {cur}\n\n| Line | Function | Runs on | Kind | What | Verdict | Release point / reason |\n|---|---|---|---|---|---|---|")
            m = re.match(r"lifetime:\s*(\S+)\s+—\s+(.*?)\s*(\*/)?$", r["verdict"])
            v, why = (m.group(1), m.group(2)) if m else ("?", r["verdict"])
            esc = lambda x: x.replace("|", "\\|")
            print(f"| {r['line']} | `{r['fn']}` | {r['ctx']} | {r['kind']} | {esc(r['detail'])} | **{v}** | {esc(why)} |")
    else:
        keys = ["file", "line", "fn", "ctx", "kind", "detail", "verdict", "head"]
        print("\t".join(keys))
        for r in rows:
            print("\t".join(str(r[k]) for k in keys))


def word(r):
    m = re.match(r"lifetime:\s*(\S+)", r["verdict"])
    return m.group(1) if m else "?"


def verdict_of(lines, line):
    own = lines[line - 1]
    if VERDICT_RE.search(own):
        return own[own.index("lifetime:"):].strip()
    i = line - 2
    while i >= 0 and re.match(r"\s*(//|/\*|\*)", lines[i]):
        if VERDICT_RE.search(lines[i]):
            return lines[i][lines[i].index("lifetime:"):].strip()
        if "lifetime:" in lines[i]:
            return "INVALID " + lines[i].strip()
        i -= 1
    if "lifetime:" in own:
        return "INVALID " + own.strip()
    return ""


if __name__ == "__main__":
    main()
