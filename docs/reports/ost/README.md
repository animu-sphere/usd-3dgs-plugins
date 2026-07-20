# OST dogfooding reports

This repository is built end to end with
[OpenStrata](https://github.com/animu-sphere/open-strata) (`ost`). These reports
are dated records of real usage and upstream feedback, following the append-only
series maintained by `usd-vrm-plugins`.

The newest report carries the live observations. Reports are not rewritten when
a later OST release changes the outcome; a new report verifies the change and
links back.

| # | Date | Report | OST | Focus |
| --- | --- | --- | --- | --- |
| 1 | 2026-07-18 | [Bootstrap and Gaussian PLY vertical slice](01-2026-07-18-v0.18.0-bootstrap.md) | 0.18.0 | Empty repo through workspace library composition, L0-L5, CI generation, package, and package-origin test |
| 2 | 2026-07-19 | [Packaged-consumer closure and digest reproducibility](02-2026-07-19-package-provenance-and-reproducibility.md) | 0.18.0 | Clean-directory extracted-package run, manual Windows activation, `/Brepro` reproducibility fix, package provenance ask |
| 3 | 2026-07-20 | [L5 golden portability, and a misdiagnosis of it](03-2026-07-20-l5-golden-portability.md) | 0.18.0 | Goldens are portable — `ost` normalizes the doc block; `--skipSourceFileComment` breaks the comparison; two documentation asks |

Open work found by the series is tracked in the
[roadmap](../../roadmap/); completed project work moves to the
[delivery history](../delivery-history.md).

