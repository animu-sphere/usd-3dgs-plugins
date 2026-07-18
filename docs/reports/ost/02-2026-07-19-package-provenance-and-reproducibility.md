# OST dogfooding report #2 — packaged-consumer closure and package digest reproducibility

- Date: 2026-07-19
- OST: 0.18.0
- Host: Windows x86_64, MSVC 14.34.31933 / toolset 143, Ninja (OST path) and
  Visual Studio 17 2022 generator (plain root path)
- Runtime: `openstrata-cy2026-windows-x86_64-py313-usd`, OpenUSD 26.05,
  CPython 3.13.14, digest `sha256:acebd2e48f70…`
- Context: post-v0.1.0 release stabilization — exercising the extracted package
  from a clean directory, verifying manual activation, and finding the cause of
  the Windows across-run archive digest drift recorded in
  [releases/v0.1.0.md](../../releases/v0.1.0.md).

Report [#1](01-2026-07-18-v0.18.0-bootstrap.md) covered bootstrap through
packaging. This report closes two packaged-consumer roadmap items and reduces
the reproducibility item to hosted observation.

## TL;DR

The extracted package works from a clean directory outside the worktree, and
manual (non-OST) OpenUSD activation works on Windows. The Windows across-run
digest drift had two distinct causes, both now understood:

1. **MSVC wall-clock timestamps.** Every relink changed the PE-header and
   debug-directory `TimeDateStamp`; every recompile changed object and
   archive-member stamps. Project-side fix: `/Brepro` on `cl.exe`, `lib.exe`,
   and `link.exe`. Two fully clean build+package cycles now produce identical
   archive digests.
2. **Build-flavor contamination (upstream ask).** `ost plugin package` stages
   whatever sits in the bundle's `lib/`, and the dual-mode plain root build
   writes its differently-flagged DLL to the same location. The July-18 local
   package demonstrably contained the Visual Studio flavor, not the
   `ost plugin build` flavor, and nothing reported that.

## 1. Commands exercised

```console
ost plugin package plugins/gaussian-ply --json
tar -xf gaussian-ply-0.1.0-cy2026-windows-x86_64-py313-usd.tar.zst -C <clean-dir>
ost plugin run <clean-dir> --target cy2026 --profile usd -- usdcat <fixture>
ost plugin run <clean-dir> --target cy2026 --profile usd -- python <stage-open checks>
ost plugin build plugins/gaussian-ply --json      # clean, repeated
ost plugin test --workspace --up-to 5 --json
ost plugin test plugins/gaussian-ply --from-package --up-to 5 --json
```

Manual activation used no `ost` at launch: `PXR_PLUGINPATH_NAME` pointed at the
extracted `plugin/resources/gaussian-ply`, the runtime `bin`/`lib` and the
Python 3.13 directory were on `PATH`, and the runtime `lib/python` was on
`PYTHONPATH`; details are in
[INSTALL.md](../../guides/INSTALL.md#manual-package-activation).

## 2. What worked well

### 2.1 The extracted package is a first-class bundle

The extracted archive root carries `openstrata.plugin.yaml`, so
`ost plugin run <extracted-root>` works directly with the extracted tree as the
bundle. All 20 files verified against the package manifest hashes, and
`Plug.Registry` resolved the plugin DLL from the extracted tree — no build-tree
path leaked into discovery. ASCII, binary little-endian, and degree-1 SH
fixtures all opened with correct type, count, degree, and default prim.

### 2.2 Same-build packaging is deterministic

Packaging the same build twice — hours apart, in separate `ost` invocations —
produced byte-identical archives (`sha256:b51470ba…` both times). The tar.zst
layer adds no nondeterminism; OST's within-run release gate is measuring the
right thing at the right layer.

## 3. Measured results

| Run | Input | Archive digest |
| --- | --- | --- |
| July 18 local package | plain-VS-flavor DLL (see 4.1) | `sha256:b51470ba…` |
| Repackage, same build | identical tree | `sha256:b51470ba…` (identical) |
| Clean `ost` rebuild, pre-fix | Ninja/cl DLL, new link time | `sha256:f930249e…` |
| Clean `ost` rebuild ×2, with `/Brepro` | Ninja/cl DLL | `sha256:bcf109fd…` (identical twice) |

Byte-level evidence:

- Two clean Ninja relinks of identical inputs differed in exactly two runs of
  bytes: the PE-header `TimeDateStamp` and its debug-directory copy.
- Relinking from the preserved July-18 Visual Studio object files reproduced
  the July-18 packaged DLL except for those same two timestamp fields — proof
  that the July-18 package contained the plain-VS flavor (232,448 bytes) rather
  than the `ost plugin build` flavor (234,496 bytes).
- After `/Brepro`, the plugin DLL and the packaged `gaussianCore.lib` hash
  identically across fully clean rebuilds. `/Brepro` was needed on the archiver
  separately (`STATIC_LIBRARY_OPTIONS`); compiling with it alone left
  `lib.exe` member-header timestamps in the static library.

Verification baselines are unchanged after the flag: workspace 12 pass / 0
fail / 3 skip, package-origin 14 pass / 0 fail / 1 skip (the report #1 golden
seam), CTest 3/3.

## 4. Upstream asks and observations

### 4.1 P1 — record and verify build provenance at package time

`ost plugin package` stages the bundle `lib/` as found. The scaffold's
dual-mode root build intentionally writes its DLL into that same `lib/` so the
bundle layout stays discoverable, which means the packaged binary is simply
"whichever build flavor ran last" — and the two flavors differ in flags and
bytes. The July-18 local package shipped the plain-VS DLL without any signal.

Ask: have `ost plugin build` record the output digest it produced, and have
`ost plugin package` warn (or refuse without a flag) when the staged binary
does not match the last recorded `ost plugin build` output.

Acceptance test:

```text
cmake --build build/plain --config Release   # overwrites bundle lib/
ost plugin package <bundle>
-> warning (or error): staged lib/ output does not match last `ost plugin build`
```

### 4.2 P2 — outside-project profile resolution

From a clean directory with `--target cy2026` but no `--profile`, the session
resolved to the mock `core` runtime and failed with `REAL_RUNTIME_REQUIRED`.
The package manifest already declares `requires.capabilities:
[usd-stage-read]`, which only the `usd` profile satisfies. Ask: resolve the
default profile from the package manifest's required capabilities, or name the
resolved profile in the error so the fix is discoverable.

### Observation — within-run gate cannot see across-run drift

The release lane's within-run double-package gate passed on every cell while
across-run digests drifted; the gate compares two packagings of one build, so
link-time stamps were invisible to it. Not a defect — but an `ost`-native
across-build reproducibility check (build twice, compare artifact digests)
would have caught this class a release earlier.

## 5. Project-side follow-up

- `/Brepro` is applied to `gaussianCore` and the plugin; hosted observation of
  across-run digest stability on Windows remains a roadmap item.
- The macOS across-run difference is uninvestigated; the suspected analog is
  Mach-O `LC_UUID`/link-time stamps.
- Manual-activation findings (the `python313.dll` requirement for `usdcat`,
  absolute `LibraryPath` after extraction scan) are documented in INSTALL.md.

## 6. Conclusion

The packaged-consumer story holds up away from the worktree: the archive is a
self-describing bundle that discovers, loads, and opens fixtures from a clean
directory, with or without OST composing the session. The reproducibility
finding splits cleanly into a project-side toolchain fix that is done and
proven locally, and one real upstream seam: packaging trusts `lib/` without
provenance.
