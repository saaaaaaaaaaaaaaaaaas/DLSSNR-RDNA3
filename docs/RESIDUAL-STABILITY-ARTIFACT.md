# Local residual stability package

2026-11-23. Runtime implementation source:
`17ed2dfdc5df129d94580a379b8e585aa57b9bde`, branch `feature/z1e-ditto-relight`.
All work and commits remain local. No tag, push, publication or RDNA4 build.

Artifact, relative to this worktree:

```text
artifacts/residual-stability/release/DLSS-NR-AMD-Vulcan-Linux-0.0.9-z1e.1-8-g17ed2df-Z1E-Turbo-Temporal-RDNA-3.tar.gz
```

SHA-256:

```text
ab0d4a96db25bad8a78642ada6827fba56c268ad626a086d49ee4752c91f42bd
```

The adjacent `.sha256` and `release-manifest.json` record every packaged file,
all three PE runtime consumers and the common residual shader hash. This is a
full Linux/Proton x86_64 RDNA3 Z1 Extreme Turbo package, without model weights.
Use the normal installer with the owner's existing/private model inputs.
**Update runtime consumers and shader data together; an ASI-only update is
incompatible with the old residual shader.**

## Build and finalization

`bash linux/build/build_z1e_release.sh artifacts/residual-stability` compiled
the three consumers and all six Turbo network variants from the source above.
The stricter new audit caught that the old assembler shipped adapters only in
the base network folder. The completed staging tree was finalized with
`linux/build/finalize_z1e_package.py`, then repacked using the implementation
commit's `SOURCE_DATE_EPOCH` and the normal sorted tar/gzip settings. No consumer
binary or learned shader was changed during finalization.

The finalizer duplicates the base adapters into all five variant folders and
includes the new stability documentation. This follow-up packaging commit
hooks that same finalizer into future release builds. Running it twice preserves
all 35 audited runtime/shader/document files byte for byte.

Final checks:

- Model-free archive audit: 1203 files; no model containers, unexpected tensors
  or known NVIDIA DLLs.
- SPIR-V validation: 442 shaders; all six folders have the same 19-descriptor,
  60-byte-push residual ABI.
- Three x86_64 PE runtime consumers have the ABI rejection guard and no static
  Vulkan import.
- 370 learned/grade shader files match the previous `v0.0.9-z1e.1` package
  byte for byte.
- CPU descriptor, presentation, QKV/planner, INI defaults and temporal cadence
  checks pass. Original goldens and protected network sources remain unchanged.
- GPU checks and measured costs are in
  [Residual stability](RESIDUAL-STABILITY.md) and its
  [validation record](RESIDUAL-STABILITY-VALIDATION.json): 21 analytic cases,
  Lighting-off Motion N=1–4, reverse Z, recreation and FG notifications.

Actual game/HDR/FG presentation remains unqualified. The new approximation
increases reconstruction time/memory and slightly increases Motion's exposed
blue-edge error; those measured tradeoffs are retained in the validation record.
