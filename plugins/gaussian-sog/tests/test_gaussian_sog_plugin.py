# SPDX-License-Identifier: Apache-2.0
"""Assertions for the gaussian-sog skeleton SdfFileFormat.

The skeleton's whole contract: USD routes `.sog` files to this plugin, every
read fails with the stable GSSOG-E001 not-implemented diagnostic (never an
empty stage), and the shipped diagnostics catalog agrees with the codes the
sources can emit. The v0.5.0 decoder replaces the rejection tests with the
real stage-contract suite.
"""

import json
import pathlib
import re

from pxr import Sdf, Tf, Usd


FIXTURES = pathlib.Path(__file__).parent / "fixtures"
CATALOG = (pathlib.Path(__file__).parents[1]
           / "plugin" / "resources" / "gaussian-sog" / "diagnostics.json")


def check_routing():
    """USD must resolve .sog to this plugin, not report 'no plugin found'."""
    file_format = Sdf.FileFormat.FindByExtension("sog")
    assert file_format, "no file format registered for .sog"
    fixture = FIXTURES / "not-implemented.sog"
    assert file_format.CanRead(str(fixture)), fixture


def check_read_fails_with_stable_code():
    fixture = str(FIXTURES / "not-implemented.sog")
    mark = Tf.Error.Mark()
    stage = None
    try:
        stage = Usd.Stage.Open(fixture)
    except Tf.ErrorException as error:
        assert "GSSOG-E001" in str(error), error
    else:
        # Some USD entry points swallow the exception and return None; the
        # diagnostic must still have been raised through the error mark.
        assert not stage, "skeleton must not open a .sog as a stage"
        errors = "\n".join(str(e) for e in mark.GetErrors())
        assert "GSSOG-E001" in errors, errors
    finally:
        mark.Clear()


def check_catalog():
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    assert catalog.get("schema") == "gaussian-sog.diagnostics/v1", catalog
    codes = [entry["code"] for entry in catalog["diagnostics"]]
    assert len(codes) == len(set(codes)), "duplicate codes in diagnostics.json"
    for entry in catalog["diagnostics"]:
        assert entry.get("severity") in ("error", "warning"), entry
        assert entry.get("summary"), entry
        assert entry.get("action"), entry

    # The catalog and the codes the source can emit must agree exactly, in
    # both directions, mirroring the gaussian-ply/gaussian-spz cross-checks.
    header = (pathlib.Path(__file__).parents[1]
              / "src" / "io" / "GaussianSogDiagnostics.h")
    header_codes = set(
        re.findall(r'"(GSSOG-[EW]\d{3})"', header.read_text(encoding="utf-8")))
    assert header_codes == set(codes), (
        f"header-only: {header_codes - set(codes)}, "
        f"catalog-only: {set(codes) - header_codes}")


def main():
    check_routing()
    check_read_fails_with_stable_code()
    check_catalog()
    print("gaussian-sog skeleton assertions passed")


if __name__ == "__main__":
    main()
