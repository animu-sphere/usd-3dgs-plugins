# gaussianCore

`gaussianCore` is the format-independent plain C++ library shared by 3DGS
decoders. It is described to OpenStrata by `openstrata.library.yaml` and exported
as the CMake target `openstrata::gaussianCore`.

It owns:

- `GaussianCloudData` and POD value types;
- stable sigmoid and logarithmic-scale conversion;
- quaternion normalization;
- SH-degree inference;
- Gaussian array validation.

It deliberately does not own:

- plugin registration or `plugInfo.json`;
- tinyPLY or any source-format parser;
- OpenUSD types or stage authoring;
- rendering or GPU upload.

Standalone build and test:

```sh
cmake -S libs/gaussian-core -B libs/gaussian-core/build/local
cmake --build libs/gaussian-core/build/local
ctest --test-dir libs/gaussian-core/build/local --output-on-failure
```

In the normal workspace flow, `ost plugin build plugins/gaussian-ply` resolves,
builds, and installs this library before configuring the plugin consumer.
