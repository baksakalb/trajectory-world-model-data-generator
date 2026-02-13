# Codex MCP Instructions (he_grenade_game)

Last updated: 2026-02-12

## Purpose
- Primary Codex quick-reference for this project.
- Single deep reference for tool payloads and responses: `MCP_TOOL_CALL_MATRIX.json`.
- Historical deep-audit narrative is intentionally removed to avoid duplicate maintenance.

## Mandatory Read-Time Health Check
- Run this exact sequence immediately after reading this file and before any other Unreal MCP work.
1. `control_editor` with `action=console_command`, `command=stat none`
2. read `ue://health` and confirm connected
3. read `ue://automation-bridge` and confirm connected
4. `manage_level` with `action=list_levels`
- If any step fails, follow `## Health and Dead-Server Behavior`, then repeat from step 1.

## Canonical Codex Setup
- Use one MCP config source only: `C:\Users\baris\.codex\config.toml`.
- Do not keep a second project-local `.codex/config.toml` MCP block.

Expected server entry:

```toml
[mcp_servers.unreal_engine]
command = "node"
args = ["C:/Users/baris/.codex/tools/Unreal_mcp/dist/cli.js"]

[mcp_servers.unreal_engine.env]
UE_PROJECT_PATH = "C:/Users/baris/Documents/Unreal Projects/he_grenade_game/he_grenade_game.uproject"
MCP_AUTOMATION_HOST = "127.0.0.1"
MCP_AUTOMATION_PORT = "8091"
MCP_AUTOMATION_WS_HOST = "127.0.0.1"
MCP_AUTOMATION_WS_PORT = "8091"
MCP_AUTOMATION_CLIENT_PORT = "8091"
MCP_AUTOMATION_ALLOW_NON_LOOPBACK = "true"
LOG_LEVEL = "info"
WASM_ENABLED = "false"
MCP_AUTOMATION_REQUEST_TIMEOUT_MS = "120000"
ASSET_LIST_TTL_MS = "10000"
```

## Unreal Plugin Settings (Project)
In `Config/DefaultGame.ini`:

```ini
[/Script/McpAutomationBridge.McpAutomationBridgeSettings]
bAlwaysListen=True
ListenHost=0.0.0.0
ListenPorts=8091
bMultiListen=False
bAllowNonLoopback=True
bRequireCapabilityToken=False
```

## Startup Sequence (Reliable)
1. Open Unreal Editor on this exact `.uproject` (never launch bare editor without project arg):
   `C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe "C:\Users\baris\Documents\Unreal Projects\he_grenade_game\he_grenade_game.uproject"`
2. Confirm the window title/project is `he_grenade_game` and wait until the level is loaded.
3. Start or refresh Codex.
4. Run the Mandatory Read-Time Health Check sequence.

## Execution Responsibility
- Codex should run compile, reload, and editor reset/restart actions itself via MCP/tool commands whenever possible.
- Do not ask the user to perform editor-side restart/rebuild/reload steps unless automation is blocked or impossible.

## Compile + Restart Workflow (C++ Changes)
- Preferred full rebuild flow when code changes must be visible in-editor:
1. Close editor (prefer MCP `control_editor` with `action=console_command`, `command=QUIT_EDITOR`).
2. Wait until `UnrealEditor.exe` process exits.
3. Build from shell:
   `C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat he_grenade_gameEditor Win64 Development "C:\Users\baris\Documents\Unreal Projects\he_grenade_game\he_grenade_game.uproject" -waitmutex`
4. Reopen editor from shell on this exact project path (quoted):
   `C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe "C:\Users\baris\Documents\Unreal Projects\he_grenade_game\he_grenade_game.uproject"`
5. If the Project Browser opens instead of the project, close it and relaunch using the exact quoted command above.
6. Wait for bridge readiness (port `8091`) and run the Mandatory Read-Time Health Check sequence.
- If a direct build fails due live coding lock, use the full close/build/reopen flow above.

## Health and Dead-Server Behavior
- `ue://health` may show disconnected immediately after startup; this can be normal before first bridge call.
- If tool calls fail with transport errors:
1. Confirm Unreal Editor is still open on this project.
2. Refresh Codex session.
3. Run warm-up call (`stat none`) again.
4. Re-check `ue://health` and `ue://automation-bridge`.
5. Re-run `manage_level:list_levels`.
- If still down, clear stale `node/npx` Unreal MCP processes, then refresh Codex again.

