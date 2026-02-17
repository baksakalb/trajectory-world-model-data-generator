# Grenade Game - Session Handoff (gameplay-only) - 2026-02-17

For MCP/tooling/build-cycle setup, see `CODEX_INSTRUCTIONS.md`.
This handoff is gameplay state only: behavior, tuning, current decisions, known problems, and next steps.

## 0) Repository
- GitHub: `https://github.com/baksakalb/he_game`
- Branch: `main`

## 1) Current Gameplay Situation

### 1.1 Movement input map and high-level behavior
- Sprint is removed.
- Slide is removed.
- `LeftShift` is crouch (ground crouch) and crouch-hop qualifier input.
- `LeftControl` is no longer movement-crouch; it is used for throw arc raise (see section 1.4).
- Jump can be started while crouched:
  - Jump start forces uncrouch first, then executes jump.

Files:
- `Source/he_grenade_game/he_grenade_gameCharacter.cpp`
- `Source/he_grenade_game/Grenade/GGMovementComponent.h`
- `Source/he_grenade_game/Grenade/GGMovementComponent.cpp`

### 1.2 Movement tuning currently in code
- Walk speed: `1100 cm/s` (raised to former sprint-like pace).
- Aim speed scalar: `0.7` (aim move speed = 70% of normal).
- Crouch speed scalar: `0.8`.
- Air acceleration: `12000 cm/s^2`.
- Air speed cap: `1500 cm/s`.
- Bunnyhop hard cap: `1900 cm/s` (crouch-hop cap can exceed this when configured).
- Jump tuning:
  - Jump velocity: `570 cm/s`
  - Gravity scale: `2.2`
  - Character jump hold extension disabled (`JumpMaxHoldTime = 0.0f`) to keep jumps less floaty.

Files:
- `Source/he_grenade_game/Grenade/GGMovementComponent.h`
- `Source/he_grenade_game/Grenade/GGMovementComponent.cpp`
- `Source/he_grenade_game/he_grenade_gameCharacter.cpp`

### 1.3 Crouch-hop system currently implemented
- Boost is jump-triggered, not auto-applied on landing.
- Landing qualification currently uses Shift press recency:
  - Qualify if Shift was pressed within `0.45s` before landing.
- Post-land jump consume window:
  - Jump must happen within `0.55s` after qualifying landing.
- Additional consume gate currently active:
  - Shift must be released after landing before boost can be consumed.
  - If Shift is still held, boost is not consumed.
- Boost amount:
  - Scalar and additive model with chain bonus.
  - Chain bonus: `+70 cm/s` per chained hop within `1.2s`.
  - Crouch-hop cap: `2100 cm/s`.
- Direction model:
  - Blend of movement intent and crosshair-facing direction.
  - Ground excess momentum crosshair influence: `0.75`.
  - Air excess momentum crosshair influence: `0.25`.

Files:
- `Source/he_grenade_game/Grenade/GGMovementComponent.h`
- `Source/he_grenade_game/Grenade/GGMovementComponent.cpp`

### 1.4 In-air handling currently implemented
- Shift air-drag slowdown was removed.
- Current air behavior includes:
  - Kickstart from low horizontal speed when air input exists.
  - Opposite-input braking in air (pressing against current travel reduces speed).
  - Air acceleration and cap enforcement.
- Intent was to keep air control responsive while still allowing speed loss on opposite input.

Files:
- `Source/he_grenade_game/Grenade/GGMovementComponent.h`
- `Source/he_grenade_game/Grenade/GGMovementComponent.cpp`

### 1.5 Throw arc raise on `LeftControl`
- `LeftControl` increases throw arc angle (pitch up), not spawn position.
- Only active in throw-charge context (while LMB throw is held and charging).
- Arc raise progress is tied to hold duration against fuse duration.
- Max extra pitch offset currently: `40 deg`.

Files:
- `Source/he_grenade_game/Grenade/GrenadeThrowerComponent.h`
- `Source/he_grenade_game/Grenade/GrenadeThrowerComponent.cpp`
- `Source/he_grenade_game/he_grenade_gameCharacter.cpp`

### 1.6 HUD feedback
- HUD now displays:
  - Hop activity label (`HOP` / `HOP xN`) with active color feedback.
  - Current horizontal speed value.
- Crosshair remains hidden while aiming and uses grenade-ready vs cooldown color when visible.

Files:
- `Source/he_grenade_game/Grenade/GrenadeHUD.h`
- `Source/he_grenade_game/Grenade/GrenadeHUD.cpp`

## 2) Current Problem (Primary Blocker)

User-reported live issue:
- Air movement still feels partially fixed/locked in some cases.
- Repro noted: from near-stationary jump, pressing WASD while airborne can feel like it does little or no movement response.
- Also reported: transitions between hops can feel like a brief slow/stationary dip before next boosted moment.

Interpretation of expected behavior:
- Both on ground and in air, movement should always react strongly to WASD.
- Crosshair/facing should strongly influence momentum direction, but WASD must still allow forward/strafe/back control at all times.
- Opposite-direction air input should be able to slow/revector movement quickly.

## 3) Likely Cause Areas to Investigate Next
- Air input kickstart path and acceleration path interaction when starting from low horizontal speed.
- Opposite-input air brake tuning and whether it is over-damping in edge cases.
- Momentum recombination block (normal speed + excess speed blend) interaction when velocity is low or direction flips quickly.
- Crouch-hop consume gate (require Shift release after landing) may make timing feel inconsistent if not clearly communicated.

Primary files:
- `Source/he_grenade_game/Grenade/GGMovementComponent.cpp`
- `Source/he_grenade_game/Grenade/GGMovementComponent.h`
- `Source/he_grenade_game/he_grenade_gameCharacter.cpp`

## 4) Build / Validation Status
- Earlier movement/trajectory iterations were successfully built in editor target.
- A fresh full build has not been rerun yet after the latest in-progress air-control adjustments in working tree.

## 5) Next Step Recommendation
- First pass should focus only on air responsiveness fix:
  - Ensure non-zero airborne movement response from stationary jump with any WASD input.
  - Keep opposite-input slowdown behavior but retune so it does not suppress basic control.
  - Re-test hop chaining after air-control fix to avoid regressions.
