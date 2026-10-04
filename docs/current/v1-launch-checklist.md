# Release verification checklist

The release tag is `v1.0.0`. Verify the exact source commit, executable and archive using the external release manifest and `SHA256SUMS`; a tag name alone does not verify downloaded bytes.

- Build and run Debug and Release CTest using [build and verification](build-and-verification.md). Enable CPU verification to match the Windows CI scope.
- Reconfigure after committing source changes. Check `vaporcruise-build.json`: version 1.0.0, clean source, matching full Git revision, Release, x64, static runtime and matching executable SHA-256.
- Run current GPU visual suites on a suitable Windows graphics host. Current stills should preserve the approved magenta, cyan and amber images in the [preview manifest](progress/visuals/landscape-effects-2026-10-04/preview-manifest.json).
- Package the clean committed Release. Verify eight flat files: executable, README, license, dependency notices, three palette PNGs and `motion-preview.gif`.
- Launch the extracted executable from its own directory. Record executable and captured-image hashes separately; packaging integrity does not establish runtime behavior.
- Record a hosted Windows CI result after pushing source. The workflow runs tests and performs no publication.

The tested reference host is Windows 10 22H2 with a GTX 1660 SUPER. Windows 11, integrated graphics, a clean machine, real sleep/resume and lock/unlock, mixed-DPI monitor changes, real driver removal and the two-hour soak remain unverified. Synthetic lifecycle tests and hidden captures do not establish those scenarios. See [release notes](v1-release-notes.md) for scope.

The portable executable is unsigned. It ships without an installer or updater. Microsoft runtime terms apply to the linked support code; the distributor must have the applicable toolchain license.
