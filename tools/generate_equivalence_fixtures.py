# SPDX-License-Identifier: Apache-2.0
"""Generate paired PLY/SPZ fixtures that encode one identical source model.

Both bundles decode into the same format-independent `GaussianCloudData`
(GAUSSIAN_MODEL_CONTRACT.md). This script defines Gaussians *in that shared
model space* — RDF reference frame, linear positive scales, opacity in [0, 1],
normalized scalar-first quaternions, Gaussian-major RGB spherical harmonics —
and then encodes the same values twice:

  * to a Graphdeco-style binary PLY, by inverting PLY_MAPPING.md §3-§4
    (log scales, opacity logit, channel-major `f_rest_*`); and
  * to an SPZ v2/v3 container, by inverting SPZ_MAPPING.md §3-§6 (RDF->RUB
    sign flips, then the reference quantization).

`tests/equivalence/test_equivalence.cpp` decodes both members of a pair and
asserts the two clouds agree within the quantization envelope documented in
EQUIVALENCE.md. Because the PLY side is float32-exact, the entire error budget
belongs to SPZ quantization.

The SPZ encoders are imported from the `gaussian-spz` bundle's fixture
generator rather than reimplemented here, so the reference quantization
formulas exist once in the repository and cannot drift between the SPZ decoder
suite and this one. That is safe as a cross-check because the PLY side is an
independent implementation: a compensating encoder/decoder bug on the SPZ side
would still show up as disagreement with PLY.
"""

from __future__ import annotations

import importlib.util
import math
import pathlib
import struct
import sys

REPO = pathlib.Path(__file__).parents[1]
ROOT = REPO / "tests" / "equivalence" / "fixtures"


