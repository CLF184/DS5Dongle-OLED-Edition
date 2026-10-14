#!/usr/bin/env python3
"""Error-pass candidates for the firmware, listed mechanically (the AVIEW
pass style: never sample failure paths by eye). The doc with the verdict
tables lives once, in the app repo: docs/ERROR_PASS_2026-10.md
(MarcelineVPQ/DS5Dongle-OLED-Config-Web). See CLAUDE.md "Error pass".

The rule: no silent failure. Every error a user action can hit reaches the
user (a result code the app shows, an OLED message), or is deliberately
logged/counted with a reason the user needn't see it.

Reuses tools/list_loops.py's scanner (tokens, #if groups, function bodies,
call graph). One TSV row per failure path in src/ (lib/ excluded):

  file  line  function  kind  ctx  verdict  head

  kind   ignored-call  a call that can fail (BTstack / TinyUSB / flash / queue
                       / Opus / cyw43 status, or one of src/'s own bool or
                       Ok/Invalid/NotSaved functions) whose result is dropped
         checked-call  the same, with its result tested or passed on
         return-fail   return false / -1 / nullptr / an Invalid, NotSaved or
                       CMD_RESULT_INVALID|FAILED|BUSY value
         result-code   a CMD_RESULT_INVALID|FAILED|BUSY assignment (0xF1's
                       result byte, what the app reads after an 0xF6 SET)
         validate      a stored-data check (magic / CRC / version) that falls
                       back when it fails
         counter       a drop / overflow / error counter bumped
         guard         `if (...) return;` in a void user-action handler
                       (0xF6 SET, the deferred cmd_task, the OLED button
                       handlers): the action silently does nothing
  ctx    where it runs (list_loops.py's call graph), plus "user" when it is
         reached from a user action (0xF6 SET / cmd_task / OLED input).

Usage: python3 tools/list_errors.py           the TSV
       python3 tools/list_errors.py --check   the gate (CI): exit 1 on a row
                                              without a valid verdict comment
       python3 tools/list_errors.py --md      the per-file tables for the doc

A verdict is a comment on the row's line or in the comment block right above:
    // error: SHOWN — CMD_RESULT_FAILED in 0xF1; the app's command error
    // error: SILENT-BUG — nothing; <what is lost> Change: <the fix>
i.e. "<surfaces as>; <why>", optionally " Change: <proposal>" (the doc's
columns). Verdicts: SHOWN LOGGED-OK SILENT-BUG SWALLOWED NOT-WORTH, each
followed by " — " and the reason. Standard library only.
"""

import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from list_loops import ROOT, KEYWORDS, Parser, tokens_of, main_loop_calls, address_taken  # noqa: E402

VERDICTS = ["SHOWN", "LOGGED-OK", "SILENT-BUG", "SWALLOWED", "NOT-WORTH"]
VERDICT_RE = re.compile(r"error:\s*(" + "|".join(VERDICTS) + r")\s+—\s+\S")

# Calls into the SDK / BTstack / TinyUSB / Opus that return a status, a bool
# or a byte count that can say "failed". (Getters, void setters and the
# event-field accessors aren't listed: they can't fail.)
EXTERNAL_FALLIBLE = {
    "hci_send_cmd", "hci_power_control", "gap_inquiry_start", "gap_inquiry_stop", "gap_read_rssi",
    "gap_pin_code_response", "gap_link_key_iterator_init", "l2cap_send", "l2cap_create_channel",
    "l2cap_register_service", "l2cap_request_can_send_now_event", "tud_control_xfer",
    "tud_audio_buffer_and_schedule_control_xfer", "tud_hid_report", "tud_hid_n_report", "tud_audio_write",
    "tud_remote_wakeup", "tud_connect", "tud_disconnect", "tusb_init", "queue_try_add", "flash_safe_execute",
    "cyw43_arch_init", "opus_encoder_create", "opus_decoder_create", "opus_encoder_ctl", "opus_decode",
    "opus_encode_float", "opus_packet_pad", "malloc", "calloc", "realloc",
}
# src/'s own bool functions that answer a question rather than report success.
PREDICATE = re.compile(r"(^(is_|has_|can_|should_|valid|take_|allowed$|ds_mode$|usb_wake_keyboard$)|_(is|has|get|owns|take)_|"
                       r"_has$|_streaming$|_to_headset$|gets_fake$|_ok$|_sent$|_sending$|(^|_)(occupied|empty|full|active|ready|valid|unsaved|"
                       r"temporary|mounted|connected|holds_slot|owns_test_slot|any_empty|known|pending|busy|due|"
                       r"seen|enabled|playing|suspended|reached|changed|wanted|running|on|off|cmd)$)")
