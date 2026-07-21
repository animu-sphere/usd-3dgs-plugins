# USD 3DGS Plugins documentation

Documentation is organized by responsibility. This follows the same taxonomy
as `usd-vrm-plugins`, so current contracts, procedures, plans, and historical
evidence do not drift into one another.

When a summary disagrees with the implementation, the implementation wins and
the summary is a documentation bug. When a summary disagrees with
[architecture/WORKSPACE.md](architecture/WORKSPACE.md) about structure, the
workspace contract wins; structural changes must update that contract first.

| Category | Answers | Start here |
| --- | --- | --- |
| [architecture/](architecture/) | How the workspace is structured and which dependency directions are legal. | [WORKSPACE.md](architecture/WORKSPACE.md) |
| [guides/](guides/) | How to build, test, package, and install the plugin. | [BUILDING.md](guides/BUILDING.md), [INSTALL.md](guides/INSTALL.md) |
| [reference/](reference/) | What is supported and how source data maps to USD. | [CAPABILITY_MATRIX.md](reference/CAPABILITY_MATRIX.md) |
| [roadmap/](roadmap/) | What remains incomplete and what comes next. | [README.md](roadmap/README.md) |
| [releases/](releases/) | Immutable records for tagged releases. | [README.md](releases/README.md) |
| [design/](design/) | Why the project is built this way. | [DESIGN_POLICY.md](design/DESIGN_POLICY.md) |
| [adr/](adr/) | Numbered, immutable architecture decision records. | [0001-model-frame-is-rub.md](adr/0001-model-frame-is-rub.md) |
| [reports/](reports/) | Evidence from real runs, including OST dogfooding. | [README.md](reports/README.md) |
| [contributing/](contributing/) | Contributor and release procedures. | [RELEASE_NOTES_TEMPLATE.md](contributing/RELEASE_NOTES_TEMPLATE.md) |

## Canonical documents

- [design/DESIGN_POLICY.md](design/DESIGN_POLICY.md) defines product intent,
  semantic conversion, scene authoring, validation, performance policy, and
  milestone gates.
- [architecture/WORKSPACE.md](architecture/WORKSPACE.md) is the binding
  structural contract for bundles, libraries, dependency directions, and
  artifacts.
- [reference/CAPABILITY_MATRIX.md](reference/CAPABILITY_MATRIX.md) describes
  what the current tree implements, not what it intends to implement later.
- [reference/GAUSSIAN_MODEL_CONTRACT.md](reference/GAUSSIAN_MODEL_CONTRACT.md)
  is the normative, format-independent contract every decoder targets.
- [reference/PLY_MAPPING.md](reference/PLY_MAPPING.md) is the normative source
  property-to-USD mapping for `gaussian-ply`;
  [reference/SPZ_MAPPING.md](reference/SPZ_MAPPING.md) and
  [reference/SOG_MAPPING.md](reference/SOG_MAPPING.md) are its counterparts for
  `gaussian-spz` and `gaussian-sog`.
- [architecture/API_BOUNDARY.md](architecture/API_BOUNDARY.md) classifies
  every installed header by audience and stability tier — "installed" is not
  "public and stable" before v1.0.0.
- [contributing/ADDING_A_FORMAT_DECODER.md](contributing/ADDING_A_FORMAT_DECODER.md)
  is the end-to-end path for a new format, targeting the shared contract with
  no PLY or SPZ code.
- [adr/0001-model-frame-is-rub.md](adr/0001-model-frame-is-rub.md) fixes the
  canonical RUB reference frame of `GaussianCloudData` and derives the
  RDF↔RUB conversion every affected decoder applies.

## Component documentation

Component-specific usage stays with the component:

| Component | Documentation |
| --- | --- |
| `gaussian-ply` | [plugin README](../plugins/gaussian-ply/README.md) |
| `gaussian-spz` | [plugin README](../plugins/gaussian-spz/README.md) |
| `gaussian-sog` | [plugin README](../plugins/gaussian-sog/README.md) (v0.5.0 skeleton) |
| `gaussianCore` | [library README](../libs/gaussian-core/README.md) |
| `gaussianUsd` | [library README](../libs/gaussian-usd/README.md) |

