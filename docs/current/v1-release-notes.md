# VaporCruise 1.0.0 release notes

The release manifest binds version 1.0.0 to its exact source commit and executable/archive SHA-256 values. Verification outputs retain those identities separately from source.

VaporCruise is a native Windows synthwave drive with a marked dark highway, glowing regional terrain grid, faceted mountains, a distant futuristic skyline, sparse city lighting, drifting mist, baked stars, and a striped sunset. Magenta, cyan, and amber palettes share smooth transitions. The unobstructed landscape fills the window; earlier car interiors and the C selector have been removed.

The portable package contains the standalone x64 Release executable, run instructions, MIT license, dependency notices, three actual-GPU palette stills, and a sampled motion preview. Release embeds its shaders and uses the static MSVC runtime. No installer, automatic updater, audio, or telemetry integration is included.

Controls: P changes palette, F toggles fullscreen, Space pauses motion, H shows hints, L toggles the optional 60 FPS cap, Esc leaves fullscreen, and Alt+F4 closes. Alt+Space opens the native window menu. Maximize uses the monitor work area; fullscreen restores the previous window placement. Palette/hint transitions can finish while motion is paused.

Requires Windows 10 version 1703 or later, x64, and Direct3D 11 graphics. The tested reference host uses Windows 10 22H2 and a GTX 1660 SUPER. Windows 11, integrated graphics, clean-machine compatibility, real sleep/lock/multi-monitor/driver scenarios, and the deferred two-hour soak have not been established. Historical frame-rate measurements remain tied to their recorded executable hashes and hardware. Sampled GIF previews do not measure live frame cadence.

See the [public README](../../README.md) for run instructions, [build and verification](build-and-verification.md) for source commands, and [third-party notices](../../THIRD-PARTY-NOTICES.md) for provenance and runtime terms.
