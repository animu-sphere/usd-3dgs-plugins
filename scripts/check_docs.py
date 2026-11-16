#!/usr/bin/env python3
"""Check first-party Markdown links and the boundaries in docs/README.md."""

import argparse
import re
import subprocess
import sys
import unicodedata
from dataclasses import dataclass, field
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit

from markdown_it import MarkdownIt


ROOT = Path(__file__).resolve().parents[1]
MARKDOWN = MarkdownIt("commonmark").enable(["table", "strikethrough"])
VERSION = r"v\d+\.\d+\.\d+"
MILESTONE = re.compile(rf"^({VERSION}) (?:—|-) (.+)$")
RECORD = re.compile(rf"docs/releases/({VERSION})\.md$")
RETIRED = re.compile(
    r"\b(?:before|through|from|at|until)\s+v1\.0\.0\b|"
    r"\bv1\.0\.0(?:'s| stability commitments?| will freeze)\b|"
    r"\b(?:pre|post)[ -]1\.0\b|\bstable\s+import\s+API\b|"
    r"#v100--stable-import-api-and-file-format-support\b",
    re.IGNORECASE,
)
DONE = re.compile(r"^\s*(?:[-*+](?:\s+|$)\s*)?(?:\[[xX]\]|✅|☑)")
WORK_ID = re.compile(r"^[A-Z][A-Z0-9]*-\d+$")
CAPABILITIES = "docs/reference/CAPABILITY_MATRIX.md"
PLAN = "docs/roadmap/release-plan.md"
CURRENT = "docs/roadmap/current.md"
BACKLOG = "docs/roadmap/backlog.md"
ROADMAP = "docs/roadmap/README.md"
RELEASES = "docs/releases/README.md"
DOC_TYPES = {
    "Normative": {"design/", "adr/"},
    "Architecture": {"architecture/"},
    "Reference": {"reference/"},
    "Guide": {"guides/", "contributing/"},
    "Plan": {"roadmap/"},
    "Record": {"releases/", "reports/"},
}
MD_SUFFIXES = {".md", ".markdown"}


def git(root, *args):
    return subprocess.check_output(["git", *args], cwd=root).decode("utf-8")


def markdown_paths(root):
    paths = git(root, "ls-files", "-z", "--cached", "--others",
                "--exclude-standard", "--", "*.md", "*.markdown")
    return sorted(name for name in set(paths.split("\0"))
                  if name and not name.startswith("third_party/")
                  and (root / name).is_file())


def slug(heading):
    """Apply GitHub's lowercase, punctuation removal and space rules."""
    return "".join(
        "-" if char == " " else char
        for char in heading.lower()
        if char in " -_" or unicodedata.category(char)[0] in "LMN"
    )


