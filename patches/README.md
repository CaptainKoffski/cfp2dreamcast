# Patches

The Flycast instrumentation lives as commits in our fork —
`git@github.com:CaptainKoffski/flycast4naomi2dreamcast.git` (see its
`INSTRUMENTATION.md`; based on upstream
`f09d1f22ef8d199b8b7a2395d0b46774e08a58c2`). `tools/flycast-src/` is a clone
of that fork. The former `patches/flycast-instrument.diff` was absorbed into
the fork's history; regenerate it any time with
`git -C tools/flycast-src diff f09d1f22..master`.

## Syphon submodule patch (still required per fresh setup)

`core/deps/Syphon` is a submodule, so the fork can't carry a commit for it.
After `git submodule update --init --recursive`, apply from the repo root:

```sh
git -C tools/flycast-src/core/deps/Syphon apply "$PWD/patches/flycast-syphon-build-fix.diff"
```

Changes `target_precompile_headers` from `PUBLIC` to `PRIVATE` so Syphon's
ObjC prefix-header PCH doesn't propagate to the Flycast target and break its
macOS OBJC++ build. Build flags and CMake invocation: `docs/kb/tooling.md`.
