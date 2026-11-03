# Asset-resolver matrix

How `gaussian-sog` behaves under each way a SOG asset and its companion files
can be named. The unbundled layout drives this matrix: its `meta.json` names
its WebP property planes relative to itself, so every plane is a companion
asset found through the active `ArResolver`. The bundled `.sog` layout has no
companions; only the primary-asset rows apply to it.

This page describes the current tree. A row is **verified** only when an
automated test exercises it; other rows say what the code does and why it is
not yet tested.

Legend: ✅ verified · ⚠️ verified on one platform family only · ⛔ known gap ·
⬜ not covered

## How resolution works

- **The primary asset** (`meta.json` or `.sog`) is resolved by USD like any
  layer. `GaussianSogFileFormat::Read()` receives the resolved path and reads
  it **through `ArResolver::OpenAsset`** (`LoadPrimaryThroughResolver`), so a
  package-relative path or a URI-style identifier is read by the resolver
  that resolved it. The reader probes the first bytes and the size, and
  refuses an asset above its layout's bound from `ArAsset::GetSize()` before
  the bulk read. The PLY and SPZ bundles still read their resolved path from
  the filesystem.
- **Each companion plane** is loaded by `LoadCompanionThroughResolver`:
  `CreateIdentifier("./<plane>", <resolved meta.json>)`, then `Resolve` and
  `OpenAsset`. The explicit `./` matters. A bare `scales.webp` is a
  *search-path* asset path to `ArDefaultResolver`. If it is missing beside
  `meta.json`, the resolver looks for it in the current directory and then on
  the context search path. That would import a plane from another asset. The
  `./` prefix limits the lookup to the directory `meta.json` resolved to.
- **Companions of a packaged `meta.json`** (`scene.usdz[cloud/meta.json]`) are
  anchored by the plugin, not by `CreateIdentifier`. Ar anchors a
  package-relative anchor on its outer package path, so `./scales.webp` would
  name a file beside `scene.usdz`. The plugin joins the directory of the
  innermost packaged path with the plane name instead
  (`scene.usdz[cloud/scales.webp]`).
