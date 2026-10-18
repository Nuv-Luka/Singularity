# Third-party: webgpu-sky-atmosphere

| Field | Value |
|---|---|
| Upstream | https://github.com/JolifantoBambla/webgpu-sky-atmosphere |
| Pinned commit | `2b4500bf1c8bb4f30901dc1b4eb30fe396056e2c` (cloned 2026-10-14) |
| License | MIT — Copyright (c) 2024 Lukas Herzberger |
| Notice file | `LICENSES/webgpu-sky-atmosphere-MIT.txt` |
| Paper | Sébastien Hillaire, EGSR 2020 — https://sebh.github.io/publications/egsr2020.pdf |

This plugin ports shader mathematics and LUT/pass dependency concepts from the upstream WGSL implementation. It does **not** vendor the TypeScript/WebGPU host runtime.

Unreal Engine shader/rendering APIs remain under Epic's license; this notice covers only the adapted atmosphere mathematics and any derived shader text from the MIT upstream.

## Upstream LUT defaults (from `src/resources.ts` at pinned commit)

| LUT | Default size | Format (upstream) |
|---|---|---|
| Transmittance | 256×64 | rgba16float |
| Multiple scattering | 32×32 | rgba16float |
| Sky view | 192×108 | rgba16float |
| Aerial perspective | 32×32×32 | rgba16float |

Not used until milestones M3+.