RESULT_TYPES = {"bool", "CalibSet", "RemapSet"}
FAIL_VALUES = re.compile(r"^(Invalid|NotSaved|Failed|Error|CMD_RESULT_(INVALID|FAILED|BUSY))$")
COUNTER = re.compile(r"(drop|overflow|overrun|underrun|lost|miss|err|fail|reject|timeout|stall)", re.I)
VALIDATE = re.compile(r"(crc|magic)", re.I)
# Entry points of a user action: an 0xF6 SET (vendor interface or HID),
# the deferred commands, and the OLED's button handlers.
USER_ROOTS = re.compile(r"^(pico_cmd_set|cmd_task|handle_buttons|\w+_handle_input|settings_adjust)$")


class ErrParser(Parser):
    """list_loops' Parser, also keeping each function's body range and
    return type."""

    def body(self, i, end, fn, nest):
        if not hasattr(fn, "rng"):
            fn.rng = (i, end)
            fn.rtype = self.return_type(i - 1)
        return super().body(i, end, fn, nest)

    def return_type(self, brace):
        j = brace - 1
        while self.tok(j) in ("const", "override", "noexcept", "final", "volatile"):
            j -= 1
        if self.tok(j) != ")" or j not in self.rev:
            return ""
        k = self.rev[j] - 1  # the name (or the RAM macro's ')')
        if self.tok(k) == ")" and k in self.rev:
            k = self.rev[k] - 1
        while self.tok(k - 1) == "::":
            k -= 2
        start = self.prev_stmt_end(k) + 1
        words = [self.tok(x) for x in range(start, k) if self.tok(x) not in ("static", "inline", "extern", "constexpr",
                                                                           "CORE0_RAM_FUNC", "__attribute__")]
        return words[-1] if words else ""


def stmt_starts(p, i):
    """True when the call at token i is the whole statement's start."""
    j = i - 1
    while p.tok(j) == "::" and j >= 2:  # ns::fn(
        j -= 2
    t = p.tok(j)
    if t in (";", "{", "}", "else", ":"):
        return "ignored"
    if t == ")" and j in p.rev:
        o = p.rev[j]
        inner = [p.tok(x) for x in range(o + 1, j)]
        if inner == ["void"]:
            return "ignored"
        if p.tok(o - 1) in ("if", "while", "for"):
            return "ignored"
    return ""


def tidy(s):
    s = re.sub(r"([!=<>]) =", r"\1=", s)
    s = re.sub(r"\s*(->|\.|::)\s*", r"\1", s)
    return re.sub(r"\s+([,;)\]])", r"\1", re.sub(r"([(\[!])\s+", r"\1", s))


