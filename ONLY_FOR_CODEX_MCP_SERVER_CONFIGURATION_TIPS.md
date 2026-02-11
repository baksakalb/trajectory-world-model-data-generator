# Unreal MCP Server Configuration Tips (he_grenade_game)

Last updated: 2026-02-11

## Canonical Setup (Known Working)

Use exactly one Codex MCP config source.

- Keep MCP config only in `C:\Users\baris\.codex\config.toml`
- Do not keep a second `mcp_servers.unreal_engine` block in project-local `.codex/config.toml`

Current known-good global config:

```toml
model = "gpt-5.3-codex"
model_reasoning_effort = "xhigh"

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

Unreal plugin settings in `Config/DefaultGame.ini`:

```ini
[/Script/McpAutomationBridge.McpAutomationBridgeSettings]
bAlwaysListen=True
ListenHost=0.0.0.0
ListenPorts=8091
bMultiListen=False
bAllowNonLoopback=True
bRequireCapabilityToken=False
```

## What Made It Work

- Switched from `npx unreal-engine-mcp-server` to local built server (`node .../dist/cli.js`).
- Removed duplicate MCP config ownership (global + local conflict).
- Added `MCP_AUTOMATION_WS_HOST/PORT` and `MCP_AUTOMATION_CLIENT_PORT` to `8091`.
- Confirmed Unreal plugin listens on `8091` and accepts automation bridge connections.

## What Made It Fail

- Two MCP configs (global and project-local) caused duplicate MCP processes and unstable transport.
- Using `npx` at runtime often exceeded Codex MCP startup handshake window and caused startup timeouts.
- Setting only `MCP_AUTOMATION_PORT=8091` was not enough for this server build.
- Without WS vars, server could still default to WS port `8090`, causing disconnect.
- Stale Codex session state after config edits caused old launcher/process behavior until reset.

## Important Behavior Notes

- `resources/templates/list` returning `-32601 Method not found` is expected for this server build.
- This does not mean MCP is broken.
- Use `resources/list` and `resources/read` (`ue://health`, `ue://automation-bridge`) for health checks.
- Initial `ue://health` can show `disconnected` right after startup even when config is correct.
- In this server build, automation bridge connection is lazy and is typically established on first automation tool call.
- Practical meaning: one warm-up call is part of normal startup, not necessarily an error state.

## Why It Starts Disconnected

- MCP server process can start before it opens the Unreal automation bridge socket.
- Health/resources can therefore report `disconnected` until the first bridge-using tool request is sent.
- This behavior is implementation-specific and may not be emphasized in README examples.

## After PC Reboot: Will It Work by Default?

Yes, with the above single-config setup it should work by default, provided:

1. Unreal Editor is opened with this project.
2. Project finishes loading fully.
3. Codex session starts after config is already in place.

Practical startup order:

1. Open Unreal Editor and wait until the level/editor is ready.
2. Open VS Code/Codex (or reset session once if already open).
3. Run one safe warm-up command to force first bridge connect, for example `control_editor` with `stat none`.
4. Verify `ue://health` and `ue://automation-bridge`.

## Quick Verification Checklist

- `list_mcp_resources` returns `ue://health`, `ue://automation-bridge`, `ue://actors`, `ue://level`, `ue://assets`, `ue://version`.
- If `ue://health` is initially `disconnected`, run warm-up call: `control_editor` with `stat none`.
- `ue://health` shows `status: connected`.
- `ue://health` shows `automationBridge.connected: true`.
- `ue://health` shows `activePort: 8091`.
- `ue://automation-bridge` shows `summary.connected: true`.
- `ue://automation-bridge` shows `summary.port: 8091`.

## Recovery If Disconnected

1. Ensure Unreal Editor is running and listening on `8091`.
2. Kill stale `node/npx` processes for `unreal-engine-mcp-server`.
3. Reset Codex session.
4. Re-check `ue://health`.
5. Trigger one safe tool call (`control_editor` with `stat none`) and re-check.

## Optional Hardening Notes

- Keep `WASM_ENABLED=false` unless needed.
- Keep one config source only.
- If changing MCP config, always reset Codex session to load new values.
- Keep `C:/Users/baris/.codex/tools/Unreal_mcp/dist/cli.js` available. If missing, rebuild from the clone.

## Capability Reality Check (Smoke Test on 2026-02-11)

### Confirmed Working

- MCP registration and resource listing (`list_mcp_resources`).
- Bridge health/status resources (`ue://health`, `ue://automation-bridge`).
- Version/level/actor resources (`ue://version`, `ue://level`, `ue://actors`).
- Editor console command execution (`control_editor` + `console_command`).
- Screenshot capture (`control_editor` and `system_control` screenshot paths).
- Actor read operations (`control_actor` list/get).
- Asset workflow basics (`manage_asset` action `list`, `exists`, `get_dependencies`).
- Blueprint and character info reads (`manage_blueprint` get, `manage_character` get_character_info).
- Level structure info (`manage_level_structure` action `get_level_structure_info`).
- Sessions info (`manage_sessions` action `get_sessions_info`).

### Confirmed Not Working / Limited

- `resources/templates/list` returns `-32601 Method not found` (not implemented in this server build).
- `ue://assets` resource may return `count: 0` even when folders/assets exist.
- Python direct console commands are blocked by server validator:
- `py ...` fails.
- `python ...` fails.
- `py.exec ...` can return success at MCP layer but Unreal log shows `SyntaxError` for tested call.

### Confirmed Handler Issues

- `manage_asset` query-style actions currently misroute and fail with:
- `Unknown manage_sessions action: asset_query`
- Observed for `search_assets`, `find_by_tag`, and `get_source_control_state`.
- `manage_level` action `get_summary` failed in this project context (`No level specified` / `Level not tracked`), while `list_levels` works.

### Operational Guidance

- Treat this stack as reliable for editor/actor/control/session/level-structure automation.
- Treat Python scripting from MCP as not usable in current default safety mode.
- For asset queries, prefer fallback approaches until `asset_query` routing bug is fixed.
