# RDNA3 0.0.9 trial artifact

- Branch: `port/rdna3-0.0.9` in the owner's GitHub origin.
- Build checkout: `3c9e512d8828ef9d6742fc211e76621caaac4ca8`.
- Production code: `fc18bed8ecd1bdd45056e04b40d58b06ce3efd6c`; runtime,
  shaders and architecture defines are unchanged between these commits.
- Archive: `linux/package/DLSSNR-AMD-Vulkan-Linux-0.0.9-linux-preview-gfc18bed-x86_64-rdna3.tar.gz`.
- Size: **273,169,798 bytes**, **1,157 files**.
- SHA-256: `84bc81d0a8db9326032cad2e23ff1b1e7468658f006bf2b08b72ac4ae6f8ad20`.
- Adjacent `.tar.gz.sha256`: run `sha256sum -c <archive>.sha256` from `linux/package/`.
- Full package recipe completed with **exit 0**:

```sh
NR_GPU=rdna3 NR_ARCH=x86_64 NR_VERSION=0.0.9-linux-preview-gfc18bed \
    bash linux/build/build_package.sh
```

Run from the pinned checkout with its own `bash fetch_deps.sh` dependencies and
`NR_ARCH=x86_64 bash linux/build/build_vulkan_loader.sh` output. The checkout's
`SOURCE_DATE_EPOCH` is `1794577140`; shader compilers are glslang 16.5.0 and Slang
2026.19. The host uses native GCC 16.2.1 and MinGW GCC 16.2.0. This records one
complete final recipe; two fresh full builds or cross-host reproducibility have
not been established.

The package's built-in distribution audit passes: no model containers,
unexpected tensor files or known NVIDIA DLLs. All archived file payloads match
the final staging directory by SHA-256. Tar ownership is 0:0 and all mtimes equal
the source epoch; gzip mtime is zero. Packaged Ultra has `int4-qkv.txt`, the
encoder route, `g_fswin256q.spv` and its INT4 rewrite marker. The qualification
report and JSON are included and match the committed files.

A private installer trial from the first complete package passed in a folder
with Cyrillic letters and a space. The installer, fitted-model scripts/deltas
and INT4 weights generator are byte-identical to the final package. Generated
experimental/optimized/INT4 model hashes and the OptiScaler Interleaved default
match. Only a temporary game directory and private package copy were written;
the actual DD2 installation was read only. Models remain outside Git and all
distribution archives. The owner's DLL extraction was not repeated.

Build log: `build/port-0.0.9/package-clean-final.log`, also retained as
`artifacts/port-0.0.9/package-clean-final.log`. Qualification text logs are in
`artifacts/port-0.0.9/validation-logs.tar.gz`; that archive contains no models
or frame buffers. The earlier interrupted packaging attempt is retained in
`package-final.log`; the clean final recipe above supersedes it.

[Qualification report](RDNA3-PORT-0.0.9.md) and
[per-case results](RDNA3-0.0.9-VALIDATION.json) distinguish the EXACT pre/post
frames from inherited strict-golden failures and unapproved fitted-network
references. This is a **trial candidate**, with no release tag. Game/Proton/FG,
temporal sequences, frame timings and memory qualification remain pending.

[GitHub Actions for the implementation commit](https://github.com/saaaaaaaaaaaaaaaaaas/DLSSNR-RDNA3/actions/runs/43435661819)
was initially in progress at artifact handoff and has now completed successfully
for both RDNA3 and RDNA4 Linux package jobs, including the RDNA3 CPU regression.
Windows and release jobs were skipped. The owner subsequently confirmed that
the delivered build works and requested integration/push on 2026-11-13.
