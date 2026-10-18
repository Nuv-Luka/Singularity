# R5 status — priority planner and async bake scheduler

**Date:** 2026-10-18
**Engine:** Unreal 5.8
**State:** compiled and automation-tested. **Not visually signed off**.

## What R5 adds

| Piece | Notes |
|---|---|
| Tile planner | `FPlanetTerrainNextTilePlanner` — Coverage / VisibleRefine / Frustum / Prefetch classes; sorted desired set; bounded sibling prefetch. |
| Tile scheduler | `FPlanetTerrainNextTileScheduler` — persistent queue, dedupe by tile id, cancel optional on camera-turn, max workers + max queue, generation stamp rejects stale completes. |
| Async bake | Thread-pool workers bake pure `HeightAt` tiles only (no UObjects). Game thread `Drain` → `TileCache::Insert` → mesh `BuildFromTile`. |
| Sync fallback | `BuildNode` still `GetOrBake` if the cache misses so coverage never starves on a full queue. |
| CVars | `MaxBakeWorkers` (default 4), `MaxBakeQueue` (256), `PrefetchPerFrame` (8). |

GPU atlas upload remains deferred (R4 D3D12 issue); residency pool is still logical.

## Acceptance gates (automation)

| Test | Result |
|---|---|
| `R5.PlannerPriority` | Class ordering, sort, dedupe, prefetch bound |
| `R5.SchedulerDedupeCancel` | Dedupe raise; camera-turn cancels prefetch; worker cap; async → cache |
| `R5.SchedulerStaleGeneration` | Generation reset rejects attach of old bake |
| `R5.SchedulerWorkerStress` | 2/4/6 workers never exceed budget; drains |
| All R1–R4 tests | Still Success |

## Not in R5

- Real D3D12 height atlas / RDG vertex sampling
- Collision queue (R6)
- Guided descent corridor / speed governor (R7–R8)
- Disk cache tier

## How to check in PIE

1. F6 spawn → fly; on-screen line shows `sched qN bakeN +started OKdone cxcancelled`.
2. Rapid 180° turns: cancelled count rises; drawn coverage stays continuous (no empty sections).
3. Stress: `MaxBakeWorkers 2` then `6` — streaming slower/faster, parents keep covering.
4. `PauseBuilds 1` — queue may fill; unpause rebuilds from cache hits when possible.

## Acceptance before R6

- [x] Compiles
- [x] Automation under `Singularity.PlanetTerrainNext`
- [ ] PIE: camera-turn priority flip without holes
- [ ] Confirm worker CVars move p95 bake / queue wait as expected