## Basic Tool Calling Pattern
- All calls require `action`.
- Many actions also require one selector field such as `assetPath`, `blueprintPath`, `widgetPath`, `actorName`, `skeletonPath`, `texturePath`, or `materialPath`.
- Start with the smallest valid payload and add optional fields only when needed.

```json
{ "action": "list_levels" }
```

```json
{ "action": "get_audio_info", "assetPath": "/Game/Audio/MyCue.MyCue" }
```

```json
{ "action": "console_command", "command": "stat none" }
```

## Tool Surface Snapshot (High Level)
- Use this list first when choosing the tool family:
`animation_physics, build_environment, control_actor, control_editor, inspect, manage_ai, manage_asset, manage_audio, manage_behavior_tree, manage_blueprint, manage_character, manage_combat, manage_effect, manage_game_framework, manage_gas, manage_geometry, manage_input, manage_interaction, manage_inventory, manage_level, manage_level_structure, manage_lighting, manage_material_authoring, manage_navigation, manage_networking, manage_performance, manage_sequence, manage_sessions, manage_skeleton, manage_splines, manage_texture, manage_volumes, manage_widget_authoring, system_control`
- Not currently exposed to Codex function surface: `manage_pipeline`.

## Quick Selector Hints
- Mostly action-only baseline calls: `build_environment`, `control_actor`, `inspect`, `manage_game_framework`, `manage_level`, `manage_level_structure`, `manage_lighting`, `manage_navigation`, `manage_performance`, `manage_sequence`, `manage_sessions`, `manage_volumes`, `system_control`.
- Usually selector-based calls: `animation_physics(assetPath)`, `manage_audio(assetPath)`, `manage_blueprint(blueprintPath)`, `manage_character(blueprintPath)`, `manage_combat(blueprintPath)`, `manage_gas(assetPath)`, `manage_material_authoring(assetPath|materialPath)`, `manage_networking(blueprintPath|actorName)`, `manage_skeleton(skeletonPath)`, `manage_texture(assetPath|texturePath)`, `manage_widget_authoring(widgetPath for read/update)`.
- Multi-selector families: `manage_ai(controllerPath|behaviorTreePath|blackboardPath|queryPath|stateTreePath|blueprintPath)`, `manage_interaction(actorName|blueprintPath|triggerPath|doorPath|switchPath)`, `manage_inventory(blueprintPath|itemPath|lootTablePath|recipePath|pickupPath)`.
- Create/author actions often need `name` plus `path` or `savePath` (for example `manage_behavior_tree`, `manage_input`).

## Current Known Limits (This Stack)
- `manage_asset` query-style flows (`search_assets`, `find_by_tag`, `get_source_control_state`) currently misroute to sessions and fail with `Unknown manage_sessions action: asset_query`.
- `manage_splines` actions currently misroute and fail with `Unknown manage_sessions action: <subAction>`.
- `manage_level:get_summary` is stateful; it works after `manage_level:load` tracks that level.

## Failure Triage Order
1. Run the Mandatory Read-Time Health Check sequence.
2. Validate payload shape (`action` + required selector).
3. Check `MCP_TOOL_CALL_MATRIX.json` row for the exact `tool + action` (`required_params`, `payload_example`, `response_signature`).
4. Check `## Current Known Limits (This Stack)` for known misroutes/stateful behavior.
5. Retry with a minimal known-good probe.
6. If capability is still unavailable, follow `ESCAPE_HATCH` (non-MCP Python fallback).

## Minimal Safe Probe Set
- `control_editor` -> `console_command` (`stat none`)
- `manage_level` -> `list_levels`
- `control_actor` -> `list`
- `system_control` -> `get_project_settings`

## Escape Hatch Reference
- If required operations remain blocked after matrix-guided MCP retries, use the non-MCP fallback in `ESCAPE_HATCH`.
- Use it only for capability gaps, not as a default path.
- After any escape-hatch run, execute the mandatory health check sequence again before scene-editing calls.

## End-of-File Enforcement
- Before any scene-editing call, run this exact sequence in order:
1. `control_editor` -> `console_command` (`stat none`)
2. read `ue://health`
3. read `ue://automation-bridge`
4. `manage_level` -> `list_levels`
