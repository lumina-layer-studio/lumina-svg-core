# Reproducible benchmark

`tools/benchmark.py` measures the same `lumina_svg_core.analyze_svg()` entry point
used by consumers. It discards one warm-up, records at least three warm runs,
and captures stage timings, peak process RSS, swap growth, and a deterministic
geometry fingerprint.

## Fixture and provenance

`supported_scale_fixture.svg` contains 64 selected source paths repeated 16
times in a disjoint 32-by-32 layout, for 1,024 paths.
`supported_scale_fixture_provenance.json` records the selection and layout.
The source paths and colors were derived from Lumina Studio's
`test_images/benchmark.svg`; the complete derived fixture is included here, so
running it requires no parent checkout or private files.

`proxy_manifest.json` is a historical reference-host measurement, not a speed
guarantee for other machines. Its fixture metadata may retain paths from the
original workspace. Private corpus inputs are not included. An explicitly
supplied input is classified by the caller, not identified by source hashes.

## Run

Install the package in a CPython 3.12 environment and run from this repository's
root:

```bash
python tools/benchmark.py \
  --svg benchmarks/supported_scale_fixture.svg \
  --target-width-mm 111 \
  --tolerance-mm 0.02 \
  --warm-runs 3 \
  --classification derived_supported_scale \
  --output build/benchmark.json
```

Compare equivalent warm runs on the same host and compiler configuration.
Memory/swap measurements are diagnostic. Application mesh generation, material
matching, and slicing are outside this standalone benchmark.
