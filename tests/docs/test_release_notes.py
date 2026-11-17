# SPDX-License-Identifier: Apache-2.0
"""The release body links the changelog's relative paths to the tagged tree."""

import unittest

from scripts import release


class AbsoluteLinkTests(unittest.TestCase):
    def test_relative_links_point_at_the_tag(self):
        body = ("See [ADR 0002](docs/adr/0002-vendored-zstd-decoder.md) and "
                "[the format](./docs/reference/SPZ_FORMAT.md#3-container).")
        self.assertEqual(
            release.absolute_links(body, "v0.11.0"),
            "See [ADR 0002](https://github.com/animu-sphere/usd-3dgs-plugins"
            "/blob/v0.11.0/docs/adr/0002-vendored-zstd-decoder.md) and "
            "[the format](https://github.com/animu-sphere/usd-3dgs-plugins"
            "/blob/v0.11.0/docs/reference/SPZ_FORMAT.md#3-container).")

    def test_absolute_and_anchor_links_are_kept(self):
        body = ("[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), "
                "[mail](mailto:a@example.com), [below](#compatibility)")
        self.assertEqual(release.absolute_links(body, "v0.11.0"), body)


if __name__ == "__main__":
    unittest.main()
