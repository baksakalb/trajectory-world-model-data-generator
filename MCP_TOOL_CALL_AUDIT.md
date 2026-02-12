# Unreal MCP Tool-Call Audit

Date: 2026-02-12  
Project: `he_grenade_game`  
Repo: `main` @ `9715ac3`  
MCP server: `0.5.15` @ `29d1315`  
UE: `5.7.2-49658320+++UE5+Release-5.7`

## Scope
- Deep, tool-by-tool call validation.
- Read-only first, then reversible mutating probes in `/Game/__MCP_AUDIT__`.
- Deliverables:
- `MCP_TOOL_CALL_AUDIT.md` (this file)
- `MCP_TOOL_CALL_MATRIX.json` (machine-readable matrix)

## Baseline
- `ue://health` showed connected before probe sweep.
- `ue://automation-bridge` showed active port `8091`.
- Tool/resource surface was available and stable at start.
- Git worktree was clean at start.

## High-Level Findings
- Most exposed tools are callable and predictable once required selector fields are provided (`assetPath`, `blueprintPath`, `widgetPath`, `actorName`, etc.).
- Two routing defects are reproducible and high impact:
- `manage_asset` query-style flows (`search_assets`, `find_by_tag`, `get_source_control_state`) fail with `Unknown manage_sessions action: asset_query`.
- `manage_splines` actions fail with `Unknown manage_sessions action: <subAction>`.
- `manage_level:get_summary` is stateful: it succeeds after the level is loaded/tracked through `manage_level:load`.
- `manage_pipeline` exists in server source but is intentionally hidden from this Codex client.

## Tool Call Patterns (Observed)
- Pattern A: top-level tool name + payload `action`.
- Pattern B: tool name + action-specific required selector fields.
- Pattern C: same tool/action may have runtime-required fields not obvious from schema-level `required`.

## Tool-by-Tool Results (Ordered)

### animation_physics
- Probe: `get_animation_info` with `assetPath=/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle`.
- Result: success.
- Required in practice: `assetPath`.

### build_environment
- Probe: `get_foliage_instances`.
- Result: success (empty list, no foliage actor).
- Required in practice: none beyond `action`.

### control_actor
- Probe: `list`.
- Result: success.
- Required in practice: none beyond `action`.

### control_editor
- Probe: `console_command` with `stat none`.
- Result: success.
- Required in practice: `command`.

### inspect
- Probe: `list_objects`.
- Result: success.
- Required in practice: none beyond `action`.

### manage_ai
- Probe 1: `get_ai_info` without selector.
- Result: missing required selector.
- Probe 2: `get_ai_info` with `behaviorTreePath=/Game/__test_bt__.__test_bt__`.
- Result: success.
- Required in practice: one selector (`controllerPath|behaviorTreePath|blackboardPath|queryPath|stateTreePath|blueprintPath`).

### manage_asset
- Probe: `list` and `exists`.
- Result: success.
- Query probes: `search_assets`, `find_by_tag`, `get_source_control_state`.
- Result: routing defect (see Known Defects).

### manage_audio
- Probe: `get_audio_info` with SoundWave asset path.
- Result: success.
- Required in practice: `assetPath`.

### manage_behavior_tree
- Probe 1: `create` without args.
- Result: missing required args.
- Probe 2: `create` with `name` + `savePath` in `/Game/__MCP_AUDIT__`.
- Result: success (mutating probe).

### manage_blueprint
- Probe: `list_node_types` with `blueprintPath=/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter`.
- Result: success.
- Required in practice: `blueprintPath` (or `assetPath` depending action).

### manage_character
- Probe: `get_character_info` with `blueprintPath`.
- Result: success.
- Required in practice: `blueprintPath`.

### manage_combat
- Probe: `get_combat_info` with `blueprintPath`.
- Result: success.
- Required in practice: `blueprintPath`.

### manage_effect
- Probe 1: `get_niagara_info` with non-Niagara asset path.
- Result: invalid argument/type (`ASSET_NOT_FOUND`).
- Probe 2: `list_debug_shapes`.
- Result: success.

