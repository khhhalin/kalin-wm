#!/usr/bin/env python3
"""Local review tool for the obsidian/ vault.

Snapshots current file contents as a "baseline" the first time it runs. On
every request it re-parses each markdown file into heading sections and
then into individual `- ` bullet snippets within each section, diffing
them against the baseline so you can page through single atomic facts that
changed (usually from autonomous agent edits) and mark each as Good or
Flag for review. Flagged snippets can be edited in place and saved
straight back into the vault file.
"""
import difflib
import hashlib
import http.server
import json
import re
import socketserver
import sys
from pathlib import Path
from urllib.parse import urlparse

VAULT_DIR = Path(__file__).resolve().parent.parent.parent / "obsidian"
STATE_FILE = Path(__file__).resolve().parent / "state.json"
INDEX_FILE = Path(__file__).resolve().parent / "index.html"

HEADING_RE = re.compile(r"^(#{1,6})\s+(.*?)\s*$")
BULLET_RE = re.compile(r"^\s*-\s+\S")
TOP_BULLET_RE = re.compile(r"^-\s+\S")
LIST_HEADER_RE = re.compile(r":\s*\*{0,2}_{0,2}\s*$")


def is_list_header(line):
    """A line that opens a list: it ends with `:` (markdown emphasis markers
    after the colon are allowed, e.g. a bold "**Rules:**" header)."""
    return bool(LIST_HEADER_RE.search(line.rstrip()))


def iter_md_files(vault_dir):
    for p in sorted(vault_dir.rglob("*.md")):
        rel = p.relative_to(vault_dir)
        if any(part.startswith(".") for part in rel.parts):
            continue
        yield p


def top_level_folders(vault_dir):
    """Top-level subfolders (relative, posix, no trailing slash) that
    contain at least one markdown file. Used for the first-run folder
    picker."""
    folders = set()
    for p in iter_md_files(vault_dir):
        rel = p.relative_to(vault_dir)
        if len(rel.parts) > 1:
            folders.add(rel.parts[0])
    return sorted(folders)


def is_ignored(relpath, ignored):
    """A relpath is ignored if it exactly matches an ignore entry, or sits
    under a folder ignore entry (stored with a trailing "/")."""
    for pat in ignored:
        if pat.endswith("/"):
            if relpath.startswith(pat):
                return True
        elif relpath == pat:
            return True
    return False


def load_state():
    if STATE_FILE.exists():
        state = json.loads(STATE_FILE.read_text())
        state.setdefault("ignored", [])
        state.setdefault("setup_done", False)
        return state
    state = {"baseline": {}, "decisions": {}, "ignored": [], "setup_done": False}
    for md in iter_md_files(VAULT_DIR):
        state["baseline"][md.relative_to(VAULT_DIR).as_posix()] = md.read_text()
    save_state(state)
    return state


def save_state(state):
    STATE_FILE.write_text(json.dumps(state, indent=2))


def sha(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]


def parse_headings(lines):
    """Split lines into heading-delimited segments.

    Returns a list of dicts: path, start, end (line indices, end exclusive).
    `start` points at the heading line itself (or 0 for a headingless
    preamble segment, which has path == []).
    """
    headings = []
    for i, line in enumerate(lines):
        m = HEADING_RE.match(line)
        if m:
            headings.append((i, len(m.group(1)), m.group(2)))

    segments = []
    first_heading_line = headings[0][0] if headings else len(lines)
    if first_heading_line > 0:
        segments.append({"path": [], "start": 0, "end": first_heading_line})

    stack = []
    for idx, (line_i, level, title) in enumerate(headings):
        end = headings[idx + 1][0] if idx + 1 < len(headings) else len(lines)
        while stack and stack[-1][0] >= level:
            stack.pop()
        stack.append((level, title))
        path = [t for _, t in stack]
        segments.append({"path": path, "start": line_i, "end": end})

    seen = {}
    for seg in segments:
        key = tuple(seg["path"])
        seg["heading_occurrence"] = seen.get(key, 0)
        seen[key] = seg["heading_occurrence"] + 1
    return segments


