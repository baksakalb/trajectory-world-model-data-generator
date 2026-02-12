# Grenade Game — Session Handoff (merged, gameplay-only) — 2026-02-12

For MCP / tooling / build cycle / environment setup, see `CLAUDE_INSTRUCTIONS.md`.
This file is ONLY: gameplay state, gameplay decisions, code locations, tuning, known issues, TODOs.

## 0) Repository
- GitHub repository target: `https://github.com/baksakalb/he_game`

## 1) Project Overview
First-person grenade game. Player throws grenades on a glass-tile arena floor. Breaking tiles creates holes; falling through results in death.

## 2) Current State (What’s Working)

### 2.1 Throw Logic (release-to-throw)
- Throw model is **release-to-throw**:
  - LMB press: arm/hold intent only
  - LMB release: throw immediately if ready
  - cooldown then return ready
- Old pin-pull / primed / in-hand fuse countdown state machine removed from thrower control flow.

**Files**
- `Source/he_grenade_game/Grenade/GrenadeThrowerComponent.h/.cpp`

### 2.2 Trajectory System
- Trajectory rendering is crisp; no multi-trajectory ghosting (frame lines are non-persistent; draw duration = `0.0`)
- Based on shared deterministic sim (`FGrenadeSim`)
- Smooth line sub-segmentation exists

**Tuning (current as of 2026-02-12)**
- `ThrowInheritVelocityFactor = 0.0` (grenade goes where cursor points; independent of player movement)
- `ThrowSpawnOffset = (30, 10, -10)` (right-hand throw)

**Files**
- `Source/he_grenade_game/Grenade/GrenadeTrajectoryComponent.h/.cpp`
- `Source/he_grenade_game/Grenade/GrenadeSim.h/.cpp`

### 2.3 Crouch System
**Resolved issue (from 2026-02-11 → 2026-02-12):**
- The camera is attached to `FirstPersonMesh` socket `"head"` with relative rotation `(0, 90, -90)`.
- Applying offsets directly in the camera’s socket/local space could appear lateral/sideways.
- **Fix applied (current):** crouch camera effect is achieved by offsetting `FirstPersonMesh` Z (mesh space), not the camera relative location.

**Tuning (current as of 2026-02-12)**
- `CrouchCameraDropCm = 18.0f`
- `CrouchCameraInterpSpeed = 14.0f`
- Crouch works on ground and in air (all movement modes except `MOVE_None`)

**Last-known additional value (from 2026-02-11; verify if still true)**
- `SetCrouchedHalfHeight(88.0f)` (a “small crouch”)

**Files**
- `Source/he_grenade_game/he_grenade_gameCharacter.h/.cpp`
- `Source/he_grenade_game/Grenade/GGMovementComponent.h/.cpp`

### 2.4 Arena / Breakables
**Arena layout**
- Glass tile floor: **10x10 grid of 250cm tiles** (25m x 25m arena)
- Old template level geometry deleted (only sky, lighting, fog, PlayerStart remain)
- 4 invisible boundary walls around perimeter (collision only, hidden)
- 6 obstacles spawned via `AArenaObstacle` (cover blocks, pillar, ramp, step platform)

**Tiles**
- Tiles use `M_GlassTile` material at `/Game/Materials/M_GlassTile`
  - translucent cyan-blue
  - metallic=0.9
  - roughness=0.05
  - opacity=0.35
- `BreakableTile::BreakTile()` hides actor + disables collision → player falls through

**Death rule**
- `KillZ = -500` in GameMode → falling = death

**Grid coordinate math (useful for edits)**
- Grid spawned at `(0, 0, 6)` by GameMode
- `GridLocalOriginOffset = (-1125, -1125, 0)` centers 10x10 grid
- Tile centers: -1125 to +1125 in XY (step 250)
- Tile edges: -1250 to +1250
- Tile Z-scale 0.15 → ~15cm thick; top surface at Z=13.5
- Obstacles bottom placed at Z=13.5

