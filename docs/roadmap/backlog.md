# Backlog

Open work without a release assignment. The [current work](current.md) page
owns work assigned to the active milestone. Each item below names the repo
responsible for the next decision and the evidence needed to revisit it.

## Other format candidates

| Work | Owner | Revisit or close when | Reference |
| --- | --- | --- | --- |
| LCC / LCC2 | `usd-3dgs-plugins` | An independently implementable specification, license, and redistributable fixtures are available. | [Format selection policy](../design/DESIGN_POLICY.md#4-format-selection) |
| glTF/GLB Gaussian extensions | `usd-3dgs-plugins` | A specific extension and implementation reference justify an ADR that settles scene graph, external resources, SPZ interaction, and ownership with the glTF importer. Implement a slice only after that decision. | [glTF policy](../design/DESIGN_POLICY.md#41-gltf--glb-gaussian-splatting) |
| Streamed SOG and LOD | `usd-3dgs-plugins` | A separate composition proposal defines resolver and chunk lifecycle, LOD selection, payloads, deferred loading, and measurable demand. | [SOG capabilities](../reference/CAPABILITY_MATRIX.md#sog-input-and-semantic-mapping) · [composition guide](../guides/COMPOSITION.md) |
| `.splat` | `usd-3dgs-plugins` | A user request or representative asset names a producer. A spike identifies the variants and their provenance, what the format preserves (SH, opacity, coordinate conventions), and a routing rule that does not clash with other formats. Not selected for v0.10.0 because compressed PLY meets the release goal. | [Candidate evaluation](release-plan.md#candidate-evaluation) |
| `.ksplat` | `usd-3dgs-plugins` | A user request or representative asset names a producer. A spike establishes the version and compression scheme, an independently implementable reference, and redistributable fixtures. Not selected for v0.10.0 because compressed PLY meets the release goal. | [Candidate evaluation](release-plan.md#candidate-evaluation) |
| SPZ v4 | `usd-3dgs-plugins` | A producer writes v4 by default or a representative v4 asset is requested, and an ADR accepts a vendored Zstandard decoder, its licence review, and the package change on every target. Not selected for v0.10.0 because of that dependency cost. | [SPZ container scope](../reference/SPZ_FORMAT.md#3-container-version-scope) · [compressed PLY spike §8](../reports/format-spike-compressed-ply-2026-11-17.md#8-other-candidates) |
| Compressed PLY 12-property `chunk` | `usd-3dgs-plugins` | A real producer of the colourless variant is found. SplatTransform's reader accepts it; today it is rejected with `GSPLY-E021`. | [Compressed PLY mapping](../reference/COMPRESSED_PLY_MAPPING.md#2-container-layout) |
| Render-mode tags in uncompressed PLY | `usd-3dgs-plugins` | Brush and Postshot write the same `SplatRenderMode` and `postshot.anti_aliasing` tags in Graphdeco PLY. Decide whether `GSPLY-W004` should apply there too; it would add a warning to imports that are silent today. | [Compressed PLY mapping §9](../reference/COMPRESSED_PLY_MAPPING.md#9-render-mode-tags) |
| Binary big-endian PLY | `usd-3dgs-plugins` | A representative asset justifies the work; decoder and stage fixtures establish the behavior on the supported targets. | [PLY dialects](../reference/PLY_DIALECTS.md) |

## Performance and loading

| Work | Owner | Revisit or close when | Reference |
| --- | --- | --- | --- |
| Attribute peak temporary allocations | `usd-3dgs-plugins` | A reproducible allocation profile can separate parser and decoder temporaries from the retained USD layer on the same benchmark inputs. | [Benchmark corpus](../reference/BENCHMARK_CORPUS.md) |
| Investigate memory mapping or reduced-copy reads | `usd-3dgs-plugins` | Per-phase measurements show material I/O or copying cost; compare measurements against the baseline before proposing a change. | [Phase timings](../reference/BENCHMARK_CORPUS.md#phase-timings) |
| Re-evaluate worker-thread authoring | `usd-3dgs-plugins` | On the next OpenUSD re-pin, add a regression test for direct detached-stage authoring under an outer `SdfChangeBlock`. Remove the hop only if full and metadata-only reads and reload preserve the stage contract across PLY, SPZ, and SOG on supported builds. | [Workaround policy](../design/DESIGN_POLICY.md#75-worker-thread-authoring-workaround) · [OpenUSD configurations](../reference/SUPPORTED_CONFIGURATIONS.md) |

## USD contract and tooling

| Work | Owner | Revisit or close when | Reference |
| --- | --- | --- | --- |
| Authoritative source axes and units | `usd-3dgs-plugins` | A selected format defines valid source metadata and a mapping test demonstrates its effect on the authored stage. | [Stage contract](../architecture/WORKSPACE.md#5-authored-stage-contract) |
| Tighter anisotropic bounds | `usd-3dgs-plugins` | Measurements show the current conservative bound materially affects framing, culling, or traversal. | [Stage contract](../architecture/WORKSPACE.md#5-authored-stage-contract) |
| Multiple clouds, camera or training metadata, animated Gaussians | `usd-3dgs-plugins` | A concrete source format and consumer use case justify an authored hierarchy or time-sampling policy with deterministic tests. | [Model contract](../reference/GAUSSIAN_MODEL_CONTRACT.md) |
| Dedicated `usd3dgs` inspection CLI | `usd-3dgs-plugins` | Define use cases for format identification, machine-readable diagnostics, and import that the existing OpenUSD CLI commands cannot cover. | [Diagnostics](../reference/DIAGNOSTICS.md) |
| USD-to-PLY export | `usd-3dgs-plugins` | A concrete workflow needs export and a proposal defines the write dialect and loss report. | [Write policy](../design/DESIGN_POLICY.md#13-write-support) |
| Compressed USD representations | `usd-3dgs-plugins` | A representative workload shows a measurable benefit and the output can preserve stage semantics without changing the canonical model. | [Model contract](../reference/GAUSSIAN_MODEL_CONTRACT.md) |

## Package verification

| Work | Owner | Revisit or close when | Reference |
| --- | --- | --- | --- |
| Independent clean-host activation | `usd-3dgs-plugins` | Repeat manual activation from each published target archive on a clean host using the documented OpenUSD and Python ABI, checking discovery and stage open. Hosted package-origin and install tests remain the automated gate. | [Manual activation](../guides/INSTALL.md#manual-package-activation) |
| Verify a second OpenUSD SDK point | `usd-3dgs-plugins` | A second target in the declared source range is available with a maintained runtime and a consumer need. Record a separate source and package gate for that ABI. | [Supported configurations](../reference/SUPPORTED_CONFIGURATIONS.md) |
