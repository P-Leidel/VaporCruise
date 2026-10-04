# Domain vocabulary

VaporCruise 1.0.0 is an autonomous Windows synthwave drive. The application has no car interior, audio, telemetry, installer, or updater.

- **Scene:** dark marked highway, regional neon terrain, a distant procedural skyline, faceted mountains, baked stars, striped sunset and drifting fog. Magenta, cyan and amber share palette roles and smooth transitions.
- **Cruise position:** a 64-bit integer travel cell and bounded fractional phase. Terrain generation uses an integer seed and section coordinates; uploaded vertices use camera-relative coordinates.
- **Scene state:** `SceneState` owns palette transitions, position, terrain seed, a terrain window and the bounded 42-second city-light/fog clock. Pause freezes motion while palette and hint transitions can settle.
- **Scene frame:** a synchronous snapshot borrows terrain meshes from scene state. The renderer consumes it during the current call; it does not retain those borrowed views.
- **Renderer:** Direct3D 11 scene rendering, terrain buffers, background artwork, embedded Release shaders and diagnostic measurement. Debug can compile source HLSL and optionally use the D3D11 debug layer.
- **Frame loop:** app policy and Win32 scheduling coordinate rendering, occlusion, pause, resize, fullscreen, focus and recovery.
- **Frame limit:** playback follows display refresh by default; the optional cap limits submissions to at most 60 per second. GPU query intervals and submitted frames do not establish displayed cadence.
- **Preview:** a captured GPU image or GIF assembled from sampled captures. It is not loaded by the application and does not measure live display performance.

See [run instructions](README.md), [build and verification](docs/current/build-and-verification.md), and [release scope](docs/current/v1-release-notes.md).
