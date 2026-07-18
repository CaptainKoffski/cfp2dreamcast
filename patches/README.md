# Flycast instrumentation patches

Two patches, applied in order after cloning Flycast at the pinned commit
(`f09d1f22ef8d199b8b7a2395d0b46774e08a58c2`).

## 1. Initialise submodules

```sh
git -C tools/flycast-src submodule update --init --recursive
```

## 2. Apply main-tree patch

From the repo root:

```sh
git -C tools/flycast-src apply "$PWD/patches/flycast-instrument.diff"
```

Covers: `CMakeLists.txt`, `core/hw/naomi/cartlog.{cpp,h}` (new files),
`core/hw/naomi/CMakeLists.txt`, `core/hw/naomi/naomi.cpp`,
`core/hw/naomi/naomi_cart.{cpp,h}`, `core/hw/maple/maple_jvs.cpp`,
`core/hw/maple/maple_if.cpp` (Phase 4 Task 4: SHIMWATCH + MIERESP).

## 3. Apply Syphon submodule patch

```sh
git -C tools/flycast-src/core/deps/Syphon apply "$PWD/patches/flycast-syphon-build-fix.diff"
```

Covers: `core/deps/Syphon/CMakeLists.txt` — changes `target_precompile_headers`
from `PUBLIC` to `PRIVATE` so the ObjC PCH does not propagate to the Flycast
target and break the macOS OBJC++ build.

## Notes

- The Syphon patch must be applied **inside the submodule** (`core/deps/Syphon/`),
  not from the Flycast root — the submodule is a separate git repo.
- macOS build flags, required dependencies, and CMake invocation are documented
  in `docs/kb/tooling.md`.
