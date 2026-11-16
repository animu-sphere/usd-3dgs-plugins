import subprocess
import tempfile
import unittest
from pathlib import Path

from scripts import check_docs


def policy_fixture():
    folders = "".join(
        f"| {kind} | {' '.join(f'[link]({item})' for item in sorted(paths))} |\n"
        for kind, paths in check_docs.DOC_TYPES.items()
    )
    return {
        "README.md": f"[Supported formats]({check_docs.CAPABILITIES})",
        "docs/README.md": f"| Type | Source |\n| --- | --- |\n{folders}",
        check_docs.CURRENT: "# Current: v0.10.0 — Format Coverage Expansion\n",
        check_docs.PLAN: "## v0.10.0 — Format Coverage Expansion\n",
        check_docs.ROADMAP: "[Capabilities](../reference/CAPABILITY_MATRIX.md)",
        check_docs.BACKLOG: "# Backlog\n",
        check_docs.RELEASES: "| Version | Record |\n| --- | --- |\n",
    }


def lint(sources, tags=()):
    return list(check_docs.policy_errors(
        {path: check_docs.parse(source) for path, source in sources.items()}, tags
    ))


class MarkdownTests(unittest.TestCase):
    def test_links_and_github_anchors(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            target = root / "format (spec).md"
            target.write_text(
                "# Format — `v2`\n# Format — `v2`\n# Format — v2-1\n"
                "Café 形式\n=========\n\n<a id=\"stable-name\"></a>\n",
                encoding="utf-8",
            )
            (root / "preview.png").touch()
            docs = {"README.md": check_docs.parse(
                "# Instructions\n\n[spec][reader] and [section](#instructions)\n\n"
                "[duplicate](<format (spec).md#format--v2-1-1>)\n\n"
                "[custom](format%20%28spec%29.md#stable-name)\n\n"
                "<img src=\"preview.png\" alt=\"Demo\">\n\n"
                "```md\n[ignored](missing.md)\n```\n\n"
                "`[inline example](missing.md)`\n\n"
                "[reader]: <format (spec).md#caf%C3%A9-%E5%BD%A2%E5%BC%8F>\n"
            )}
            (root / "README.md").touch()
            self.assertEqual(list(check_docs.link_errors(root, docs)), [])
            self.assertIn("format--v2-1", check_docs.parse(target.read_text(encoding="utf-8")).anchors)

    def test_missing_links_and_anchors_in_tables(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = ("# Entry\n\n| Links |\n| --- |\n"
                      "| [missing](guide.md) / [obsolete](#retired-heading) |\n\n"
                      "<img src=\"missing.png\">\n")
            (root / "README.md").write_text(source, encoding="utf-8")
            problems = list(check_docs.link_errors(root, {"README.md": check_docs.parse(source)}))
            self.assertCountEqual(problems, [
                "README.md:5: guide.md: missing local target",
                "README.md:5: #retired-heading: missing Markdown anchor",
                "README.md:7: missing.png: missing local target",
            ])


class PolicyTests(unittest.TestCase):
    def test_pr_does_not_require_future_record_or_flag_history_and_examples(self):
        sources = policy_fixture()
        sources["CHANGELOG.md"] = (
            "# Changelog\n\n## [Unreleased]\n\n## [0.9.0]\n\nFrozen at v1.0.0.\n"
        )
        sources["docs/reports/notes.md"] = "# History\n\nStable import API at v1.0.0.\n"
        sources[check_docs.CURRENT] += "\n```md\n- [x] done\nFrozen at v1.0.0.\n```\n"
        self.assertEqual(lint(sources), [])

    def test_rejects_drift_in_current_documents(self):
        sources = policy_fixture()
        sources["docs/README.md"] = sources["docs/README.md"].replace("(guides/)", "(reference/)")
        sources["README.md"] += "\n\n| Format | Status |\n| --- | --- |\n| PLY | supported |\n"
        sources[check_docs.PLAN] += (
            "\n## v0.9.0 — Preview\n\n## v1.0.0 — stable import API\n"
        )
        sources[check_docs.CURRENT] += (
            "\n- [x] Shipped\n\n| ID | Work |\n| --- | --- |\n| FMT-01 | Evaluate input |\n"
            + "\n" * 150
        )
        sources[check_docs.BACKLOG] += "\n| Work | Owner |\n| --- | --- |\n| Evaluate input | import |\n"
        sources["docs/architecture/old.md"] = "[Details](../roadmap/release-plan.md#v100--stable-import-api-and-file-format-support)"
        errors = "\n".join(lint(sources))
        for fragment in ("Guide must classify", "150-line limit", "past milestone sections",
                         "duplicate status table", "FMT-01 is already assigned",
                         "old.md:1: retired API milestone", "move completed work"):
            self.assertIn(fragment, errors)
        sources[check_docs.PLAN] = "## v0.9.0 — Preview\n"
        self.assertIn("active milestone title must match", "\n".join(lint(sources)))

    def test_release_records_match_their_index_rows(self):
        sources = policy_fixture()
        sources["docs/releases/v0.9.0.md"] = "# v0.8.0 — Wrong version\n"
        self.assertIn("heading must start with v0.9.0", "\n".join(lint(sources)))
        self.assertIn("missing index row for v0.9.0", "\n".join(lint(sources)))
        sources["docs/releases/v0.9.0.md"] = "# v0.9.0 — Preview\n"
        sources[check_docs.RELEASES] += "| v0.9.0 | [old](v0.8.0.md) |\n"
        self.assertIn("v0.9.0 row must link to v0.9.0.md", "\n".join(lint(sources)))
        sources[check_docs.RELEASES] = sources[check_docs.RELEASES].replace("v0.8.0.md", "v0.9.0.md")
        self.assertEqual(lint(sources, {"v0.9.0"}), [])

    def test_tag_gate_requires_earlier_reachable_records_only(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)

            def git(*args):
                subprocess.run(["git", "-c", "user.name=Docs lint",
                                "-c", "user.email=lint@example.invalid",
                                "-c", "commit.gpgsign=false", *args],
                               cwd=root, check=True, capture_output=True)

            git("init", "-b", "main")
            git("commit", "--allow-empty", "-m", "prior release")
            git("tag", "v0.9.0")
            git("switch", "-c", "unrelated")
            git("commit", "--allow-empty", "-m", "unrelated release")
            git("tag", "v0.9.1")
            git("switch", "main")
            git("commit", "--allow-empty", "-m", "new release")
            git("tag", "v0.10.0")
            (root / "VERSION").write_text("0.10.0\n", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "does not match VERSION"):
                check_docs.release_inputs(root, "v0.9.0")
            tags = check_docs.release_inputs(root, "v0.10.0")
            self.assertEqual(tags, {"v0.9.0"})
            sources = policy_fixture()
            self.assertIn("earlier tag v0.9.0", "\n".join(lint(sources, tags)))
            sources[check_docs.RELEASES] += "| v0.9.0 | [Preview](v0.9.0.md) |\n"
            sources["docs/releases/v0.9.0.md"] = "# v0.9.0 — Preview\n"
            self.assertEqual(lint(sources, tags), [])


if __name__ == "__main__":
    unittest.main()
