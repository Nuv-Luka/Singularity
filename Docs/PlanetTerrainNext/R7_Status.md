# R7 status — deferred

**Date:** 2026-10-18  
**State:** **Skipped for now** (explicit).

## Spec (not implemented)

Flight integration: real-space orbit→surface traversal, optional assisted-descent corridor / speed governor, no scale/teleport/loading transition.

## Why deferred

R0–R6 already cover coverage, morph, streaming, collision, and shadows. Assisted descent must not mask streaming gaps; the user chose to postpone it until free-flight streaming + collision are signed off in longer play.

## When revisiting

1. Scripted camera replay: orbit → 100 m AGL → hover (automation, no gameplay pawn required first).
2. Planner path samples already exist for collision; extend them to a descent corridor with time-to-visibility priorities.
3. Optional speed governor from p95 bake/cook latency — never a loading screen.
4. Only then wire ship landing / disembark (`L`).

Continue at **R8** (performance and polish).
