# Import limits

These are the default resource limits applied before the PLY, SPZ, or SOG
readers allocate unbounded source or model data. They are an input-safety
policy, not a change to the `GaussianCloudData` model contract: the model has
no semantic Gaussian-count ceiling below what the platform can address.

The shared Gaussian ceiling is deliberately above the committed 5.8-million
Gaussian PLY performance asset, while keeping a declared count from turning a
single import into an unbounded allocation request.

| Resource | Limit | Applies to |
| --- | ---: | --- |
| Declared Gaussian count | 8,000,000 | PLY, SPZ, SOG |
| SOG `meta.json` size | 4 MiB | Bundled and unbundled SOG |
| SOG JSON nesting depth | 32 levels | SOG metadata |
| SOG JSON value tokens | 100,000 | SOG metadata |
| SOG bundled archive source size | 1 GiB | Bundled SOG |
| SOG ZIP entries | 64 | Bundled SOG |
| SOG ZIP expanded size | 2 GiB | Bundled SOG |
| SOG ZIP entry / companion input size | 512 MiB | SOG planes |
| SOG decoded plane dimensions | 16,383 x 16,383 pixels | SOG WebP planes |
| SOG decoded RGBA plane size | 512 MiB | SOG WebP planes |
| SPZ source file size | 1 GiB | SPZ v1-v3 |
| SPZ extension records | 64 MiB | SPZ v1-v3 |

The readers also use checked multiplication and allocation helpers for model
arrays, reject zero-count clouds, require every SOG plane to be present and
large enough for the declared count, reject duplicate SOG plane names and ZIP
entries, and retain the SPZ format's `INT32_MAX` representability check. The
unbundled SOG file-format loader applies the plane input bound to the
resolver-provided asset size before allocating a companion buffer; the reader
keeps the same check after the loader returns. The shared 8,000,000 count
ceiling is evaluated after those format-level range checks, so existing
malformed-container diagnostics remain stable.

Limit failures use the format-specific stable diagnostic codes `GSPLY-E019`,
`GSPZ-E015`, and `GSSOG-E016`. A failed import never produces a partial
`GaussianCloudData` or USD layer. `CanRead()` remains a routing decision and
may claim a structurally recognizable file whose `Read()` then rejects a
declared resource limit.

These are compile-time defaults in v0.6.0 preparation. A future host-facing
limits API must preserve the same checked arithmetic and must not silently
turn a rejected input into a partial import.