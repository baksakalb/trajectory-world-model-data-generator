# Codex MCP Instructions (he_grenade_game)

Last updated: 2026-05-25

## Purpose
- Primary Codex quick-reference for this project.
- Codex is the only maintained agent workflow for this project. Do not add or rely on other-agent-specific instructions.
- Single deep reference for tool payloads and responses: `MCP_TOOL_CALL_MATRIX.json`.
- Historical deep-audit narrative is intentionally removed to avoid duplicate maintenance.

## Current MCP Stack
- Implementation: `ChiR24/Unreal_mcp`.
- Local Codex server checkout: `C:\Users\baris\.codex\tools\Unreal_mcp`.
- MCP server package: `unreal-engine-mcp-server@0.5.21`.
- Project bridge plugin: `McpAutomationBridge 0.1.4`.
- Engine: Unreal Engine `5.7.2`.
- Bridge port: Unreal listens on `0.0.0.0:8091`; Codex connects to `127.0.0.1:8091`.
- Last verified live on 2026-05-25: rebuilt bridge DLL, `36/36` tools enabled, live calls passed for `manage_lighting:list_light_types`, `control_actor:list`, and `manage_asset:list /Game`.

## Mandatory Read-Time Health Check
- Run this sequence immediately after reading this file and before any other Unreal MCP work.
1. Confirm Unreal is open on `he_grenade_game - Unreal Editor` and port `8091` is listening.
2. Run `node check.mjs --json` from the project root and confirm all checks pass.
3. Warm up Codex MCP with a read-only call such as `manage_lighting` with `action=list_light_types` or `control_actor` with `action=list`.
4. If `manage_tools` / `manage_pipeline` are exposed in the Codex tool surface, confirm `manage_tools:get_status` reports `36/36` tools enabled and set categories to all with `manage_pipeline:set_categories`.
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

## MCP Bridge Update Workflow
- Use this focused workflow when updating only `Plugins/McpAutomationBridge` from `C:\Users\baris\.codex\tools\Unreal_mcp\plugins\McpAutomationBridge`.
1. Close Unreal Editor and verify no `UnrealEditor`, `UnrealBuildTool`, `dotnet`, `cl`, or `link` build processes are running.
2. Sync bridge `Source`, `Config`, and top-level plugin metadata from the local `Unreal_mcp` checkout into `Plugins\McpAutomationBridge`.
3. Remove only `Plugins\McpAutomationBridge\Binaries` and `Plugins\McpAutomationBridge\Intermediate`, after verifying the resolved path is inside this project's `Plugins\McpAutomationBridge` folder.
4. Build only the bridge module:
   `C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat he_grenade_gameEditor Win64 Development "C:\Users\baris\Documents\Unreal Projects\he_grenade_game\he_grenade_game.uproject" -Module=McpAutomationBridge -NoHotReload -NoLiveCoding -WaitMutex`
5. Wait for `Link [x64] UnrealEditor-McpAutomationBridge.dll` and `Result: Succeeded`.
6. Reopen the project with the exact quoted `.uproject` command from `## Startup Sequence (Reliable)`.
7. Verify the rebuilt DLL has a fresh timestamp, port `8091` is listening, `node check.mjs --json` passes, and live read-only MCP calls succeed.
- This bridge-only build can still compile 70+ actions and take around 10 minutes. That is expected if the output shows `Compile [x64] McpAutomationBridge...`.

## Health and Dead-Server Behavior
- `ue://health` may show disconnected immediately after startup; this can be normal before first bridge call.
- If tool calls fail with transport errors:
1. Confirm Unreal Editor is still open on this project.
2. Refresh Codex session.
3. Run warm-up call (`stat none`) again.
4. Re-check `ue://health` and `ue://automation-bridge`.
5. Re-run `manage_level:list_levels`.
- If still down, clear stale `node/npx` Unreal MCP processes, then refresh Codex again.
- If the in-chat `mcp__unreal_engine__` namespace says `Transport closed` after killing/rebuilding the MCP server, the server and Unreal bridge may still be healthy. Restart or reload the Codex session so the MCP client reconnects to the rebuilt server.

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
- Latest server also exposes dynamic management tools `manage_tools` and `manage_pipeline`. Some Codex sessions may need an MCP reconnect before those appear in the generated function surface.

## Quick Selector Hints
- Mostly action-only baseline calls: `build_environment`, `control_actor`, `inspect`, `manage_game_framework`, `manage_level`, `manage_level_structure`, `manage_lighting`, `manage_navigation`, `manage_performance`, `manage_sequence`, `manage_sessions`, `manage_volumes`, `system_control`.
- Usually selector-based calls: `animation_physics(assetPath)`, `manage_audio(assetPath)`, `manage_blueprint(blueprintPath)`, `manage_character(blueprintPath)`, `manage_combat(blueprintPath)`, `manage_gas(assetPath)`, `manage_material_authoring(assetPath|materialPath)`, `manage_networking(blueprintPath|actorName)`, `manage_skeleton(skeletonPath)`, `manage_texture(assetPath|texturePath)`, `manage_widget_authoring(widgetPath for read/update)`.
- Multi-selector families: `manage_ai(controllerPath|behaviorTreePath|blackboardPath|queryPath|stateTreePath|blueprintPath)`, `manage_interaction(actorName|blueprintPath|triggerPath|doorPath|switchPath)`, `manage_inventory(blueprintPath|itemPath|lootTablePath|recipePath|pickupPath)`.
- Create/author actions often need `name` plus `path` or `savePath` (for example `manage_behavior_tree`, `manage_input`).

## Current Known Limits (This Stack)
- Do not rely on old February 2026 misroute notes; after the 2026-05-25 update, core live probes succeeded through the consolidated tools path.
- `manage_level:get_summary` may still be stateful; prefer `manage_level:list_levels` for health checks.
- If a specific action fails, verify the exact `tool + action` payload against `MCP_TOOL_CALL_MATRIX.json`, then retry with a minimal read-only probe before assuming the capability is missing.

## Failure Triage Order
1. Run the Mandatory Read-Time Health Check sequence.
2. Validate payload shape (`action` + required selector).
3. Check `MCP_TOOL_CALL_MATRIX.json` row for the exact `tool + action` (`required_params`, `payload_example`, `response_signature`).
4. Check `## Current Known Limits (This Stack)` for known misroutes/stateful behavior.
5. Retry with a minimal known-good probe.
6. If capability is still unavailable, follow `ESCAPE_HATCH` (non-MCP Python fallback).

## Minimal Safe Probe Set
- `manage_lighting` -> `list_light_types`
- `control_actor` -> `list`
- `manage_asset` -> `list` with `path=/Game`, `limit=5`
- `manage_tools` -> `get_status` when exposed in the Codex tool surface

## Escape Hatch Reference
- If required operations remain blocked after matrix-guided MCP retries, use the non-MCP fallback in `ESCAPE_HATCH`.
- Use it only for capability gaps, not as a default path.
- After any escape-hatch run, execute the mandatory health check sequence again before scene-editing calls.

## End-of-File Enforcement
- Before any scene-editing call, run this exact sequence in order:
1. `node check.mjs --json`
2. `manage_lighting` -> `list_light_types`
3. `control_actor` -> `list`
4. `manage_asset` -> `list` with `path=/Game`, `limit=5`