def split_blocks(lines, start, end):
    """Split lines[start:end] into atomic blocks: each `- ` bullet (any
    indent, plus any wrapped continuation lines) is its own block; leftover
    non-bulleted prose between blanks/bullets is its own "prose" block.

    Returns a list of dicts: lines, start, end (end exclusive).
    """
    blocks = []
    cur_lines = []
    cur_start = None

    def flush(i):
        nonlocal cur_lines, cur_start
        if any(l.strip() for l in cur_lines):
            blocks.append({"lines": cur_lines, "start": cur_start, "end": i})
        cur_lines = []
        cur_start = None

    for i in range(start, end):
        line = lines[i]
        if BULLET_RE.match(line):
            flush(i)
            cur_lines = [line]
            cur_start = i
        elif line.strip() == "":
            flush(i)
        else:
            if cur_start is None:
                cur_start = i
            cur_lines.append(line)
    flush(end)
    return blocks


def merge_lists(blocks):
    """Merge a colon-ending header block with the `- ` list blocks (flat or
    nested) that immediately follow it, up to the next blank line — a blank
    line always ends a list. Returns blocks with an added "is_list" flag."""
    merged = []
    i = 0
    while i < len(blocks):
        block = blocks[i]
        if is_list_header(block["lines"][0]):
            group_lines = list(block["lines"])
            group_end = block["end"]
            j = i + 1
            while j < len(blocks):
                nxt = blocks[j]
                is_bullet = bool(BULLET_RE.match(nxt["lines"][0]))
                contiguous = nxt["start"] == group_end
                if is_bullet and contiguous:
                    group_lines.extend(nxt["lines"])
                    group_end = nxt["end"]
                    j += 1
                else:
                    break
            if j > i + 1:
                merged.append({"lines": group_lines, "start": block["start"], "end": group_end, "is_list": True})
                i = j
                continue
        merged.append({"lines": block["lines"], "start": block["start"], "end": block["end"], "is_list": False})
        i += 1
    return merged


def parse_leaves(text):
    """Parse markdown text into atomic review snippets, grouped by heading.

    Returns a list of dicts: path, heading_occurrence, occurrence
    (bullet index within that heading instance), text, start, end (line
    indices, end exclusive), leaf_kind ("bullet", "multisnippet", or "prose").
    A "multisnippet" is a colon-ending header line plus the `- ` list that
    immediately follows it (up to the next blank line) — they travel
    together as one reviewable unit.
    """
    lines = text.splitlines()
    leaves = []
    for seg in parse_headings(lines):
        is_heading_line = bool(lines[seg["start"]:seg["start"] + 1] and HEADING_RE.match(lines[seg["start"]]))
        body_start = seg["start"] + 1 if is_heading_line else seg["start"]
        raw_blocks = split_blocks(lines, body_start, seg["end"])
        for occurrence, block in enumerate(merge_lists(raw_blocks)):
            block_lines = block["lines"]
            if block["is_list"]:
                leaf_kind = "multisnippet"
            elif BULLET_RE.match(block_lines[0]):
                leaf_kind = "bullet"
            else:
                leaf_kind = "prose"
            leaves.append({
                "path": seg["path"],
                "heading_occurrence": seg["heading_occurrence"],
                "occurrence": occurrence,
                "start": block["start"],
                "end": block["end"],
                "text": "\n".join(block_lines),
                "leaf_kind": leaf_kind,
            })
    return leaves


def section_id(relpath, path, side, occurrence):
    raw = relpath + "\x00" + "/".join(path) + "\x00" + side + "\x00" + str(occurrence)
    return hashlib.sha1(raw.encode("utf-8")).hexdigest()[:16]


