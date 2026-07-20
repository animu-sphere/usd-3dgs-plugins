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
- [reference/PLY_MAPPING.md](reference/PLY_MAPPING.md) is the normative source
  property-to-USD mapping for `gaussian-ply`;
  [reference/SPZ_MAPPING.md](reference/SPZ_MAPPING.md) is its counterpart for
  `gaussian-spz`.

## Component documentation

Component-specific usage stays with the component:

| Component | Documentation |
| --- | --- |
| `gaussian-ply` | [plugin README](../plugins/gaussian-ply/README.md) |
| `gaussian-spz` | [plugin README](../plugins/gaussian-spz/README.md) |
| `gaussianCore` | [library README](../libs/gaussian-core/README.md) |