**Observed behavior (from logs, 2026-02-11)**
- Auto-spawn observed:
  - `Loghe_grenade_game: Auto-spawned breakable grid actor: BreakableTileGrid_0`

**Files**
- `Source/he_grenade_game/Grenade/Breakables/BreakableTile.h/.cpp`
- `Source/he_grenade_game/Grenade/Breakables/BreakableTileGrid.h/.cpp`
- `Source/he_grenade_game/Grenade/ArenaObstacle.h/.cpp`
- `Source/he_grenade_game/he_grenade_gameGameMode.h/.cpp`

### 2.5 HUD
- Crosshair hides in aim mode; color maps to throw availability state

**Files**
- `Source/he_grenade_game/Grenade/GrenadeHUD.h/.cpp`

### 2.6 Movement
- Quake-like movement:
  - GroundAcceleration=18000
  - AirAcceleration=6000
  - WalkSpeed=600
  - SprintSpeed=900
  - CrouchSpeedScalar=0.8

**Files**
- `Source/he_grenade_game/Grenade/GGMovementComponent.h/.cpp`

## 3) Known Issues / Notes
- (Previously, 2026-02-11) crouch camera felt lateral/sideways and inconsistent.
- (Current, 2026-02-12) described as fixed via mesh-offset approach; if the feel still seems off, re-check attachment hierarchy and which component receives the Z offset.

## 4) TODO (Priority Order)

### 4.1 Tile Improvements (COMPLETED 2026-02-12)
- Done: increased tile size from 200cm to 250cm.
- Done: increased tile thickness scale from 0.10 to 0.15.
- Done: updated grid centering/walls/obstacle Z assumptions to match new tile dimensions.

### 4.2 Crouch ↔ Trajectory Behavior (design decision needed)
There is a **conflicting intent across sessions**:
- 2026-02-11 intent: keep crouch effect subtle so trajectory changes only naturally by a small origin shift.
- 2026-02-12 TODO: crouching should **noticeably** lower trajectory / more downward arc; currently crouch mostly moves camera, not throw origin/angle enough.

Action: choose desired design, then implement by adjusting throw origin/angle/launch transform when crouched.

### 4.3 Trajectory Clipping Below Map
- Don’t render trajectory segments below the lowest part of the map
- Stop drawing at floor level, not into the void

### 4.4 Breakable Object Interaction — Visual Feedback
- When aim trajectory intersects a breakable object, tint it transparent red (same translucency as blue glass, but red)
- Only while trajectory is actively shown; revert to blue when not intersected

### 4.5 Grenade ↔ Breakable Physics Interaction
- Breaking should apply resistance/force to grenade (currently passes through too easily)
- Grenade should decelerate/deflect slightly when breaking (glass-like interaction)
- Important for future non-floor breakables (walls/cover)

### 4.6 Trajectory Compute System Revamp
- Larger revamp mentioned by user; details TBD

## 5) Key File Index (quick jump list)

### Core gameplay
- `Source/he_grenade_game/he_grenade_gameCharacter.h/.cpp`
- `Source/he_grenade_game/Grenade/GGMovementComponent.h/.cpp`
- `Source/he_grenade_game/Grenade/GrenadeThrowerComponent.h/.cpp`
- `Source/he_grenade_game/Grenade/GrenadeTrajectoryComponent.h/.cpp`
- `Source/he_grenade_game/Grenade/GrenadeSim.h/.cpp`

### Arena / breakables
- `Source/he_grenade_game/Grenade/Breakables/BreakableTile.h/.cpp`
- `Source/he_grenade_game/Grenade/Breakables/BreakableTileGrid.h/.cpp`
- `Source/he_grenade_game/Grenade/ArenaObstacle.h/.cpp`
- `Source/he_grenade_game/he_grenade_gameGameMode.h/.cpp`

### HUD
- `Source/he_grenade_game/Grenade/GrenadeHUD.h/.cpp`

### Materials
- `/Game/Materials/M_GlassTile`

### Logs
- `Saved/Logs/he_grenade_game.log`