def compute_snippets(state):
    baseline = state["baseline"]
    decisions = state["decisions"]
    snippets = []

    ignored = state.get("ignored", [])
    files = sorted(set(baseline.keys()) | {p.relative_to(VAULT_DIR).as_posix() for p in iter_md_files(VAULT_DIR)})
    for relpath in files:
        if is_ignored(relpath, ignored):
            continue
        fpath = VAULT_DIR / relpath
        new_text = fpath.read_text() if fpath.exists() else None
        old_text = baseline.get(relpath)

        new_leaves = parse_leaves(new_text) if new_text is not None else []
        old_leaves = parse_leaves(old_text) if old_text is not None else []

        by_heading = {}
        for leaf in new_leaves:
            by_heading.setdefault((tuple(leaf["path"]), leaf["heading_occurrence"]), {"new": [], "old": []})["new"].append(leaf)
        for leaf in old_leaves:
            by_heading.setdefault((tuple(leaf["path"]), leaf["heading_occurrence"]), {"new": [], "old": []})["old"].append(leaf)

        for (path, _heading_occ), sides in by_heading.items():
            new_list = sides["new"]
            old_list = sides["old"]
            new_texts = [l["text"].strip() for l in new_list]
            old_texts = [l["text"].strip() for l in old_list]
            matcher = difflib.SequenceMatcher(a=old_texts, b=new_texts, autojunk=False)

            for tag, i1, i2, j1, j2 in matcher.get_opcodes():
                if tag == "equal":
                    continue
                elif tag == "delete":
                    for leaf in old_list[i1:i2]:
                        snippets.append(build_snippet(decisions, relpath, path, "old", leaf["occurrence"], "removed", leaf))
                elif tag == "insert":
                    for leaf in new_list[j1:j2]:
                        snippets.append(build_snippet(decisions, relpath, path, "new", leaf["occurrence"], "added", leaf))
                elif tag == "replace":
                    n_pairs = min(i2 - i1, j2 - j1)
                    for k in range(n_pairs):
                        leaf = new_list[j1 + k]
                        snippets.append(build_snippet(decisions, relpath, path, "new", leaf["occurrence"], "modified", leaf))
                    for leaf in old_list[i1 + n_pairs:i2]:
                        snippets.append(build_snippet(decisions, relpath, path, "old", leaf["occurrence"], "removed", leaf))
                    for leaf in new_list[j1 + n_pairs:j2]:
                        snippets.append(build_snippet(decisions, relpath, path, "new", leaf["occurrence"], "added", leaf))
    return snippets


