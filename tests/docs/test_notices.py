# SPDX-License-Identifier: Apache-2.0
"""Every packaged notice carries the full license text of each vendored
component its bundle ships (FMT-08).

A plugin package contains its own THIRD_PARTY_NOTICES.md and the workspace
one; third_party/ is not packaged, so a reference to a license file there is
not enough. Re-vendoring a component changes its license file, and this test
then fails until the notices are refreshed.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

MINIZ = ("third_party/miniz/LICENSE",)
LIBWEBP = ("third_party/libwebp/COPYING", "third_party/libwebp/PATENTS")
ZSTD = ("third_party/zstd/LICENSE",)

# tinyPLY's grant is the comment at the top of tinyply.h, not a file.
TINYPLY_GRANT = (
    "This software is in the public domain. Where that dedication is not "
    "recognized, you are granted a perpetual, irrevocable license to copy, "
    "distribute, and modify this file as you see fit."
)

NOTICES = {
    "THIRD_PARTY_NOTICES.md": MINIZ + LIBWEBP + ZSTD,
    "plugins/gaussian-ply/THIRD_PARTY_NOTICES.md": (),
    "plugins/gaussian-spz/THIRD_PARTY_NOTICES.md": MINIZ + ZSTD,
    "plugins/gaussian-sog/THIRD_PARTY_NOTICES.md": LIBWEBP + MINIZ,
}
TINYPLY_NOTICES = ("THIRD_PARTY_NOTICES.md",
                   "plugins/gaussian-ply/THIRD_PARTY_NOTICES.md")


def normalized(text):
    # Markdown quoting and re-wrapping must not hide a missing sentence, but
    # line endings and indentation are not part of the license.
    text = re.sub(r"^> ?", "", text, flags=re.MULTILINE)
    return " ".join(text.split())


class PackagedNoticeTests(unittest.TestCase):
    def test_license_texts_are_complete(self):
        for notice, licenses in NOTICES.items():
            body = normalized((ROOT / notice).read_text(encoding="utf-8"))
            for license_file in licenses:
                with self.subTest(notice=notice, license=license_file):
                    text = normalized(
                        (ROOT / license_file).read_text(encoding="utf-8"))
                    self.assertIn(text, body)

    def test_tinyply_grant_is_quoted(self):
        self.assertIn(TINYPLY_GRANT, normalized(
            (ROOT / "third_party/tinyply/tinyply.h").read_text(encoding="utf-8")
            .replace(" * ", " ")))
        for notice in TINYPLY_NOTICES:
            with self.subTest(notice=notice):
                body = normalized((ROOT / notice).read_text(encoding="utf-8"))
                self.assertIn(TINYPLY_GRANT, body)

    def test_every_bundle_packages_the_workspace_notice(self):
        for bundle in ("gaussian-ply", "gaussian-spz", "gaussian-sog"):
            with self.subTest(bundle=bundle):
                cmake = (ROOT / "plugins" / bundle / "CMakeLists.txt").read_text(
                    encoding="utf-8")
                self.assertIn('/../../THIRD_PARTY_NOTICES.md"', cmake)


if __name__ == "__main__":
    unittest.main()