### manage_game_framework
- Probe: `get_game_framework_info`.
- Result: success.

### manage_gas
- Probe: `get_gas_info` with `assetPath`.
- Result: success.
- Required in practice: `assetPath`.

### manage_geometry
- Probe: `get_mesh_info` with common map actors (`PlayerStart_0`, `DirectionalLight_0`).
- Result: invalid argument (`ACTOR_NOT_FOUND`).
- Note: handler expects a target shape/context not matched by these actor names in current map.

### manage_input
- Probe 1: `create_input_action` without args.
- Result: invalid argument.
- Probe 2: `create_input_action` with `name=IA_MCP_AUDIT_TMP`, `path=/Game/__MCP_AUDIT__`.
- Result: success (mutating probe).

### manage_interaction
- Probe: `get_interaction_info` with `actorName=PlayerStart_0`.
- Result: success.
- Required in practice: one selector (`actorName|blueprintPath|...`).

### manage_inventory
- Probe 1: `get_inventory_info` without selector.
- Result: missing parameter.
- Probe 2: `get_inventory_info` with `blueprintPath`.
- Result: success.

### manage_level
- Probe: `list_levels`.
- Result: success.
- Additional probes:
- `get_summary` before `load`.
- Result: expected context failure (`No level specified` / `Level not tracked`).
- `load` for `/Game/FirstPerson/Lvl_FirstPerson`, then `get_summary` with same path.
- Result: success (`Level summary ready`).

### manage_level_structure
- Probe: `get_level_structure_info`.
- Result: success.

### manage_lighting
- Probe: `list_light_types`.
- Result: success.

### manage_material_authoring
- Probe 1: `get_material_info` without path.
- Result: missing required arg.
- Probe 2: `get_material_info` with `assetPath=/Game/Materials/M_GlassTile.M_GlassTile`.
- Result: success.

### manage_navigation
- Probe: `get_navigation_info`.
- Result: success.

### manage_networking
- Probe 1: `get_networking_info` without selector.
- Result: invalid params.
- Probe 2: `get_networking_info` with `blueprintPath`.
- Result: success.

### manage_performance
- Probe: `show_stats` with `enabled=true/false`.
- Result: success.

### manage_sequence
- Probe: `list`.
- Result: success (empty sequence set in project).

### manage_sessions
- Probe: `get_sessions_info`.
- Result: success.
- Important: this handler is also the erroneous fallback surface for unrelated calls (defects below).

### manage_skeleton
- Probe: `get_skeleton_info` with `skeletonPath=/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin`.
- Result: success.

### manage_splines
- Probe: `get_splines_info` and `create_spline_actor`.
- Result: routing defect (`Unknown manage_sessions action: ...`).

### manage_texture
- Probe: `get_texture_info` with `assetPath` to weapon texture.
- Result: success.

### manage_volumes
- Probe: `get_volumes_info`.
- Result: success.

### manage_widget_authoring
- Probe 1: `get_widget_info` with non-widget path.
- Result: not found.
- Probe 2: `create_widget_blueprint` + `get_widget_info` in `/Game/__MCP_AUDIT__`.
- Result: success (mutating probe).

### system_control
- Probe: `get_project_settings`.
- Result: success.
- Additional probe: `console_command` with `stat none`.
- Result: success.

### manage_pipeline (not exposed to Codex tool surface)
- Status: not directly callable through this Codex function surface.
- Root cause: server-side visibility filtering by client capability.

## Known Defects (Reproducible)

### 1) `asset_query` misroute through `manage_sessions`
- Symptom:
- `manage_asset` actions `search_assets`, `find_by_tag`, `get_source_control_state` fail with `Unknown manage_sessions action: asset_query`.
- Evidence:
- `C:/Users/baris/.codex/tools/Unreal_mcp/src/tools/assets.ts:99`
- `C:/Users/baris/Desktop/Unreal_mcp/plugins/McpAutomationBridge/Source/McpAutomationBridge/Private/McpAutomationBridge_AssetQueryHandlers.cpp:33`
- `C:/Users/baris/Desktop/Unreal_mcp/plugins/McpAutomationBridge/Source/McpAutomationBridge/Private/McpAutomationBridge_ProcessRequest.cpp:518`