def analyse(p, fn, fallible):
    rows = []
    a, b = fn.rng
    t = p.tok
    i = a
    while i < b:
        tok = t(i)
        ln = p.t[i][1] if i < len(p.t) else 0
        if re.match(r"[A-Za-z_]\w*$", tok) and t(i + 1) == "(" and tok not in KEYWORDS and t(i - 1) not in (".", "->"):
            if tok in fallible:
                close = p.match.get(i + 1, b)
                ignored = stmt_starts(p, i) and t(close + 1) == ";"
                rows.append((ln, "ignored-call" if ignored else "checked-call", tok))
        elif tok == "return":
            k = i + 1
            val = []
            while k < b and t(k) != ";":
                val.append(t(k))
                k += 1
            fail = (val in (["false"], ["nullptr"], ["NULL"]) or (len(val) == 2 and val[0] == "-" and val[1].isdigit())
                    or (val and FAIL_VALUES.match(val[-1])) or (val == ["0"] and re.search(r"get_report|cmd_get", fn.name)))
            if fail and (fn.rtype not in RESULT_TYPES or fn.name.split("::")[-1] in fallible):
                rows.append((ln, "return-fail", tidy(" ".join(val))))
        elif tok == "=" and FAIL_VALUES.match(t(i + 1)) and t(i + 1).startswith("CMD_RESULT") and t(i + 2) == ";":
            rows.append((ln, "result-code", t(i + 1)))
        elif tok == "if" and t(i + 1) == "(":
            close = p.match.get(i + 1, b)
            cond = [t(x) for x in range(i + 2, close)]
            if "user" in fn.ctx and fn.rtype == "void" and t(close + 1) == "return" and t(close + 2) == ";":
                rows.append((ln, "guard", tidy(" ".join(cond))[:60]))
        elif VALIDATE.search(tok) and re.match(r"[A-Za-z_]\w*$", tok) and t(i + 2) == "=" and t(i + 1) in ("=", "!") \
                and t(i - 1) not in (".", "->") or (VALIDATE.search(tok) and t(i - 1) in (".", "->") and t(i + 2) == "="
                                                   and t(i + 1) in ("=", "!")):
            # magic / crc compared (== or !=; the tokenizer splits them): stored data checked
            j, k = i, i + 3
            while t(j - 1) in (".", "->"):
                j -= 2
            while k < b and k < i + 9 and t(k) not in ("&&", "||", ")", ";", "?", ","):
                k += 1
            rows.append((ln, "validate", tidy(" ".join(t(x) for x in range(j, k)))))
        elif tok == "+" and t(i + 1) == "+" and (COUNTER.search(t(i - 1)) or COUNTER.search(t(i + 2))):  # x++ / ++x
            name = t(i - 1) if COUNTER.search(t(i - 1)) else t(i + 2)
            if re.match(r"[A-Za-z_]\w*$", name):
                rows.append((ln, "counter", name))
        i += 1
    return rows


def verdict_of(lines, line):
    own = lines[line - 1]
    if VERDICT_RE.search(own):
        return own[own.index("error:"):].strip()
    i = line - 2
    while i >= 0 and re.match(r"\s*(//|/\*|\*)", lines[i]):
        if VERDICT_RE.search(lines[i]):
            return lines[i][lines[i].index("error:"):].strip()
        if "error:" in lines[i]:
            return "INVALID " + lines[i].strip()
        i -= 1
    if "error:" in own:
        return "INVALID " + own.strip()
    return ""