def build_snippet(decisions, relpath, path, side, occurrence, kind, leaf):
    sid = section_id(relpath, path, side, occurrence)
    content_hash = sha(leaf["text"])
    decision = decisions.get(sid)
    status = decision["status"] if decision and decision.get("hash") == content_hash else "pending"
    return {
        "id": sid,
        "relpath": relpath,
        "heading_path": list(path),
        "occurrence": leaf["occurrence"],
        "kind": kind,
        "leaf_kind": leaf["leaf_kind"],
        "text": leaf["text"],
        "hash": content_hash,
        "status": status,
    }


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def _send_json(self, obj, code=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self):
        length = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(length) or b"{}")

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/" or path == "/index.html":
            body = INDEX_FILE.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif path == "/api/snippets":
            state = load_state()
            self._send_json({
                "vault": str(VAULT_DIR),
                "setup_done": state.get("setup_done", False),
                "snippets": compute_snippets(state),
            })
        elif path == "/api/files":
            state = load_state()
            ignored = state.get("ignored", [])
            files = sorted({p.relative_to(VAULT_DIR).as_posix() for p in iter_md_files(VAULT_DIR)} | set(state["baseline"].keys()))
            self._send_json({"files": [{"relpath": f, "ignored": is_ignored(f, ignored)} for f in files]})
        elif path == "/api/folders":
            state = load_state()
            ignored = set(state.get("ignored", []))
            folders = top_level_folders(VAULT_DIR)
            self._send_json({
                "folders": [{"name": f, "excluded": f"{f}/" in ignored} for f in folders],
                "setup_done": state.get("setup_done", False),
            })
        else:
            self.send_error(404)

    def do_POST(self):
        path = urlparse(self.path).path
        state = load_state()

        if path == "/api/decide":
            data = self._read_json()
            state["decisions"][data["id"]] = {"status": data["status"], "hash": data["hash"]}
            save_state(state)
            self._send_json({"ok": True})

        elif path == "/api/ignore":
            data = self._read_json()
            ignored = set(state.get("ignored", []))
            if data["ignored"]:
                ignored.add(data["relpath"])
            else:
                ignored.discard(data["relpath"])
            state["ignored"] = sorted(ignored)
            save_state(state)
            self._send_json({"ok": True})

        elif path == "/api/setup":
            data = self._read_json()
            excluded_folders = {f"{name}/" for name in data.get("excluded_folders", [])}
            kept = {p for p in state.get("ignored", []) if not p.endswith("/")}
            state["ignored"] = sorted(kept | excluded_folders)
            state["setup_done"] = True
            save_state(state)
            self._send_json({"ok": True})

        elif path == "/api/save":
            data = self._read_json()
            relpath = data["relpath"]
            heading_path = data["heading_path"]
            occurrence = data["occurrence"]
            new_content = data["new_content"]
            kind = data["kind"]
            fpath = VAULT_DIR / relpath

            if kind == "removed" or not fpath.exists():
                existing = fpath.read_text() if fpath.exists() else ""
                sep = "\n\n" if existing and not existing.endswith("\n\n") else ""
                fpath.write_text(existing + sep + new_content.rstrip("\n") + "\n")
                # Suppress the original "removed" flag: match it against the
                # exact original (unedited) baseline text, not new_content,
                # since that's what compute_snippets will hash next time.
                old_leaves = parse_leaves(state["baseline"].get(relpath, ""))
                orig = next((s for s in old_leaves if s["path"] == heading_path and s["occurrence"] == occurrence), None)
                if orig is not None:
                    sid = section_id(relpath, heading_path, "old", occurrence)
                    state["decisions"][sid] = {"status": "good", "hash": sha(orig["text"])}
            else:
                text = fpath.read_text()
                leaves = parse_leaves(text)
                target = next((s for s in leaves if s["path"] == heading_path and s["occurrence"] == occurrence), None)
                if target is None:
                    self._send_json({"ok": False, "error": "snippet not found"}, 404)
                    return
                lines = text.splitlines()
                new_lines = lines[:target["start"]] + new_content.rstrip("\n").split("\n") + lines[target["end"]:]
                fpath.write_text("\n".join(new_lines) + "\n")
                sid = section_id(relpath, heading_path, "new", occurrence)
                state["decisions"][sid] = {"status": "good", "hash": sha(new_content.rstrip("\n"))}

            save_state(state)
            self._send_json({"ok": True})

        elif path == "/api/checkpoint":
            new_baseline = {}
            for md in iter_md_files(VAULT_DIR):
                new_baseline[md.relative_to(VAULT_DIR).as_posix()] = md.read_text()
            state["baseline"] = new_baseline
            state["decisions"] = {}
            save_state(state)
            self._send_json({"ok": True})

        elif path == "/api/audit":
            # Full-vault audit: clear the baseline so every current bullet
            # in every non-ignored file is treated as a fresh snippet to
            # review, regardless of whether it changed recently. Existing
            # decisions are kept, so anything already reviewed under this
            # same content stays resolved.
            state["baseline"] = {}
            save_state(state)
            self._send_json({"ok": True})

        else:
            self.send_error(404)


def main():
    global VAULT_DIR, STATE_FILE
    args = sys.argv[1:]
    port = 8787
    positional = []
    i = 0
    while i < len(args):
        if args[i] == "--vault":
            VAULT_DIR = Path(args[i + 1]).expanduser().resolve()
            i += 2
        elif args[i] == "--state":
            STATE_FILE = Path(args[i + 1]).expanduser().resolve()
            i += 2
        else:
            positional.append(args[i])
            i += 1
    if positional:
        port = int(positional[0])
    if not VAULT_DIR.exists():
        print(f"Vault directory not found: {VAULT_DIR}", file=sys.stderr)
        sys.exit(1)
    load_state()
    socketserver.ThreadingTCPServer.allow_reuse_address = True
    with socketserver.ThreadingTCPServer(("127.0.0.1", port), Handler) as httpd:
        print(f"Vault review tool: http://127.0.0.1:{port}  (vault: {VAULT_DIR})")
        httpd.serve_forever()


if __name__ == "__main__":
    main()