- **Plane names are restricted before any lookup**: `SogReader` accepts only a
  bare file name. It refuses `/`, `\`, `:`, control characters, `.`, `..`,
  names longer than 255 bytes, and Windows device names. A resolver is never
  given a path or a URI to interpret.
- **Metadata-only reads** (`Read(metadataOnly=true)`) parse `meta.json` alone
  and never ask for a companion.

## Primary asset

| Case | Behavior | Status | Evidence |
| --- | --- | --- | --- |
| Absolute path | opens | ✅ | `test_gaussian_sog_resolver.py` `case_absolute` |
| Path relative to the current directory | opens | ✅ | `case_cwd_relative` |
| Reference from another layer: `./` and `../` anchored paths | opens; companions are anchored on the *resolved* `meta.json`, not on the referencing layer | ✅ | `case_anchored_references` |
| Reference by absolute path written with `/` | opens on every platform | ✅ | `case_anchored_references` |
| Search-path identifier through `ArDefaultResolverContext`, opened directly or by reference | opens | ✅ | `case_search_path` |
| Windows: `\` and `/` spellings of one absolute path | both open | ⚠️ | `case_windows_spellings` (runs on Windows only; POSIX has one spelling) |
| Non-ASCII characters in the path | opens. The USD-free readers open their UTF-8 path through `openstrata::gs::OpenInputFile` (`GaussianFileIO.h`), which on Windows opens the UTF-16 name instead of reinterpreting the bytes in the ANSI code page. PLY and SPZ share the helper. | ✅ | reader `TestNonAsciiPath` (both layouts and the directory loader), `gaussianCore_file_io`, and the PLY and SPZ decoder `TestNonAsciiPath`; observed on Windows (MSVC), macOS, and Linux in the `workspace-ctest-pr-*` CI cells ([WORKSPACE.md §8](../architecture/WORKSPACE.md)) |
| Package-relative path: an unbundled `meta.json` at the root or in a subdirectory of a `.usdz`, opened directly, by reference, or metadata-only; a `.sog` inside a `.usdz` | opens through `ArAsset`; companions are the sibling entries inside the package | ✅ | `case_package_relative`; reader `TestInjectedPrimaryLoader`; observed on Windows, macOS, and Linux |
| Packaged `meta.json` whose plane is missing from the package, with a valid same-named file beside the package | `GSSOG-E008`; the file beside the package is never used | ✅ | `case_package_relative` |
| URI-style identifier served by a custom resolver | read through `ArAsset`; the reader never interprets the identifier | ⬜ | reader `TestInjectedPrimaryLoader` covers the seam with a URI-like name; no real resolver plugin is tested |

## Companion planes (unbundled layout)

| Case | Behavior | Status | Evidence |
| --- | --- | --- | --- |
| Plane beside `meta.json` | loaded through the resolver | ✅ | every unbundled case above |
| Plane missing | `GSSOG-E008`, naming the plane and the `meta.json` it was resolved from | ✅ | `case_missing_companion`; reader `unbundled-missing-plane` |
| Plane missing, but a file with the same name is in the current directory or on the search path | still `GSSOG-E008`; the other file is never used | ✅ | `case_missing_companion_is_not_searched` |
| On-disk name differs only in case | follows the host filesystem: found on case-insensitive volumes (Windows, default macOS), `GSSOG-E008` on case-sensitive ones | ✅ | `case_companion_case` (asserts the branch the volume implies) |
| Name containing `\` (Windows-style subpath) | `GSSOG-E005`; the loader is never called | ✅ | `unbundled-backslash-plane-name`; reader `TestResolverSeam`; `case_unsafe_companion_names` |
| URI-like name (`file:scales.webp`) | `GSSOG-E005`; the loader is never called | ✅ | `unbundled-uri-plane-name`; same tests |
| Name with `..` or an absolute path | `GSSOG-E005` | ✅ | `escaping-plane-name.sog` (same validator) |
| Windows device name (`NUL.webp`) | `GSSOG-E005` on every platform | ✅ | `device-plane-name.sog` |
| Loader-reported failure | the loader's own diagnostic reaches the caller unchanged | ✅ | reader `TestResolverSeam` |
| Loader receives the primary path as anchor and bare plane names, in a fixed order | as stated | ✅ | reader `TestResolverSeam`, `TestInjectedCompanionLoader` |
| Companion larger than 512 MiB | `GSSOG-E016`, checked from `ArAsset::GetSize()` before allocation | ✅ | `case_oversized_companion` (sparse file); reader `TestOversizedUnbundledCompanion` ([IMPORT_LIMITS.md](IMPORT_LIMITS.md#test-coverage)) |
| Custom `ArResolver` plugin serving companions | the seam is verified with an injected loader; no real resolver plugin is tested | ⬜ | reader `TestInjectedCompanionLoader` covers the seam only |
| Resolver that serves a stream without `GetBuffer()` | read through `ArAsset::Read()` | ⬜ | needs a custom resolver |

## Metadata-only reads

| Case | Behavior | Status | Evidence |
| --- | --- | --- | --- |
| Every companion missing | count and SH degree still come from `meta.json`; a full read of the same file fails `GSSOG-E008` | ✅ | `case_metadata_only_skips_companions` |
| Companion loader calls during a metadata-only read | none | ✅ | reader `TestResolverSeam` |

## Where the tests live

- `plugins/gaussian-sog/tests/test_gaussian_sog_resolver.py`: the USD-level
  matrix, run through `ArDefaultResolver`. CTest name:
  `gaussianSog_python_resolver`. Each case copies the fixture into a fresh
  temporary directory, so the layer registry cannot reuse an earlier case's
  result.
- `plugins/gaussian-sog/tests/test_gaussian_sog_reader.cpp`: the
  resolver-independent seams (`TestResolverSeam`,
  `TestInjectedCompanionLoader`, `TestInjectedPrimaryLoader`). It needs no
  USD.

Both run in the `workspace-ctest-pr-*` CI cells on all three hosted
platforms, as well as in the local gate.

Next step for the ⬜ rows: add a minimal test resolver plugin (URI-style
identifiers, companions served by a custom resolver, and a stream without
`GetBuffer()`). The ⚠️ row is Windows-only by nature.
