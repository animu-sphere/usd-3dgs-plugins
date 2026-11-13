# Import diagnostics

Find the bracketed code from an import message in the catalog for its prefix.
Each JSON entry records the severity, a summary, and an action. Installed
bundles carry the same file at `plugin/resources/<bundle>/diagnostics.json`.

| Code prefix | Catalog |
| --- | --- |
| `GSPLY-` | [Gaussian PLY](../../plugins/gaussian-ply/plugin/resources/gaussian-ply/diagnostics.json) |
| `GSPZ-` | [Niantic SPZ](../../plugins/gaussian-spz/plugin/resources/gaussian-spz/diagnostics.json) |
| `GSSOG-` | [PlayCanvas SOG](../../plugins/gaussian-sog/plugin/resources/gaussian-sog/diagnostics.json) |

Errors start with a stable `E` code and warnings with a stable `W` code. Match
the code in automation so message wording can evolve. The bundle owns the codes
for its reader, decoder, and USD authoring failures. Shared code supplies the
message format and the layer writer, which uses codes supplied by the bundle.
File-open failures identify the source asset. A SOG companion failure can also
identify the plane name and the path used to resolve it.

## Inspect and validate from the command line

From the repository root on PowerShell, run an import with its summary enabled
and validate the flattened USD:

```powershell
$env:TF_DEBUG = 'GSSOG_IMPORT_STATS'
ost plugin run plugins/gaussian-sog -- usdcat --flatten --out build/inspect.usda plugins/gaussian-sog/tests/fixtures/kit-one-degree0.sog
ost plugin run plugins/gaussian-sog --no-inject -- usdchecker build/inspect.usda
Remove-Item Env:TF_DEBUG
```

Use `GSPLY_IMPORT_STATS` for PLY or `GSPZ_IMPORT_STATS` for SPZ. A successful
full import reports the Gaussian count, SH degree, bounds, warning count, and
read, decode, and authoring times. Warnings that affect many Gaussians are
aggregated by the reader. The summary reports the number of warning messages.

Run `usdchecker` on the USD output for all three formats. A bundled SOG is a
ZIP container, so checking it directly applies USDZ archive rules to its SOG
entries.
