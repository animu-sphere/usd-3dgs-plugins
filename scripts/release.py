#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Release-lane helpers for the tag-driven GitHub Actions workflow.

`ost ci generate github` has no tag lane -- its lanes are pull_request |
main | scheduled | workflow_dispatch -- so `.github/workflows/release.yml`
is hand-authored. To keep the release from drifting away from the CI
contract, this module *derives* every pin the release needs (the
bootstrapped `ost` version, runtime artifact digests, OCI references, and
each cell's platform/profile/up_to) from `openstrata.ci.yaml`'s source
cells. Editing that one file therefore moves the PR lane and the release
lane together, so a re-pinned runtime can never ship a release built
against the old one.

Subcommands:

  guard        Enforce the machine-checkable half of the release gate in
               docs/releases/README.md: tag, VERSION, every bundle manifest,
               openstrata.toml, and every bundle CMake project version agree,
               and CHANGELOG.md carries a finalized section.
  matrix       Emit the release job matrix as JSON.
  notes        Render docs/contributing/RELEASE_NOTES_TEMPLATE.md for the tag.
  set-version  Rewrite every declared release-version location from one
               input, making VERSION-by-way-of-this-command the single
               source. Library component versions (libs/*) are deliberately
               independent and untouched.

`--allow-unreleased` relaxes the changelog half for the workflow_dispatch
dry run, which exercises the whole lane before the changelog is finalized.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[1]
CI_MANIFEST = REPO_ROOT / "openstrata.ci.yaml"
VERSION_FILE = REPO_ROOT / "VERSION"
PROJECT_MANIFEST = REPO_ROOT / "openstrata.toml"
CHANGELOG = REPO_ROOT / "CHANGELOG.md"
NOTES_TEMPLATE = REPO_ROOT / "docs" / "contributing" / "RELEASE_NOTES_TEMPLATE.md"

# `ost ci generate` hard-codes these for cells requiring full evidence. The
# release lane consumes the same artifacts and must never verify them more
# weakly than the lane that gates the PRs feeding it.
EVIDENCE_FLAGS = "--require-sbom --require-provenance"
DEFAULT_MIN_TRUST = "local"

# Source lanes build from the checked-out repo. A tag release is a source
# build too, so it inherits exactly those cells.
SOURCE_LANES = ("pull_request", "main")


class GateError(Exception):
    """A release-gate precondition failed."""


def _load_ci_manifest() -> dict:
    with CI_MANIFEST.open(encoding="utf-8") as handle:
        return yaml.safe_load(handle)


def _repo_version() -> str:
    return VERSION_FILE.read_text(encoding="utf-8").strip()


def _version_from_tag(tag: str) -> str:
    if not re.fullmatch(r"v\d+\.\d+\.\d+", tag):
        raise GateError(
            f"tag {tag!r} is not a release tag; expected vMAJOR.MINOR.PATCH"
        )
    return tag[1:]


def _bundle_manifests() -> list[Path]:
    """Locate every source bundle manifest, using the root CMakeLists rule.

    A bundle is an immediate subdirectory of the repo root or of `plugins/`
    holding both an `openstrata.plugin.yaml` and a `CMakeLists.txt`. A
    recursive glob would also match the manifests `ost` copies into
    `.strata/targets/*/` as build state, which are not sources to gate on.
    """
    roots = [REPO_ROOT]
    if (REPO_ROOT / "plugins").is_dir():
        roots.append(REPO_ROOT / "plugins")

    manifests: list[Path] = []
    for root in roots:
        for entry in sorted(root.iterdir()):
            if not entry.is_dir():
                continue
            manifest = entry / "openstrata.plugin.yaml"
            if manifest.is_file() and (entry / "CMakeLists.txt").is_file():
                manifests.append(manifest)
    return manifests


def _project_manifest_version() -> str:
    match = re.search(
        r'^version\s*=\s*"([^"]+)"',
        PROJECT_MANIFEST.read_text(encoding="utf-8"),
        flags=re.MULTILINE,
    )
    return match.group(1) if match else ""


def _cmake_project_version(cmakelists: Path) -> str:
    if not cmakelists.is_file():
        return ""
    match = re.search(
        r"^\s*VERSION\s+(\d+\.\d+\.\d+)\s*$",
        cmakelists.read_text(encoding="utf-8"),
        flags=re.MULTILINE,
    )
    return match.group(1) if match else ""


def changelog_section(version: str, allow_unreleased: bool) -> tuple[str, str]:
    """Return the (heading, body) of the CHANGELOG section for `version`.

    A tagged release requires a finalized `## [X.Y.Z] - DATE` heading:
    shipping notes that still say "unreleased" is the exact mistake this
    guard exists to catch. The dry run instead falls back to the
    `## [Unreleased]` section, so the lane can be exercised end to end
    before anyone commits to a release date.
    """
    text = CHANGELOG.read_text(encoding="utf-8")
    lines = text.splitlines()

    def find(pattern: str) -> int | None:
        for index, line in enumerate(lines):
            if re.match(pattern, line):
                return index
        return None

    start = find(r"^## \[" + re.escape(version) + r"\]")
    if start is None and allow_unreleased:
        start = find(r"^## \[Unreleased\]")
    if start is None:
        raise GateError(
            f"CHANGELOG.md has no '## [{version}]' section; finalize the "
            f"release notes before tagging (see docs/releases/README.md)"
        )

    end = len(lines)
    for index in range(start + 1, len(lines)):
        # Keep-a-Changelog puts link definitions after the last section.
        if lines[index].startswith("## ") or lines[index].startswith("[Unreleased]:"):
            end = index
            break

    heading = lines[start]
    body = "\n".join(lines[start + 1 : end]).strip()
    if not body:
        raise GateError(f"CHANGELOG.md section {heading!r} is empty")
    if "unreleased" in heading.lower() and not allow_unreleased:
        raise GateError(
            f"CHANGELOG.md heading is not finalized: {heading!r}; replace "
            f"'Unreleased' with '[{version}] - <date>' before tagging"
        )
    return heading, body


def _write_output(path: str | None, **values: str) -> None:
    if not path:
        return
    with Path(path).open("a", encoding="utf-8") as handle:
        for key, value in values.items():
            handle.write(f"{key}={value}\n")


def cmd_guard(args: argparse.Namespace) -> int:
    is_release = bool(args.tag)
    version = _version_from_tag(args.tag) if is_release else _repo_version()
    # A dry run has no tag to disagree with VERSION, so only the changelog
    # half applies; relax it there and nowhere else.
    allow_unreleased = args.allow_unreleased or not is_release

    problems: list[str] = []

    if is_release:
        declared = _repo_version()
        if declared != version:
            problems.append(
                f"VERSION declares {declared!r} but the tag says {version!r}"
            )

    project_version = _project_manifest_version()
    if project_version != version:
        problems.append(
            f"openstrata.toml declares version {project_version!r} "
            f"but the release version is {version!r}"
        )

    for manifest_path in _bundle_manifests():
        with manifest_path.open(encoding="utf-8") as handle:
            manifest = yaml.safe_load(handle)
        bundle_version = str(manifest.get("plugin", {}).get("version", ""))
        rel = manifest_path.relative_to(REPO_ROOT).as_posix()
        if bundle_version != version:
            problems.append(
                f"{rel} declares plugin.version {bundle_version!r} "
                f"but the release version is {version!r}"
            )
        cmake_version = _cmake_project_version(
            manifest_path.parent / "CMakeLists.txt")
        if cmake_version != version:
            cmake_rel = (manifest_path.parent / "CMakeLists.txt").relative_to(
                REPO_ROOT).as_posix()
            problems.append(
                f"{cmake_rel} declares project VERSION {cmake_version!r} "
                f"but the release version is {version!r}"
            )

    try:
        changelog_section(version, allow_unreleased)
    except GateError as error:
        problems.append(str(error))

    if problems:
        for problem in problems:
            print(f"::error title=Release gate::{problem}", file=sys.stderr)
        return 1

    kind = "release" if is_release else "dry run"
    print(f"release gate passed ({kind}, version {version})")
    _write_output(
        args.github_output,
        version=version,
        tag=args.tag or f"v{version}",
        is_release="true" if is_release else "false",
    )
    return 0


def cmd_matrix(args: argparse.Namespace) -> int:
    manifest = _load_ci_manifest()
    runners = manifest.get("runners", {})
    trust = manifest.get("trust", {})
    minimum_trust = trust.get("release_min_trust", DEFAULT_MIN_TRUST)

    include: list[dict] = []
    for cell in manifest.get("cells", []):
        if cell.get("lane", "scheduled") not in SOURCE_LANES:
            continue
        # A packaged cell verifies an already-published artifact; only
        # source cells describe work a tag can reproduce from the checkout.
        if cell.get("plugin_artifact"):
            continue

        runner_name = cell["runner"]
        runner = runners[runner_name]
        hosted = runner.get("kind") == "github-hosted"
        runs_on = [runner["image"]] if hosted else list(runner.get("labels", []))
        remote = cell.get("runtime_remote") or {}

        include.append(
            {
                "name": cell["name"].replace("-pr-", "-release-"),
                "cell": cell["name"],
                "runs_on": runs_on,
                "hosted": hosted,
                "runner_profile": runner_name,
                "runtime_artifact": cell["runtime_artifact"],
                "runtime_remote": remote.get("uri", ""),
                "bundle": cell["bundle"],
                "platform": cell["platform"],
                "profile": cell["profile"],
                "up_to": cell["up_to"],
                "host_python": cell.get("host_python", ""),
                "minimum_trust": cell.get("trust", minimum_trust),
                "evidence_flags": EVIDENCE_FLAGS,
            }
        )

    if not include:
        raise GateError(
            "openstrata.ci.yaml declares no source cell; the release lane "
            "has nothing to build"
        )

    payload = json.dumps({"include": include}, separators=(",", ":"))
    print(payload)
    bootstrap = manifest.get("bootstrap", {}).get("ost", {})
    _write_output(
        args.github_output,
        matrix=payload,
        ost_version=str(bootstrap.get("version", "")),
        ost_repository=str(bootstrap.get("repository", "")),
    )
    return 0


def cmd_notes(args: argparse.Namespace) -> int:
    version = _version_from_tag(args.tag)
    _, body = changelog_section(version, args.allow_unreleased)

    checksums = "(appended by the release workflow)"
    if args.checksums:
        checksums = Path(args.checksums).read_text(encoding="utf-8").strip()

    notes = NOTES_TEMPLATE.read_text(encoding="utf-8")
    for key, value in {
        "{tag}": args.tag,
        "{version}": version,
        "{changelog}": body,
        "{checksums}": checksums,
    }.items():
        notes = notes.replace(key, value)

    if args.output:
        Path(args.output).write_text(notes, encoding="utf-8", newline="\n")
        print(f"wrote release notes for {args.tag} -> {args.output}")
    else:
        sys.stdout.write(notes)
    return 0


def cmd_set_version(args: argparse.Namespace) -> int:
    version = args.version
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise GateError(
            f"{version!r} is not a version; expected MAJOR.MINOR.PATCH")

    def rewrite(path: Path, pattern: str, replacement: str) -> None:
        rel = path.relative_to(REPO_ROOT).as_posix()
        if not path.is_file():
            raise GateError(f"{rel}: file not found")
        text = path.read_text(encoding="utf-8")
        updated, count = re.subn(
            pattern, replacement, text, count=1, flags=re.MULTILINE)
        if count != 1:
            raise GateError(f"{rel}: version declaration not found")
        if updated != text:
            path.write_text(updated, encoding="utf-8", newline="\n")
            print(f"set {rel} -> {version}")

    VERSION_FILE.write_text(version + "\n", encoding="utf-8", newline="\n")
    print(f"set VERSION -> {version}")
    rewrite(
        PROJECT_MANIFEST,
        r'^version\s*=\s*"[^"]+"',
        f'version = "{version}"',
    )
    for manifest_path in _bundle_manifests():
        # Match only a bare MAJOR.MINOR.PATCH so dependency pins elsewhere in
        # the manifest (quoted ranges like ">=0.1,<0.2") can never be hit.
        rewrite(
            manifest_path,
            r"^(\s*version:\s*)\d+\.\d+\.\d+\s*$",
            rf"\g<1>{version}",
        )
        rewrite(
            manifest_path.parent / "CMakeLists.txt",
            r"^(\s*VERSION\s+)\d+\.\d+\.\d+\s*$",
            rf"\g<1>{version}",
        )
    print("library component versions under libs/ are independent; "
          "bump them only on their own API changes")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    guard = subparsers.add_parser("guard", help="check the release gate")
    guard.add_argument("--tag", help="release tag; omit for a dry run")
    guard.add_argument("--allow-unreleased", action="store_true")
    guard.add_argument("--github-output")
    guard.set_defaults(func=cmd_guard)

    matrix = subparsers.add_parser("matrix", help="emit the release matrix")
    matrix.add_argument("--github-output")
    matrix.set_defaults(func=cmd_matrix)

    notes = subparsers.add_parser("notes", help="render the release notes")
    notes.add_argument("--tag", required=True)
    notes.add_argument("--checksums")
    notes.add_argument("--allow-unreleased", action="store_true")
    notes.add_argument("--output")
    notes.set_defaults(func=cmd_notes)

    set_version = subparsers.add_parser(
        "set-version",
        help="rewrite every declared release-version location")
    set_version.add_argument("version", help="MAJOR.MINOR.PATCH")
    set_version.set_defaults(func=cmd_set_version)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except GateError as error:
        print(f"::error title=Release::{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
