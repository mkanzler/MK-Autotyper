# Releasing

## Local preparation

1. Review the source and documentation. Check that version resources, notes and packaging agree.
2. Capture current screenshots with neutral sample content; exclude personal data.
3. Run `scripts\Build-Release.ps1`. Pass `-PlatformToolset` when overriding the project default.
4. Review ZIP contents, hashes, executable metadata and [manual checks](testing.md#manual-release-checks).

The script is local only: it does not modify Git history or contact GitHub. It refuses to overwrite an existing release directory; select another `-OutputDirectory` for a fresh run.

The script produces direct x64/x86 EXEs and optional ZIP bundles. Each EXE embeds the full GPLv3 license; each Windows ZIP contains the same executable, quick-start text, license and checksums. A separate license asset is also provided. The source archive uses an explicit allowlist, excluding build outputs, local settings, Git history, test data and credentials. Publish the corresponding source ZIP alongside the binaries.

Release builds use a static C++ runtime, `asInvoker` permissions and version metadata. There is no packing or obfuscation. Apply trusted Authenticode signing before packaging/checksums if available; signing afterward changes hashes.

## GitHub publication

After local review:

1. Commit and push the intended release source/assets.
2. Create tag **release-2.0** on that exact commit.
3. Create a GitHub release using `docs/releases/v2.0.md` for its notes.
4. Attach both Windows EXEs, both Windows ZIPs, the source ZIP, `LICENSE` and `SHA256SUMS.txt`.
5. Check README download links, then publish when ready.

Do not tag an older commit while release changes exist only in a working tree. Review licensing and outstanding manual checks before publication. Antivirus results must refer to exact final hashes and must not be described as a guarantee of acceptance.
