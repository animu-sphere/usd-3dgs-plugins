# Roadmap triage — 2026-11-17

This records the disposition of stale work during the v0.10.0 roadmap
reorganization. Follow the linked sources for evidence and any ongoing work.

| Former roadmap item | Disposition | Basis |
| --- | --- | --- |
| P0–P2 and M5 | Done | [v0.2.0](../releases/v0.2.0.md) records the PLY baselines, metadata reads, and dialects. [v0.3.0](../releases/v0.3.0.md) records SPZ delivery. |
| SOG M1 and real assets | Done | [v0.5.0](../releases/v0.5.0.md) records both one-object layouts. The committed SOG corpus has a [measured baseline](../reference/PERFORMANCE_BASELINES.md#results--sog). |
| v0.6.0 hardening | Done | [v0.6.0](../releases/v0.6.0.md) links the input-budget, resolver, benchmark, and product-install evidence. |
| Aggregate package and generic SBOM/provenance task | Done | The [v0.8.0 artifact audit](../releases/v0.8.0.md#artifacts) verified the three published products, member manifests, SPDX SBOMs, provenance, notices, and checksums. Evidence is bundled in the product; standalone public sidecars were not verified. The [v0.9.0 record](../releases/v0.9.0.md) covers the later publication. |
| Clean machine checks | Open | Hosted package-origin and install tests pass; independent manual activation on every supported OS is separate. See the [package backlog](../roadmap/backlog.md#package-verification). |
| Benchmark harness | Done | The [hosted corpus gate](../reference/BENCHMARK_CORPUS.md#hosted-gate) runs the harness, and [phase timings](../reference/BENCHMARK_CORPUS.md#phase-timings) are available. Peak process RSS alone does not isolate temporary allocations; that follow-up is in the [backlog](../roadmap/backlog.md#performance-and-loading). |
| Renderer capability note and v0.9.0 public preview | Done | The [capability note](../reference/CAPABILITY_MATRIX.md#hydra-rendering) links the locked Formation and clean-store viewport evidence. The [v0.9.0 release record](../releases/v0.9.0.md) records tag CI and the separate published import products. |
| Ongoing renderer conformance | Transferred | `hydra-merlin` owns rendering; see its [host smoke test PR](https://github.com/animu-sphere/hydra-merlin/pull/147) and the [repository boundary](../architecture/API_BOUNDARY.md#repository-ownership). |
| Plugin milestone for external API/ABI stability | Dropped | Integrated guarantees belong to `usd-3dgs-runtime` when defined. The [repository boundary](../architecture/API_BOUNDARY.md#repository-ownership) keeps the plugin's observable contract and review rules. |
| TinyPLY dependency update procedure | Done | The [vendoring policy](../design/DESIGN_POLICY.md#15-tinyply-dependency-policy) defines the procedure and the [parser verification](../reference/BENCHMARK_CORPUS.md#vendored-tinyply-30-verification) records an actual update. Future dependencies follow the [decoder guide](../contributing/ADDING_A_FORMAT_DECODER.md). |
| Worker-thread authoring workaround | Needs evidence | The file-format entry points still use the worker hop. Prove direct authoring and reload are safe on the supported SDKs before removing it; see the [revisit criteria](../roadmap/backlog.md#performance-and-loading). |
| Compressed PLY, SPLAT, KSPLAT, SPZ v4 | Needs evidence | Moved to the [v0.10.0 candidate evaluation](../roadmap/release-plan.md#candidate-evaluation); no candidate gains support by being listed. |
| Streamed SOG/LOD, LCC/LCC2, glTF/GLB | Open | The [unscheduled format section](../roadmap/backlog.md#other-format-candidates) records the technical and evidence prerequisites. |
