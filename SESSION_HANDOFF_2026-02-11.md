# Grenade Game Session Handoff (2026-02-11)

This file is the current high-signal memory for continuing development in the next session.

## 0) Repository

- GitHub repository target: `https://github.com/baksakalb/he_game`

## 1) Environment + Runtime

- Project root: `C:\Users\baris\Documents\Unreal Projects\he_grenade_game`
- Engine: UE `5.7.2-49658320+++UE5+Release-5.7`
- Target map: `/Game/FirstPerson/Lvl_FirstPerson`
- Startup/Game default map: `Lvl_FirstPerson` (from `Config/DefaultEngine.ini`)
- Default game mode configured in project settings: `BP_FirstPersonGameMode_C` (inherits from project C++ base behavior used here)
- Node/NPM used for MCP server: `npx.cmd` + `unreal-engine-mcp-server@0.5.15`

## 2) MCP Configuration (Confirmed)

Primary config files:
- Project config: `.codex/config.toml`
- User config: `C:\Users\baris\.codex\config.toml`

Server command:
- `C:\Program Files\nodejs\npx.cmd -y unreal-engine-mcp-server@0.5.15`

Important env values used:
- `UE_PROJECT_PATH=C:/Users/baris/Documents/Unreal Projects/he_grenade_game/he_grenade_game.uproject`
- `MCP_AUTOMATION_HOST=127.0.0.1`
- `MCP_AUTOMATION_PORT=8091`
- `MCP_AUTOMATION_WS_HOST=127.0.0.1`
- `MCP_AUTOMATION_WS_PORT=8091`
- `MCP_AUTOMATION_CLIENT_PORT=8091`
- `LOG_LEVEL=info`
- `MCP_ROUTE_STDOUT_LOGS=true`

## 3) Quick MCP Health/Connectivity Checks

Preferred checks:
1. `read_mcp_resource(server="unreal_engine", uri="ue://health")`
2. `read_mcp_resource(server="unreal_engine", uri="ue://automation-bridge")`
3. One tool call, e.g. `control_editor.console_command("stat none")`

Notes:
- `ue://health` can briefly show disconnected during startup/handshake.
- `ue://automation-bridge` is a strong source of truth for bridge session state.
- During this session, connectivity was confirmed working multiple times with successful tool execution.

## 4) Proven MCP Tool Call Patterns

Working examples:
- `mcp__unreal_engine__control_editor({"action":"console_command","command":"stat none"})`
- `mcp__unreal_engine__control_editor({"action":"play"})`
- `mcp__unreal_engine__control_editor({"action":"stop"})`
- `mcp__unreal_engine__control_actor({"action":"list"})`
- `read_mcp_resource(server="unreal_engine", uri="ue://level")`
- `read_mcp_resource(server="unreal_engine", uri="ue://health")`

Important behavior:
- `control_actor.list` fails while PIE is running with "Editor is currently in a play mode". Use it in editor world (not active PIE), or use resources/logs for runtime confirmation.

## 5) Console Command Safety / Validator Limits

The MCP server validates console commands and blocks unsafe patterns:
- multiline commands
- chaining (`&&`, `||`, `;`)
- pipes (`|`) and backticks
- dangerous command/token patterns

Implication:
- use simple single commands for `console_command`.

## 6) Build/Run Commands That Worked

Primary editor build:
```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" `
  he_grenade_gameEditor Win64 Development `
  "C:\Users\baris\Documents\Unreal Projects\he_grenade_game\he_grenade_game.uproject" `
  -WaitMutex -NoHotReloadFromIDE
```

When UBA memory pressure appears or repeated process kills happen:
```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" `
  he_grenade_gameEditor Win64 Development `
  "C:\Users\baris\Documents\Unreal Projects\he_grenade_game\he_grenade_game.uproject" `
  -WaitMutex -NoHotReloadFromIDE -NoUBA