### 2) `manage_splines` misroute through `manage_sessions`
- Symptom:
- `Unknown manage_sessions action: get_splines_info` and similarly for create actions.
- Evidence:
- `C:/Users/baris/.codex/tools/Unreal_mcp/src/tools/consolidated-tool-handlers.ts:452`
- `C:/Users/baris/Desktop/Unreal_mcp/plugins/McpAutomationBridge/Source/McpAutomationBridge/Private/McpAutomationBridge_ProcessRequest.cpp:518`
- `C:/Users/baris/Desktop/Unreal_mcp/plugins/McpAutomationBridge/Source/McpAutomationBridge/Private/McpAutomationBridgeSubsystem.cpp:889`
- Additional code context:
- `HandleManageSessionsAction` reads only payload `action` and does not gate by top-level `Action` first.
- `C:/Users/baris/Desktop/Unreal_mcp/plugins/McpAutomationBridge/Source/McpAutomationBridge/Private/McpAutomationBridge_SessionsHandlers.cpp:933`
- `C:/Users/baris/Desktop/Unreal_mcp/plugins/McpAutomationBridge/Source/McpAutomationBridge/Private/McpAutomationBridge_SessionsHandlers.cpp:935`

### 3) `manage_pipeline` visibility behavior
- Symptom:
- Tool is present in source definitions but not exposed in this Codex session.
- Evidence:
- `C:/Users/baris/.codex/tools/Unreal_mcp/src/server/tool-registry.ts:288`
- `C:/Users/baris/.codex/tools/Unreal_mcp/src/server/tool-registry.ts:294`

## Runtime Behavior Notes
- `manage_level:get_summary` is not a generic map query. It summarizes tracked levels in MCP tool state.
- Reliable sequence:
- `manage_level:list_levels` (discover path), `manage_level:load` (track current level), then `manage_level:get_summary`.
- Source context:
- `C:/Users/baris/.codex/tools/Unreal_mcp/src/tools/level.ts:200`
- `C:/Users/baris/.codex/tools/Unreal_mcp/src/tools/level.ts:271`

## Controlled Mutating Probe + Cleanup Status
- Created audit assets:
- `/Game/__MCP_AUDIT__/WBP_MCP_AUDIT_TMP.WBP_MCP_AUDIT_TMP`
- `/Game/__MCP_AUDIT__/IA_MCP_AUDIT_TMP.IA_MCP_AUDIT_TMP`
- `/Game/__MCP_AUDIT__/BT_MCP_AUDIT_TMP/BT_MCP_AUDIT_TMP.BT_MCP_AUDIT_TMP`
- Cleanup attempt:
- `manage_asset:bulk_delete` timed out, then MCP transport closed.
- Post-refresh verification:
- `manage_asset:exists` reports `exists=false` for all three audit assets.
- `manage_asset:list` for `/Game/__MCP_AUDIT__` returns `0 assets / 0 folders`.
- Disk check:
- `Content/__MCP_AUDIT__` exists and contains `FILE_COUNT=0`.

## Connection Lifecycle Notes
- MCP was connected throughout main sweep.
- Transport dropped during bulk-delete cleanup test.
- After drop, `resources/list` returned `Transport closed`.
- Unreal-MCP node processes were stale/multiple and were terminated for recovery attempts.
- After Codex refresh and warm-up (`control_editor` -> `stat none`), MCP reconnected and health returned `connected` on port `8091`.

## Practical Guidance
- Prefer these safe baseline probes before major automation:
- `control_editor` + `console_command: stat none`
- `manage_level:list_levels`
- `control_actor:list`
- `system_control:get_project_settings`
- For `manage_level:get_summary`, call `manage_level:load` first for the target level path.
- For tool families with selector requirements, always pass one explicit target path/name first.
- Avoid relying on `manage_asset` query actions and `manage_splines` until routing defects are fixed.

## Cross-Reference
- Full structured dataset: `MCP_TOOL_CALL_MATRIX.json`
