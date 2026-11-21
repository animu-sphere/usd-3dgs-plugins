# Backlog

Open work without a release assignment. The [current work](current.md) page
owns work assigned to the active milestone. Each item below names the repo
responsible for the next decision and the evidence needed to revisit it.

## Other format candidates

| Work | Owner | Revisit or close when | Reference |
| --- | --- | --- | --- |
| LCC / LCC2 | `usd-3dgs-plugins` | An independently implementable specification, license, and redistributable fixtures are available. | [Format selection policy](../design/DESIGN_POLICY.md#4-format-selection) |
| glTF/GLB Gaussian extensions | `usd-3dgs-plugins` with `usd-gltf-plugins` | Deferred from v0.12.0 on 2026-11-19. The [FMT-09 spike](../reports/format-spike-gltf-gaussian-splatting-2026-11-19.md) found no technical blocker in the ratified `KHR_gaussian_splatting`, but `usd-gltf-plugins` already claims `.gltf`/`.glb` as the primary file format and owns glTF scene translation. Revisit when the two repositories agree who translates Gaussian primitives, for example after the Gaussian core is extracted as a shared library (see *USD contract and tooling*). Gaussian compression extensions (SPZ, EGSC, MPEG) stay here until one is ratified in the Khronos registry. | [glTF policy](../design/DESIGN_POLICY.md#41-gltf--glb-gaussian-splatting) |
| Streamed SOG and LOD | `usd-3dgs-plugins` | A separate composition proposal defines resolver and chunk lifecycle, LOD selection, payloads, deferred loading, and measurable demand. | [SOG capabilities](../reference/CAPABILITY_MATRIX.md#sog-input-and-semantic-mapping) · [composition guide](../guides/COMPOSITION.md) |
| `.ksplat` | `usd-3dgs-plugins` | A user request or representative asset names a producer. A spike establishes the version and compression scheme, an independently implementable reference, and redistributable fixtures. Not selected for v0.10.0-v0.13.0: no producer or request has been recorded. | [Candidate evaluation](release-plan.md#candidate-evaluation) |
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
| Extract the Gaussian core as a consumable library | `usd-3dgs-plugins` | A second repository, such as `usd-gltf-plugins` for glTF Gaussian primitives, needs to decode or author Gaussians. Today `gaussianCore` and `gaussianUsd` are contributor APIs with no external API or ABI guarantee. A proposal defines the public surface (model, validation, prim-level authoring), versioning, and how packages consume it. | [API boundary](../architecture/API_BOUNDARY.md) |
| Dedicated `usd3dgs` inspection CLI | `usd-3dgs-plugins` | Define use cases for format identification, machine-readable diagnostics, and import that the existing OpenUSD CLI commands cannot cover. | [Diagnostics](../reference/DIAGNOSTICS.md) |
| USD-to-PLY export | `usd-3dgs-plugins` | A concrete workflow needs export and a proposal defines the write dialect and loss report. | [Write policy](../design/DESIGN_POLICY.md#13-write-support) |
| Compressed USD representations | `usd-3dgs-plugins` | A representative workload shows a measurable benefit and the output can preserve stage semantics without changing the canonical model. | [Model contract](../reference/GAUSSIAN_MODEL_CONTRACT.md) |

## Package verification

| Work | Owner | Revisit or close when | Reference |
| --- | --- | --- | --- |
| Independent clean-host activation | `usd-3dgs-plugins` | Repeat manual activation from each published target archive on a clean host using the documented OpenUSD and Python ABI, checking discovery and stage open. Hosted package-origin and install tests remain the automated gate. | [Manual activation](../guides/INSTALL.md#manual-package-activation) |
| Verify a second OpenUSD SDK point | `usd-3dgs-plugins` | A second target in the declared source range is available with a maintained runtime and a consumer need. Record a separate source and package gate for that ABI. Checked 2026-11-21 for v0.13.0: the OpenStrata runtime matrix and `usd-vrm-plugins` pin only OpenUSD 26.08, and the 26.05 runtimes have been withdrawn. | [Supported configurations](../reference/SUPPORTED_CONFIGURATIONS.md) |
