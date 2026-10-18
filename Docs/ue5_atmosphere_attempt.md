# UE5.8 atmosphere attempt — problem record

Canonical copy (kept with the PUG docs tree):

`C:\Users\luka\Desktop\SINGULARITY\DOCS\WiP\ue5_atmosphere_attempt.md`

**Summary:** Planet atmospheres were attempted via shells, stock SkyAtmosphere, a custom `FGlobalShader` screen pass (PIE PSO fatal), then an unbound `MD_PostProcess` Custom HLSL Hillaire material (full-screen “PFX,” including wrong ScreenPosition pin = PixelPosition). All visual atmosphere code was stripped on 2026-10-14. Generation descriptors in `Pug/AtmosphereGen.cpp` remain unused by render.