def _load(name: str, path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


spz = _load(
    "_spz_fixtures",
    REPO / "plugins" / "gaussian-spz" / "tools" / "generate_fixtures.py")
ply = _load(
    "_ply_fixtures",
    REPO / "plugins" / "gaussian-ply" / "tools" / "generate_fixtures.py")

FRACTIONAL_BITS = 12

# SPZ_MAPPING.md §5: the RUB<->RDF rest-coefficient sign table, bands 1-3.
# The flip is its own inverse, so the same table encodes and decodes.
FLIP_SH = (
    -1.0, -1.0, 1.0,
    -1.0, 1.0, 1.0, -1.0, 1.0,
    -1.0, 1.0, -1.0, -1.0, 1.0, -1.0, 1.0,
)


# --------------------------------------------------------------------------
# Source model
# --------------------------------------------------------------------------

class Gaussian:
    """One Gaussian in shared-model space (RDF, linear scale, opacity 0-1)."""

    def __init__(self, position, scale, rotation, opacity, dc, rest):
        self.position = position          # (x, y, z)
        self.scale = scale                # linear, strictly positive
        self.rotation = rotation          # normalized (w, x, y, z)
        self.opacity = opacity            # [0, 1], never 0 or 1
        self.dc = dc                      # (r, g, b)
        self.rest = rest                  # [(r, g, b)] per rest coefficient


def on_grid(
    position_units,     # position = n / 2^FRACTIONAL_BITS
    log_scale_steps,    # scale = exp(m / 16)
    alpha_steps,        # opacity = n / 255
    dc_bytes,           # dc = (b - 127.5) / (0.15 * 255)
    sh_bytes,           # rest = (b - 128) / 128
    rotation,
) -> Gaussian:
    """Build a Gaussian whose every attribute except rotation sits exactly on
    an SPZ quantization point, so the observed disagreement is float32 noise
    rather than quantization. Rotation has no exact grid: `first-three` stores
    w implicitly, so even the identity quaternion round-trips inexactly."""
    return Gaussian(
        position=tuple(n / (1 << FRACTIONAL_BITS) for n in position_units),
        scale=tuple(math.exp(m / 16.0) for m in log_scale_steps),
        rotation=rotation,
        opacity=alpha_steps / 255.0,
        dc=tuple((b - 127.5) / (spz.COLOR_SCALE * 255.0) for b in dc_bytes),
        rest=[tuple((b - 128) / 128.0 for b in triple) for triple in sh_bytes])


def normalized(quaternion):
    w, x, y, z = quaternion
    norm = math.sqrt(w * w + x * x + y * y + z * z)
    return (w / norm, x / norm, y / norm, z / norm)


def exact_degree3() -> list[Gaussian]:
    """Four Gaussians at degree 3 (15 rest coefficients), every attribute but
    rotation on an exact quantization point.

    Values are distinct across Gaussian, coefficient, and channel so a stride
    error, a coefficient swap, or a channel swap changes the result. Signs are
    mixed on every axis so the RDF/RUB flips cannot cancel out. Quaternions
    keep `w` well away from zero: `first-three` reconstructs it as
    `sqrt(1 - x^2 - y^2 - z^2)`, whose error grows without bound as w -> 0.
    """
    rotations = [
        normalized((1.0, 0.0, 0.0, 0.0)),        # identity
        normalized((0.9, 0.3, -0.2, 0.1)),       # small tilt, w dominant
        normalized((0.6, -0.5, 0.5, -0.377)),    # w still largest
        normalized((0.5, 0.5, -0.5, 0.5)),       # four-way tie
    ]
    out = []
    for i, rotation in enumerate(rotations):
        # Positions span both signs on every axis and stay off round numbers.
        position_units = (
            (i - 1) * 1024 + 37,
            (1 - i) * 2048 - 61,
            (i * i - 2) * 512 + 13)
        # Log-scale steps map to bytes m + 160, kept well inside 0..255.
        log_scale_steps = (-16 + i, 8 - 2 * i, i * 3)
        # Distinct DC bytes per Gaussian and per channel.
        dc_bytes = (40 + 7 * i, 128 + 3 * i, 210 - 11 * i)
        # 15 rest coefficients, three channels each, all distinct.
        #
        # Byte 0 is excluded. SH dequantizes as `(byte - 128) / 128`, so the
        # representable range is asymmetric — [-1.0, +0.9921875] — and byte 0
        # is the one grid point whose negation is *not* on the grid. A rest
        # coefficient of -1.0 whose band sign flip is negative would be
        # encoded as +1.0 and clamp back to +0.9921875, which is a property of
        # the SPZ encoding rather than a decoder disagreement. Staying in
        # 1..255 keeps every value representable in both sign conventions.
        sh_bytes = [
            (1 + (13 * k + 5 * i) % 255,
             1 + (200 - 9 * k + 4 * i) % 255,
             1 + (128 + 17 * k - 6 * i) % 255)
            for k in range(15)]
        out.append(on_grid(
            position_units=position_units,
            log_scale_steps=log_scale_steps,
            alpha_steps=25 + 60 * i,
            dc_bytes=dc_bytes,
            sh_bytes=sh_bytes,
            rotation=rotation))
    return out


def off_grid() -> list[Gaussian]:
    """Three degree-1 Gaussians deliberately *between* quantization points, so
    the pair proves the documented half-step envelope holds for arbitrary
    input rather than only for values chosen to round-trip cleanly.

    Values sitting exactly on a half-step boundary are avoided on purpose: at
    the boundary the encoder's rounding rule decides the result, so the
    measured error would land exactly on the tolerance and the assertion would
    be a coin flip rather than a check.
    """
    return [
        Gaussian(
            position=(0.1234567, -2.7182818, 3.1415927),
            scale=(0.0123456, 1.7320508, 0.3333333),
            rotation=normalized((0.7071068, 0.1234, -0.5678, 0.2345)),
            opacity=0.4444444,
            dc=(0.1111111, -0.7777777, 1.2345678),
            rest=[(0.0987654, -0.3216549, 0.6543210),
                  (-0.8765432, 0.2468013, -0.1357924),
                  (0.5555555, -0.6666666, 0.0246813)]),
        Gaussian(
            position=(-12.3456789, 0.0000305, 7.7777777),
            scale=(2.7182818, 0.0500001, 11.1111111),
            rotation=normalized((0.5773503, -0.5773503, 0.5773503, 0.0616)),
            opacity=0.0072,
            dc=(-1.6180339, 0.0041, 0.8090169),
            rest=[(-0.4999999, 0.5000001, -0.0000001),
                  (0.7654321, -0.2345678, 0.9012345),
                  (-0.1928374, 0.5647382, -0.7361829)]),
        Gaussian(
            position=(0.0, -0.00019, 1000.5),
            scale=(0.9999999, 1.0000001, 0.7071068),
            rotation=normalized((0.9238795, 0.0, 0.3826834, 0.0)),
            opacity=0.9931,
            dc=(0.3333333, 0.6666666, -0.9999999),
            rest=[(0.0123456, 0.7891011, -0.1213141),
                  (-0.5161718, 0.1920212, 0.2223242),
                  (0.2526272, -0.8293031, 0.3233343)]),
    ]


# --------------------------------------------------------------------------
# PLY encoding — the inverse of PLY_MAPPING.md §3-§4
# --------------------------------------------------------------------------

def logit(opacity: float) -> float:
    return math.log(opacity / (1.0 - opacity))


def write_ply(name: str, gaussians: list[Gaussian]) -> None:
    rest_count = len(gaussians[0].rest)
    properties = ply.BASE_PROPERTIES + tuple(
        f"f_rest_{i}" for i in range(rest_count * 3))
    rows = []
    for g in gaussians:
        row = [
            *g.position,
            *(math.log(s) for s in g.scale),
            *g.rotation,
            logit(g.opacity),
            *g.dc,
        ]
        # Graphdeco stores rest coefficients channel-major: all red, then all
        # green, then all blue (PLY_MAPPING.md §4).
        for channel in range(3):
            row.extend(g.rest[k][channel] for k in range(rest_count))
        rows.append(tuple(row))
    (ROOT / name).write_bytes(
        ply.header(len(gaussians), properties) + ply.pack_rows(rows))


# --------------------------------------------------------------------------
# SPZ encoding — the inverse of SPZ_MAPPING.md §3-§6
# --------------------------------------------------------------------------

def to_rub(vector):
    """RDF -> RUB is the same Y/Z negation in both directions."""
    x, y, z = vector
    return (x, -y, -z)


def write_spz(name: str, gaussians: list[Gaussian], version: int) -> None:
    rest_count = len(gaussians[0].rest)
    degree = int(round(math.sqrt(rest_count + 1))) - 1

    stream = spz.spz_header(
        version, len(gaussians), degree, fractional_bits=FRACTIONAL_BITS)

    for g in gaussians:
        position = to_rub(g.position)
        if version == 1:
            stream += spz.enc_position_v1(position)
        else:
            stream += spz.enc_position_v2(position, FRACTIONAL_BITS)
    for g in gaussians:
        stream += spz.enc_alpha(g.opacity)
    for g in gaussians:
        # Band 0 is rotation-invariant, so DC carries no flip.
        stream += spz.enc_color(g.dc)
    for g in gaussians:
        stream += spz.enc_scale(tuple(math.log(s) for s in g.scale))
    for g in gaussians:
        # The quaternion's vector part flips with the frame; the real part
        # does not (SPZ_MAPPING.md §5).
        w, x, y, z = g.rotation
        flipped = (w,) + to_rub((x, y, z))
        if version >= 3:
            stream += spz.enc_rotation_smallest_three(flipped)
        else:
            stream += spz.enc_rotation_first_three(flipped)
    stream += spz.enc_sh_stream(
        [[tuple(FLIP_SH[k] * channel for channel in g.rest[k])
          for k in range(rest_count)]
         for g in gaussians])

    (ROOT / name).write_bytes(spz.gzip_member(stream))


# --------------------------------------------------------------------------

def main() -> None:
    ROOT.mkdir(parents=True, exist_ok=True)

    # Degree 3 on the quantization grid: the sharp structural check. Every
    # rest coefficient is present, so all 15 sign flips and both SH layouts
    # (PLY channel-major, SPZ Gaussian-major) are pinned against each other.
    exact = exact_degree3()
    write_ply("equiv-degree3-exact-binary-le.ply", exact)
    write_spz("equiv-degree3-exact-v2.spz", exact, version=2)
    # The same source model through the v3 smallest-three rotation encoding.
    # Both SPZ files share one PLY partner, so a v3-only disagreement is
    # attributable to the rotation path and nothing else.
    write_spz("equiv-degree3-exact-v3.spz", exact, version=3)

    # Arbitrary off-grid values: proves the documented envelope, not just the
    # round-trip of hand-picked points.
    arbitrary = off_grid()
    write_ply("equiv-degree1-offgrid-binary-le.ply", arbitrary)
    write_spz("equiv-degree1-offgrid-v2.spz", arbitrary, version=2)


if __name__ == "__main__":
    main()
