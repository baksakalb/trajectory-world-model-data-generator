# Codex MCP Instructions (he_grenade_game)

Last updated: 2026-02-12

## Purpose
- This is the Codex-specific MCP quick guide for this project.
- For exact per-tool payloads and tested outcomes, use:
- `MCP_TOOL_CALL_AUDIT.md` (human-readable summary)
- `MCP_TOOL_CALL_MATRIX.json` (machine-readable matrix)

## Canonical Codex Setup
- Use one MCP config source only: `C:\Users\baris\.codex\config.toml`
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
1. Open Unreal Editor with this project and wait until the level is loaded.
2. Start or refresh Codex.
3. Run one safe warm-up call:
- `control_editor` with `action=console_command`, `command=stat none`
4. Verify MCP status (`ue://health`, `ue://automation-bridge`) if needed.

## Health and Dead-Server Behavior
- `ue://health` may show disconnected immediately after startup; this can be normal before first bridge call.
- If tool calls fail with transport errors:
1. Confirm Unreal Editor is still open on this project.
2. Refresh Codex session.
3. Run warm-up call (`stat none`) again.
4. Re-check `ue://health`.
- If still down, clear stale `node/npx` Unreal MCP processes, then refresh Codex again.

## How Codex Calls Tools
- Codex uses typed tool functions (for example `manage_level`, `manage_asset`, `control_editor`) with arguments.
- Most tool families require:
- `action` (always)
- one selector field depending on action (`assetPath`, `blueprintPath`, `widgetPath`, `actorName`, `skeletonPath`, etc.)
- If a call fails, compare payload with known-good examples in `MCP_TOOL_CALL_MATRIX.json`.

## Current Known Limits (This Stack)
- `manage_asset` query-style flows (`search_assets`, `find_by_tag`, `get_source_control_state`) currently misroute to sessions and fail with `Unknown manage_sessions action: asset_query`.
- `manage_splines` actions currently misroute and fail with `Unknown manage_sessions action: <subAction>`.
- `manage_pipeline` exists in source but is hidden from this Codex client surface based on client capability filtering.
- `manage_level:get_summary` is stateful; it works after `manage_level:load` tracks that level.

## Failure Triage Order
1. Check payload shape and required selector (`MCP_TOOL_CALL_MATRIX.json`).
2. Check if action is currently known-broken/misrouted (`MCP_TOOL_CALL_AUDIT.md` Known Defects).
3. Check connection state and recover transport.
4. Retry with a minimal, known-good probe call.

## Minimal Safe Probe Set
- `control_editor` -> `console_command` (`stat none`)
- `manage_level` -> `list_levels`
- `control_actor` -> `list`
- `system_control` -> `get_project_settings`

## Scope Note
- This file is intentionally short.
- Deep call coverage, response signatures, and evidence stay in:
- `MCP_TOOL_CALL_AUDIT.md`
- `MCP_TOOL_CALL_MATRIX.json`
