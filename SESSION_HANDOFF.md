# Grenade Game - Session Handoff (gameplay-only) - 2026-02-12

For MCP/tooling/build-cycle setup, see `CODEX_INSTRUCTIONS.md`.
This handoff is gameplay state only: behavior, tuning, current decisions, and TODO.

## 0) Repository
- GitHub: `https://github.com/baksakalb/he_game`

## 1) Project Overview
First-person grenade game on a glass-tile arena floor. Grenades break tiles and create holes; falling through restarts the player.

## 2) Current State (stable)

### 2.1 Throw and trajectory lock
- Throw model is release-to-throw:
  - LMB press: hold intent
  - LMB release: throw if ready
- Current throw lock behavior (preferred by play feel):
  - While holding LMB, launch params are continuously snapshotted.
  - On release, throw uses held snapshot first (current fallback exists if needed).
- Trajectory and throw are deterministic via shared `FGrenadeSim`.
- Debug instrumentation exists (`gg.Grenade.DebugThrowLock`) and recent samples showed zero spawn/velocity drift in logged release/initialize pairs.

Files:
- `Source/he_grenade_game/Grenade/GrenadeThrowerComponent.h`
- `Source/he_grenade_game/Grenade/GrenadeThrowerComponent.cpp`
- `Source/he_grenade_game/Grenade/GrenadeTrajectoryComponent.h`
- `Source/he_grenade_game/Grenade/GrenadeTrajectoryComponent.cpp`

### 2.2 Breakable interaction and post-break motion
- Floor intersection throws can break two adjacent tiles (intended/acceptable).
- Updated breakable-hit response to remove unnatural upward rebound/hover feeling after breaking upward-facing floor tiles:
  - Deflection uses post-damped velocity.
  - Upward-facing tile breaks clamp outward normal rebound so grenade does not pop upward.

Files:
- `Source/he_grenade_game/Grenade/GrenadeSim.h`
- `Source/he_grenade_game/Grenade/GrenadeSim.cpp`
- `Source/he_grenade_game/Grenade/GrenadeActor.cpp`

### 2.3 Arena and breakables
- Arena: 10x10 glass tiles, tile size 250 cm, centered grid.
- Break behavior: tile hides and collision disables, creating real hole.
- KillZ death/restart flow is active.

Files:
- `Source/he_grenade_game/Grenade/Breakables/BreakableTile.h`
- `Source/he_grenade_game/Grenade/Breakables/BreakableTile.cpp`
- `Source/he_grenade_game/Grenade/Breakables/BreakableTileGrid.h`
- `Source/he_grenade_game/Grenade/Breakables/BreakableTileGrid.cpp`
- `Source/he_grenade_game/he_grenade_gameGameMode.h`
- `Source/he_grenade_game/he_grenade_gameGameMode.cpp`

### 2.4 Movement and crouch
- Quake-like movement setup is active.
- Crouch camera offset is mesh-driven (not direct camera local offset), which resolved prior sideways/space issues.

Files:
- `Source/he_grenade_game/Grenade/GGMovementComponent.h`
- `Source/he_grenade_game/Grenade/GGMovementComponent.cpp`
- `Source/he_grenade_game/he_grenade_gameCharacter.h`
- `Source/he_grenade_game/he_grenade_gameCharacter.cpp`

## 3) Known Notes
- Overall behavior is currently in a good state.
- Remaining improvements are now small polish-level behavior tuning.

## 4) TODO (remaining)

### 4.1 Trajectory behavior (to be determined)
- Finalize desired trajectory behavior polish/design details (including any remaining edge-case feel issues).

### 4.2 Movement behavior polish
- Tune/adjust movement behavior details per latest playtest feel.

## 5) Useful Debug
- Throw lock debug CVar:
  - `gg.Grenade.DebugThrowLock 1`
  - `gg.Grenade.DebugThrowLock 0`
- Logs:
  - `Saved/Logs/he_grenade_game.log`