```

Known pitfall:
- Link fails with `LNK1104` if `UnrealEditor.exe` is running and locking `UnrealEditor-he_grenade_game.dll`.
- Fix:
```powershell
Get-Process UnrealEditor -ErrorAction SilentlyContinue | Stop-Process -Force
```

## 7) Gameplay Systems Implemented (Current State)

### 7.1 Throw Logic
- Throw model changed to release-throw behavior:
  - LMB press: arm/hold intent only.
  - LMB release: throw immediately if ready.
  - cooldown then return ready.
- Old pin-pull/primed/in-hand fuse countdown state machine removed from thrower control flow.
- Files:
  - `Source/he_grenade_game/Grenade/GrenadeThrowerComponent.h`
  - `Source/he_grenade_game/Grenade/GrenadeThrowerComponent.cpp`

### 7.2 Trajectory
- Trajectory parity infrastructure remains based on shared deterministic sim (`FGrenadeSim`) and throw params.
- Smooth line sub-segmentation exists.
- Multi-trajectory "ghosting" issue was fixed by setting draw duration to `0.0` (non-persistent frame lines).
- Files:
  - `Source/he_grenade_game/Grenade/GrenadeTrajectoryComponent.h`
  - `Source/he_grenade_game/Grenade/GrenadeTrajectoryComponent.cpp`
  - `Source/he_grenade_game/Grenade/GrenadeSim.cpp`

### 7.3 Breakable Grid
- Auto-spawn logic exists and was observed in logs:
  - `Loghe_grenade_game: Auto-spawned breakable grid actor: BreakableTileGrid_0`
- Files:
  - `Source/he_grenade_game/he_grenade_gameGameMode.h`
  - `Source/he_grenade_game/he_grenade_gameGameMode.cpp`
  - `Source/he_grenade_game/Grenade/Breakables/*`

### 7.4 HUD
- Crosshair hides in aim mode; color maps to throw availability state.
- File:
  - `Source/he_grenade_game/Grenade/GrenadeHUD.cpp`

## 8) Crouch Work in This Session (Most Recent)

Recent edits targeted:
1. Camera feedback for crouch (explicit camera offset with smoothing).
2. Allow crouch while in air (per latest user preference).
3. Keep crouch effect subtle so trajectory changes only naturally by small origin shift.

Latest crouch-relevant values/logic:
- `CrouchCameraDropCm = 8.0f`
- `SetCrouchedHalfHeight(88.0f)` (small crouch vs previous deeper crouch)
- Crouch allowed while in-air (`CanCrouchInCurrentState` now allows all movement modes except `MOVE_None`)
- Files:
  - `Source/he_grenade_game/he_grenade_gameCharacter.h`
  - `Source/he_grenade_game/he_grenade_gameCharacter.cpp`
  - `Source/he_grenade_game/Grenade/GGMovementComponent.cpp`

## 9) Current User-Reported Issues To Resolve Next

Priority order from user:
1. Camera crouch behavior still feels wrong:
   - observed as lateral/sideways feel instead of clean vertical up/down effect.
2. Ground crouch feel inconsistency:
   - user perception indicated mismatch between trajectory change and camera feedback.
3. Trajectory movement while crouching should be small/natural only:
   - secondary concern compared to camera behavior.

Note:
- User explicitly confirmed trajectory line quality itself is now "working super well".

## 10) Likely Root Cause / Next Fix Direction

Most likely issue is not the crouch state machine anymore, but first-person camera attachment/relative transform basis:
- Camera is attached to `FirstPersonMesh` at socket `"head"` with a nontrivial relative rotation (`0,90,-90`).
- Applying local `SetRelativeLocation` on this rotated parent/socket can present as sideways motion from player POV.

Recommended next session approach:
1. Move crouch camera offset application to world-space or controller-up-space, not raw local-space on rotated camera attachment.
2. Or attach camera to a dedicated neutral `USceneComponent` pivot (non-rotated) and apply crouch offset there.
3. Keep crouch capsule delta small (already reduced), then retune only if needed after camera axis fix.

## 11) Important Files for Next Agent

Core gameplay:
- `Source/he_grenade_game/he_grenade_gameCharacter.h`
- `Source/he_grenade_game/he_grenade_gameCharacter.cpp`
- `Source/he_grenade_game/Grenade/GGMovementComponent.h`
- `Source/he_grenade_game/Grenade/GGMovementComponent.cpp`
- `Source/he_grenade_game/Grenade/GrenadeThrowerComponent.h`
- `Source/he_grenade_game/Grenade/GrenadeThrowerComponent.cpp`
- `Source/he_grenade_game/Grenade/GrenadeTrajectoryComponent.h`
- `Source/he_grenade_game/Grenade/GrenadeTrajectoryComponent.cpp`
- `Source/he_grenade_game/Grenade/GrenadeSim.h`
- `Source/he_grenade_game/Grenade/GrenadeSim.cpp`

Breakables:
- `Source/he_grenade_game/Grenade/Breakables/BreakableTile.h`
- `Source/he_grenade_game/Grenade/Breakables/BreakableTile.cpp`
- `Source/he_grenade_game/Grenade/Breakables/BreakableTileGrid.h`
- `Source/he_grenade_game/Grenade/Breakables/BreakableTileGrid.cpp`

Game mode / HUD:
- `Source/he_grenade_game/he_grenade_gameGameMode.h`
- `Source/he_grenade_game/he_grenade_gameGameMode.cpp`
- `Source/he_grenade_game/Grenade/GrenadeHUD.h`
- `Source/he_grenade_game/Grenade/GrenadeHUD.cpp`

Config:
- `.codex/config.toml`
- `C:\Users\baris\.codex\config.toml`
- `Config/DefaultEngine.ini`

Logs:
- `Saved/Logs/he_grenade_game.log`

## 12) Session Outcome

- Major systems are in place and compiling.
- MCP bridge and core tooling are operational.
- Trajectory rendering quality improved and user confirmed it is working well.
- Remaining high-priority gameplay polish is crouch camera behavior axis/feel.