def collect():
    files = sorted(p for p in (ROOT / "src").rglob("*") if p.suffix in (".c", ".cpp", ".h"))
    fns, parsers, texts = [], {}, {}
    for path in files:
        rel = path.relative_to(ROOT).as_posix()
        parser = ErrParser(rel, tokens_of(path))
        for f in parser.parse():
            f.file = rel
            fns.append(f)
        parsers[rel] = parser
        texts[rel] = path.read_text(encoding="utf-8", errors="replace").split("\n")
    byname = {}
    for f in fns:
        byname.setdefault(f.name.split("::")[-1], []).append(f)

    def spread(roots, tag):
        queue = list(roots)
        while queue:
            f = queue.pop()
            if tag in f.ctx:
                continue
            f.ctx.add(tag)
            for c in f.calls:
                queue += byname.get(c, [])

    # The same roots as list_loops.py, plus the user-action roots.
    spread(byname.get("core1_entry", []), "core1")
    main_fn = next((f for f in fns if f.name == "main" and f.file == "src/main.cpp"), None)
    if main_fn:
        spread([g for c in main_loop_calls(main_fn) for g in byname.get(c, [])], "core0-loop")
    cbs = [f for f in fns if re.match(r"(tud_\w+_cb|usbd_app_driver_get_cb)$", f.name)]
    cbs += [g for name in address_taken(files, set(byname)) for g in byname.get(name, [])]
    spread([g for g in cbs if "core1" not in g.ctx], "callback")
    if main_fn:
        spread([main_fn], "boot")
    spread([f for f in fns if USER_ROOTS.match(f.name.split("::")[-1])], "user")

    fallible = set(EXTERNAL_FALLIBLE)
    for f in fns:
        short = f.name.split("::")[-1]
        if getattr(f, "rtype", "") in RESULT_TYPES and not PREDICATE.search(short):
            fallible.add(short)

    rows = []
    for f in fns:
        if not hasattr(f, "rng"):
            continue
        lines = texts[f.file]
        ctx = "+".join(sorted(f.ctx - {"boot"}) or sorted(f.ctx) or ["-"])
        seen = {}
        for ln, kind, head in analyse(parsers[f.file], f, fallible):
            key = (ln, kind)
            if key in seen:  # one row per line and kind
                seen[key]["head"] += ", " + head
                continue
            row = dict(file=f.file, line=ln, fn=f.name, kind=kind, ctx=ctx, verdict=verdict_of(lines, ln), head=head)
            seen[key] = row
            rows.append(row)
    rows.sort(key=lambda r: (r["file"], r["line"], r["kind"]))
    return rows


def split_verdict(v):
    m = re.match(r"error:\s*(\S+)\s+—\s+(.*?)\s*(\*/)?$", v)
    if not m:
        return "?", v, "", ""
    why, change = m.group(2), ""
    if " Change: " in why:
        why, change = why.split(" Change: ", 1)
    surfaces, _, why = why.partition("; ")  # "<surfaces as>; <why>"
    return m.group(1), surfaces, why, change


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    rows = collect()
    if mode == "--check":
        bad = [r for r in rows if not r["verdict"] or r["verdict"].startswith("INVALID")]
        if bad:
            print(f"list_errors: {len(bad)} failure path(s) without a valid verdict comment (CLAUDE.md \"Error pass\"):", file=sys.stderr)
            for r in bad:
                print(f"  {r['file']}:{r['line']}  {r['fn']}  [{r['kind']} {r['ctx']}]  {r['head']}  {r['verdict'] or ''}", file=sys.stderr)
            print("Add \"// error: <" + "|".join(VERDICTS) + "> — <reason>\" on the line or the line above.", file=sys.stderr)
            sys.exit(1)
        counts = {v: sum(split_verdict(r["verdict"])[0] == v for r in rows) for v in VERDICTS}
        print(f"list_errors: {len(rows)} failure paths, all with a verdict (" + ", ".join(f"{v} {counts[v]}" for v in VERDICTS) + ")")
    elif mode == "--md":
        cur = None
        for r in rows:
            if r["file"] != cur:
                cur = r["file"]
                print(f"\n#### {cur}\n\n| Line | Function | Kind | Runs on | Surfaces as | Verdict | Why | Change |\n|---|---|---|---|---|---|---|---|")
            v, surfaces, why, change = split_verdict(r["verdict"])
            esc = lambda s: s.replace("|", "\\|")  # noqa: E731
            print(f"| {r['line']} | `{r['fn']}` | {r['kind']} `{esc(r['head'])}` | {r['ctx']} | {esc(surfaces)} | **{v}** | {esc(why) or '-'} | {esc(change) or '-'} |")
    else:
        print("\t".join(["file", "line", "function", "kind", "ctx", "verdict", "head"]))
        for r in rows:
            print("\t".join(str(r[k]) for k in ["file", "line", "fn", "kind", "ctx", "verdict", "head"]))


if __name__ == "__main__":
    main()