class HtmlLinks(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []
        self.anchors = set()

    def handle_starttag(self, tag, attrs):
        for key, value in attrs:
            if not value:
                continue
            if key == "id" or (tag == "a" and key == "name"):
                self.anchors.add(value)
            if (tag, key) in {("a", "href"), ("img", "src"), ("source", "src"),
                              ("video", "src"), ("video", "poster")}:
                self.links.append((self.getpos()[0] - 1, value))


@dataclass
class Cell:
    text: str
    links: list[str]


@dataclass
class Document:
    lines: int
    headings: list[tuple[int, str]] = field(default_factory=list)
    anchors: set[str] = field(default_factory=set)
    links: list[tuple[int, str]] = field(default_factory=list)
    visible: list[tuple[int, str]] = field(default_factory=list)
    tables: list[list[list[Cell]]] = field(default_factory=list)


def parse(source):
    doc = Document(len(source.splitlines()))
    line = 1
    in_heading = in_cell = False
    generated = set()
    next_suffix = {}

    def add_html(content, start):
        html = HtmlLinks()
        html.feed(content)
        doc.anchors.update(html.anchors)
        doc.links.extend((start + offset, url) for offset, url in html.links)

    for token in MARKDOWN.parse(source):
        if token.map:
            line = token.map[0] + 1
        kind = token.type
        if kind == "heading_open":
            in_heading = True
        elif kind == "heading_close":
            in_heading = False
        elif kind == "table_open":
            doc.tables.append([])
        elif kind == "tr_open":
            doc.tables[-1].append([])
        elif kind in {"th_open", "td_open", "th_close", "td_close"}:
            in_cell = kind.endswith("_open")
        elif kind == "html_block":
            add_html(token.content, line)
        elif kind == "inline":
            parts = []
            urls = []
            inline_line = line
            for child in token.children or []:
                if child.type in {"text", "code_inline"}:
                    parts.append(child.content)
                elif child.type in {"softbreak", "hardbreak"}:
                    parts.append(" ")
                    inline_line += 1
                elif child.type in {"link_open", "image"}:
                    url = child.attrGet("href" if child.type == "link_open" else "src")
                    if url:
                        doc.links.append((inline_line, url))
                        urls.append(url)
                elif child.type == "html_inline":
                    add_html(child.content, inline_line)
            label = "".join(parts)
            doc.visible.append((line, label))
            if in_cell:
                doc.tables[-1][-1].append(Cell(label, urls))
            if in_heading:
                doc.headings.append((line, label))
                base = anchor = slug(label)
                suffix = next_suffix.get(base, 0)
                while anchor in generated:
                    suffix += 1
                    anchor = f"{base}-{suffix}"
                next_suffix[base] = suffix
                generated.add(anchor)
    doc.anchors.update(generated)
    return doc


def historical(path):
    return (RECORD.fullmatch(path) or path.startswith("docs/adr/") or
            (path.startswith("docs/reports/") and Path(path).name != "README.md"))


def link_errors(root, docs):
    cache = {}
    for path, doc in docs.items():
        source = root / path
        for line, url in doc.links:
            target = urlsplit(url)
            if target.scheme or target.netloc or target.path.startswith("/"):
                continue
            destination = (
                source.parent / unquote(target.path) if target.path else source
            ).resolve()
            fragment = unquote(target.fragment)
            prefix = f"{path}:{line}: {url}"
            if not destination.is_relative_to(root) or not destination.exists():
                yield f"{prefix}: missing local target"
                continue
            if fragment and destination.is_dir():
                destination = destination / "README.md"
                if not destination.is_file():
                    yield f"{prefix}: directory has no README for the anchor"
                    continue
            if fragment and destination.suffix.lower() in MD_SUFFIXES:
                rel = destination.relative_to(root).as_posix()
                if rel not in cache:
                    other = docs.get(rel) or parse(destination.read_text(encoding="utf-8-sig"))
                    cache[rel] = other.anchors
                if fragment not in cache[rel]:
                    yield f"{prefix}: missing Markdown anchor"


def table_rows(doc, first_column):
    for rows in doc.tables:
        if rows and rows[0] and rows[0][0].text == first_column:
            yield from rows[1:]


def classification_errors(index, paths):
    classified = {}
    for row in table_rows(index, "Type"):
        if row and row[0].text in DOC_TYPES:
            kind = row[0].text
            if kind in classified:
                yield f"docs/README.md: duplicate classification for {kind}"
            classified[kind] = {url.split("/", 1)[0] + "/"
                                for cell in row[1:] for url in cell.links
                                if "/" in url and not url.startswith("../")}
    for kind, directories in DOC_TYPES.items():
        if classified.get(kind) != directories:
            yield f"docs/README.md: {kind} must classify {', '.join(sorted(directories))}"
    known = set().union(*DOC_TYPES.values())
    for path in paths:
        if path.startswith("docs/") and path != "docs/README.md":
            parts = path.split("/")
            if len(parts) < 3 or f"{parts[1]}/" not in known:
                yield f"{path}: add a documentation classification"


def record_errors(docs, required_tags):
    index = {}
    for row in table_rows(docs[RELEASES], "Version"):
        if not row or not re.fullmatch(VERSION, row[0].text):
            continue
        tag = row[0].text
        link = f"{tag}.md"
        if tag in index:
            yield f"{RELEASES}: duplicate record row for {tag}"
        if not any(link in cell.links for cell in row[1:]):
            yield f"{RELEASES}: {tag} row must link to {link}"
        index[tag] = f"docs/releases/{link}"
    for path, doc in docs.items():
        match = RECORD.fullmatch(path)
        if not match:
            continue
        tag = match[1]
        if not doc.headings or not doc.headings[0][1].startswith((tag + " ", tag + "—")):
            yield f"{path}: heading must start with {tag}"
        if tag not in index:
            yield f"{RELEASES}: missing index row for {tag}"
    for tag in sorted(required_tags):
        if index.get(tag) not in docs:
            yield f"{RELEASES}: earlier tag {tag} needs an indexed release record"


def policy_errors(docs, required_tags=()):
    current = docs[CURRENT]
    if current.lines > 150:
        yield f"{CURRENT}: {current.lines} lines exceeds the 150-line limit"

    title = current.headings[0][1] if current.headings else ""
    active = MILESTONE.fullmatch(title.removeprefix("Current: "))
    milestones = [match for _, heading in docs[PLAN].headings
                  if (match := MILESTONE.fullmatch(heading))]
    if not title.startswith("Current: ") or not active or not any(m[0] == active[0] for m in milestones):
        yield f"{CURRENT}: the active milestone title must match a release-plan heading"
    elif any(version_key(m[1]) < version_key(active[1]) for m in milestones):
        yield f"{PLAN}: move past milestone sections to the release records"

    assigned = {}
    for row in table_rows(current, "ID"):
        if len(row) > 1 and WORK_ID.fullmatch(row[0].text):
            assigned[row[0].text.casefold()] = row[0].text
            assigned[row[1].text.casefold()] = row[0].text
    for column in ("Work", "ID"):
        for row in table_rows(docs[BACKLOG], column):
            if row and (duplicate := assigned.get(row[0].text.casefold())):
                yield f"{BACKLOG}: {duplicate} is already assigned in {CURRENT}"

    for path, doc in docs.items():
        if historical(path):
            continue
        # Changelog entries from tagged releases are evidence. Check only Unreleased.
        released = next((line for line, heading in doc.headings
                         if re.match(r"^\[?\d+\.\d+\.\d+(?:\]|\s|$)", heading)), float("inf"))
        live = path != "CHANGELOG.md"
        for line, label in doc.visible + doc.links:
            if (live or line < released) and RETIRED.search(label):
                yield f"{path}:{line}: retired API milestone or version promise"
            if path in {CURRENT, BACKLOG, ROADMAP, PLAN} and DONE.match(label):
                yield f"{path}:{line}: move completed work to a release record or report"

    for path in ("README.md", ROADMAP, CURRENT, PLAN, BACKLOG):
        doc = docs[path]
        for rows in doc.tables:
            headers = {cell.text.lower() for cell in rows[0]}
            if headers & {"status", "support", "supported"} and headers & {
                "capability", "container", "dialect", "exporter", "format", "milestone", "version",
            }:
                yield f"{path}: replace duplicate status table with a canonical reference"
    for path, url in (("README.md", CAPABILITIES), (ROADMAP, "../reference/CAPABILITY_MATRIX.md")):
        if not any(destination == url for _, destination in docs[path].links):
            yield f"{path}: link to the canonical capability matrix"

    yield from classification_errors(docs["docs/README.md"], docs)
    yield from record_errors(docs, required_tags)


def version_key(tag):
    return tuple(int(part) for part in tag.removeprefix("v").split("."))


def release_inputs(root, tag):
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(VERSION, tag) or tag != f"v{version}":
        raise ValueError(f"tag {tag!r} does not match VERSION {version!r}")
    return {name for name in git(root, "tag", "--merged", "HEAD").splitlines()
            if name != tag and re.fullmatch(VERSION, name)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-tag", help="verify VERSION and require records for earlier reachable tags")
    args = parser.parse_args()
    try:
        docs = {name: parse((ROOT / name).read_text(encoding="utf-8-sig"))
                for name in markdown_paths(ROOT)}
        tags = release_inputs(ROOT, args.release_tag) if args.release_tag else ()
        errors = sorted(set([*link_errors(ROOT, docs), *policy_errors(docs, tags)]))
    except (KeyError, OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"Documentation check could not run: {error}", file=sys.stderr)
        return 1
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    lines = docs[CURRENT].lines
    if lines > 100:
        print(f"Warning: {CURRENT} is {lines} lines (aim for 100 or fewer).")
    print(f"Checked {len(docs)} Markdown documents; {CURRENT}: {lines} lines.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
