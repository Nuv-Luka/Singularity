# PlanetTerrainNext R11.1 distance-driven LOD and watertight repair

Base: `183964e83f6c006ed526b272685bd656c9723d4e`
Branch: `feature/planet-terrain-next-r11-1-distance-lod-repair`

## Implemented stages

- Quadtree balance samples eight positions along every edge, resolves cube-face neighbors through direction projection, and rejects both unsafe splits and unsafe merges. The split check correctly validates the post-split L+1 level against neighbors.
- Resident patch builds can describe all four edge adjacencies. Same-level borders are canonicalized, fine-to-coarse borders use the actual coarser rendered edge with a one-row transition band, and affected normals, tangents, UV displacement and skirts are refreshed.
- Skirt tops now originate from final rendered edge positions. The existing skirt cvar remains the exceptional fallback while neighbor coverage is unavailable.
- LOD fades use signed distance to the patch bounding region. Stationary cameras do not advance fades, camera reversal re-anchors without losing progress, and large camera cuts are handled explicitly. Time-based advancement remains only for diagnostic calls without a view.
- Shadow ownership is reconciled once from the final visible/fade state; visibility synchronization no longer applies a contradictory intermediate cast-shadow state.

## Validation

- R11 automation includes complementary dither, mixed-LOD edge band, queue ramp, merge residency, spatial fade, and neighbor stitch/skirt alignment coverage.
- R10 regression tests remain required before accepting the branch.
- Stationary, descent, and close-surface performance captures remain acceptance work; no performance recovery claim is made from commandlet tests alone.
