# MCP Automation Bridge - Complete Reference

> Plugin: **McpAutomationBridge** from [ChiR24/Unreal_mcp](https://github.com/ChiR24/Unreal_mcp)
> Engine: **Unreal Engine 5.7.2** | Protocol: **WebSocket (ws://127.0.0.1:8091)**
> Generated: 2026-02-11 from source code analysis + live testing

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Agent-Agnostic Reference (WebSocket Protocol)](#2-agent-agnostic-reference)
3. [Claude Code / VSCode Extension Setup](#3-claude-code--vscode-extension-setup)
4. [Dispatch System Explained](#4-dispatch-system-explained)
5. [All 84 Registered Handlers](#5-all-84-registered-handlers)
6. [All 56 Fallback Dispatchers](#6-all-56-fallback-dispatchers)
7. [The 33 Advertised GitHub Tools - Full Reference](#7-the-33-advertised-github-tools---full-reference)
8. [Bonus Tools (Not Advertised)](#8-bonus-tools-not-advertised)
9. [Test Results Summary](#9-test-results-summary)
10. [Troubleshooting](#10-troubleshooting)

---

## 1. Architecture Overview

```
AI Assistant (Claude Code, Cursor, etc.)
    |
    v
TypeScript MCP Server  (npx unreal-engine-mcp-server)
    |  Translates MCP tool calls -> WebSocket JSON
    v
WebSocket Connection  (ws://127.0.0.1:8091)
    |
    v
C++ Plugin (McpAutomationBridge)  inside Unreal Editor
    |  Dispatches to 84+ registered handlers + 56 fallback handlers
    v
Unreal Engine Editor APIs
```

Three communication layers:
- **MCP Server** (TypeScript/Node.js) — speaks MCP protocol to the AI agent
- **WebSocket Bridge** — JSON messages over WebSocket RFC 6455
- **C++ Plugin** — executes commands via native UE editor APIs on the GameThread

---

## 2. Agent-Agnostic Reference

Everything in this section works regardless of which AI agent or client you use.

### 2.1 WebSocket Protocol

**Connection:** `ws://127.0.0.1:8091` (configurable in DefaultGame.ini)

**Handshake:**
```json
// Client -> Server
{ "type": "bridge_hello", "capabilityToken": "" }

// Server -> Client
{
  "type": "bridge_ack",
  "serverName": "UnrealEditor",
  "sessionId": "<uuid>",
  "protocolVersion": 1,
  "capabilities": ["console_commands", "native_plugin"],
  "supportedOpcodes": ["automation_request"]
}
```

**Automation Request:**
```json
{
  "type": "automation_request",
  "requestId": "<unique-id>",
  "action": "<action-name>",
  "payload": { /* action-specific params */ }
}
```

**Automation Response:**
```json
{
  "type": "automation_response",
  "requestId": "<matching-id>",
  "success": true|false,
  "message": "optional status",
  "result": { /* handler-specific data */ }
}
```

### 2.2 Three Dispatch Patterns

The plugin uses three different patterns for how actions are identified:

#### Pattern A: Registered Handler + `action` field in payload
```json
{ "action": "control_actor", "payload": { "action": "list" } }
```
Handlers using this: `control_actor`, `control_editor`, `manage_level`, `manage_effect`

#### Pattern B: Registered Handler + `subAction` field in payload
```json
{ "action": "manage_navigation", "payload": { "subAction": "get_navigation_info" } }
```
Handlers using this: `manage_sequence`, `manage_navigation`, `manage_level_structure`, `manage_volumes`, `manage_behavior_tree`, `manage_geometry`, `manage_skeleton`, `manage_gas`, `manage_character`, `manage_combat`, `manage_ai`, `manage_inventory`, `manage_interaction`, `manage_game_framework`, `manage_widget_authoring`, `manage_networking`, `manage_material_authoring`, `manage_texture`, `manage_audio` (authoring), `manage_sessions`

#### Pattern C: Direct Action Name (top-level, no sub-action)
```json
{ "action": "list_light_types", "payload": {} }
{ "action": "create_sound_cue", "payload": { "name": "MySoundCue" } }
```
Handlers using this: `list_assets`, `list_light_types`, `console_command`, `list_blueprints`, `spawn_light`, `create_sound_cue`, `show_fps`, `generate_memory_report`, all `audio_*` prefixed actions, all performance actions

### 2.3 DefaultGame.ini Configuration

```ini
[/Script/McpAutomationBridge.McpAutomationBridgeSettings]
ListenHost=0.0.0.0
ListenPorts=8091
bAlwaysListen=True
bAllowNonLoopback=True
bRequireCapabilityToken=False
bMultiListen=False
```

---

## 3. Claude Code / VSCode Extension Setup

This section is specific to Claude Code running in VSCode.

### 3.1 .mcp.json (Project Root)

```json
{
  "mcpServers": {
    "unreal-engine": {
      "command": "npx",
      "args": ["unreal-engine-mcp-server"],
      "env": {
        "UE_PROJECT_PATH": "C:/Users/<user>/Documents/Unreal Projects/<project>"
      }
    }
  }
}
```

### 3.2 How Claude Code Uses It

- Claude Code reads `.mcp.json` on startup
- Spawns the TypeScript MCP server as a child process
- The MCP server connects to the plugin via WebSocket
- All 33+ tools become available as native Claude tools
- **Restart Claude Code** after creating/modifying `.mcp.json`

### 3.3 Key Difference: Agent vs Direct WebSocket

| | Claude Code (via MCP Server) | Direct WebSocket |
|---|---|---|
| Tool names | Mapped by MCP server (e.g., `control_actor`) | Raw action strings |
| Sub-actions | Abstracted into tool parameters | Must know `action` vs `subAction` field |
| Error handling | MCP server wraps errors | Raw JSON error responses |
| Connection | Managed by MCP server | Manual connect + handshake |

---

## 4. Dispatch System Explained

When the plugin receives an `automation_request`, it routes through this chain:

### Phase 0: O(1) Registry Lookup
Checks `AutomationHandlers` map for exact action name match. If found, calls the handler.

### Phase 1: Early Blueprint Check
If action looks blueprint-related (`blueprint_*`, `manage_blueprint*`, `*scs*`), tries `HandleBlueprintAction`.

### Phase 2: Fallback Chain (56 handlers in order)
Each handler checks if it can handle the action. If it returns `false`, the next handler tries.

**IMPORTANT:** If no handler matches, the request falls through to `HandleManageSessionsAction` (the last meaningful catch-all), which produces `"Unknown manage_sessions action"` errors.

---

## 5. All 84 Registered Handlers

These are registered via `RegisterHandler()` in the subsystem and dispatched via O(1) lookup.

### Property & Data Handlers
| Action | Description |
|--------|-------------|
| `execute_editor_function` | Call arbitrary editor functions |
| `set_object_property` | Set a property on any UObject |
| `get_object_property` | Get a property from any UObject |
| `array_append` | Append to array property |
| `array_remove` | Remove from array property |
| `array_insert` | Insert into array property |
| `array_get_element` | Get element from array |
| `array_set_element` | Set element in array |
| `array_clear` | Clear array property |
| `map_set_value` | Set value in map property |
| `map_get_value` | Get value from map property |
| `map_remove_key` | Remove key from map property |
| `map_has_key` | Check if map has key |
| `map_get_keys` | Get all keys from map |
| `map_clear` | Clear map property |
| `set_add` | Add to set property |
| `set_remove` | Remove from set property |
| `set_contains` | Check if set contains value |
| `set_clear` | Clear set property |

### Asset Workflow Handlers
| Action | Description |
|--------|-------------|
| `get_asset_references` | Get references to an asset |
| `get_asset_dependencies` | Get dependencies of an asset |
| `fixup_redirectors` | Fix up asset redirectors |
| `source_control_checkout` | Checkout file in source control |
| `source_control_submit` | Submit file to source control |
| `bulk_rename_assets` | Bulk rename multiple assets |
| `bulk_delete_assets` | Bulk delete multiple assets |
| `generate_thumbnail` | Generate asset thumbnail |

### Landscape & Foliage Handlers
| Action | Description |
|--------|-------------|
| `create_landscape` | Create a landscape actor |
| `create_procedural_terrain` | Create procedural terrain |
| `create_landscape_grass_type` | Create landscape grass type |
| `sculpt_landscape` | Sculpt landscape heightmap |
| `set_landscape_material` | Set material on landscape |
| `edit_landscape` | Edit landscape properties |
| `add_foliage_type` | Add a foliage type |
| `create_procedural_foliage` | Create procedural foliage spawner |
| `paint_foliage` | Paint foliage instances |
| `add_foliage_instances` | Add foliage at locations |
| `remove_foliage` | Remove foliage instances |
| `get_foliage_instances` | Get foliage instance data |

### Niagara / VFX Handlers
| Action | Description |
|--------|-------------|
| `create_niagara_system` | Create a Niagara system asset |
| `create_niagara_ribbon` | Create Niagara ribbon renderer |
| `create_niagara_emitter` | Create Niagara emitter |
| `spawn_niagara_actor` | Spawn Niagara actor in level |
| `modify_niagara_parameter` | Modify Niagara parameter |

### Animation Handlers
| Action | Description |
|--------|-------------|
| `create_anim_blueprint` | Create an Animation Blueprint |
| `play_anim_montage` | Play an animation montage |
| `setup_ragdoll` | Setup ragdoll physics |

### Material Handlers
| Action | Description |
|--------|-------------|
| `add_material_texture_sample` | Add texture sample to material |
| `add_material_expression` | Add expression to material |
| `create_material_nodes` | Create multiple material nodes |
| `rebuild_material` | Rebuild/recompile material |

### Sequencer Handlers
| Action | Description |
|--------|-------------|
| `add_sequencer_keyframe` | Add keyframe to sequencer |
| `manage_sequencer_track` | Manage sequencer tracks |
| `add_camera_track` | Add camera track to sequence |
| `add_animation_track` | Add animation track |
| `add_transform_track` | Add transform track |

### Core System Handlers
| Action | Description |
|--------|-------------|
| `manage_ui` | Manage UI/UMG elements |
| `control_environment` | Control environment settings |
| `build_environment` | Build environment (landscape, foliage) |
| `console_command` | Execute UE console command |
| `inspect` | Inspect UObjects |
| `system_control` | System control (engine version, etc.) |
| `manage_blueprint_graph` | Blueprint graph editing |
| `list_blueprints` | List all blueprints |
| `manage_world_partition` | World Partition management |
| `manage_render` | Render target management |
| `manage_input` | Enhanced Input management |

### Umbrella Handlers (dispatch to sub-actions)
| Action | Sub-action Field | Description |
|--------|-----------------|-------------|
| `control_actor` | `action` | Actor spawn/delete/transform/query |
| `manage_level` | `action` | Level load/save/stream/list |
| `manage_sequence` | `subAction` | Sequencer/cinematics |
| `manage_asset` | `subAction` | Asset management |
| `manage_behavior_tree` | `subAction` | Behavior tree editing |
| `manage_audio` | `subAction` | Audio authoring |
| `manage_lighting` | (top-level) | Lighting (uses direct action names) |
| `manage_physics` | `subAction` | Physics configuration |
| `manage_effect` | `action` | Effects (particles/Niagara) |
| `create_effect` | `action` | Alias for manage_effect |
| `clear_debug_shapes` | - | Clear debug draw shapes |
| `manage_performance` | (top-level) | Performance profiling |
| `manage_game_framework` | `subAction` | Game mode/state/controller |
| `manage_sessions` | `subAction` | Sessions/split-screen/LAN |
| `manage_level_structure` | `subAction` | Level structure/sublevels/HLOD |
| `manage_volumes` | `subAction` | Trigger/blocking/physics volumes |
| `manage_navigation` | `subAction` | NavMesh/nav links/pathfinding |

---

## 6. All 56 Fallback Dispatchers

These are tried in order when the O(1) registry lookup doesn't match. Each checks the top-level Action name.

| Order | Handler | Action Patterns Accepted |
|-------|---------|-------------------------|
| 1 | HandleBlueprintAction (early) | `blueprint_*`, `manage_blueprint*`, `*scs*` |
| 2 | HandleExecuteEditorFunction | `execute_editor_function` |
| 3 | HandleLevelAction | Level-related actions |
| 4 | HandleAssetAction (early) | `import`, `duplicate`, `rename`, `move`, `delete`, `create_folder`, `create_material`, `create_material_instance`, `list`, `list_assets`, `exists`, `manage_asset`, etc. |
| 5 | HandleSetObjectProperty | `set_object_property` |
| 6 | HandleGetObjectProperty | `get_object_property` |
| 7 | HandleAssetAction (late) | Same as early (retry) |
| 8 | HandleControlActorAction | `control_actor`, `spawn_actor`, `delete_actor`, etc. |
| 9 | HandleControlEditorAction | `control_editor`, `play`, `stop`, `eject`, etc. |
| 10 | HandleUiAction | `manage_ui`, UI-related |
| 11 | HandleBlueprintAction (late) | Blueprint actions (retry) |
| 12 | HandleSequenceAction | `sequence_*`, `manage_sequence` |
| 13 | HandleEffectAction | `create_effect`, `spawn_effect`, `manage_effect` |
| 14 | HandleAnimationPhysicsAction | `animation_physics`, `create_anim*`, `setup_ragdoll`, `play_anim*` |
| 15 | HandleAudioAction | `create_sound_cue`, `play_sound_*`, `audio_*`, `fade_sound_*` |
| 16 | HandleLightingAction | `spawn_light*`, `build_lighting`, `list_light_types`, `setup_volumetric_fog`, `setup_global_illumination`, `configure_shadows`, `set_exposure`, `set_ambient_occlusion` |
| 17 | HandlePerformanceAction | `generate_memory_report`, `start_profiling`, `stop_profiling`, `show_fps`, `show_stats`, `set_scalability`, `set_resolution_scale`, `set_vsync`, `set_frame_rate_limit`, `configure_nanite`, `configure_lod`, `configure_texture_streaming`, `merge_actors` |
| 18 | HandleBuildEnvironmentAction | `build_environment` |
| 19 | HandleControlEnvironmentAction | `control_environment` |
| 20 | HandleSystemControlAction | `system_control` |
| 21 | HandleConsoleCommandAction | `console_command` |
| 22 | HandleInspectAction | `inspect` |
| 23 | HandleBlueprintGraphAction | `manage_blueprint_graph` |
| 24 | HandleNiagaraGraphAction | Niagara graph editing |
| 25 | HandleMaterialGraphAction | Material graph editing |
| 26 | HandleBehaviorTreeAction | `manage_behavior_tree` |
| 27 | HandleWorldPartitionAction | `manage_world_partition` |
| 28 | HandleRenderAction | `manage_render` |
| 29 | HandleGeometryAction | `manage_geometry` |
| 30 | HandleManageSkeleton | `manage_skeleton` |
| 31 | HandleManageMaterialAuthoringAction | `manage_material_authoring` |
| 32 | HandleManageTextureAction | `manage_texture` |
| 33 | HandleManageAnimationAuthoringAction | Animation authoring |
| 34 | HandleManageAudioAuthoringAction | Audio authoring |
| 35 | HandleManageNiagaraAuthoringAction | Niagara authoring |
| 36 | HandleManageGASAction | `manage_gas` |
| 37 | HandleManageCharacterAction | `manage_character` |
| 38 | HandleManageCombatAction | `manage_combat` |
| 39 | HandleManageAIAction | `manage_ai` |
| 40 | HandleManageInventoryAction | `manage_inventory` |
| 41 | HandleManageInteractionAction | `manage_interaction` |
| 42 | HandleManageWidgetAuthoringAction | `manage_widget_authoring` |
| 43 | HandleManageNetworkingAction | `manage_networking` |
| 44 | HandleManageGameFrameworkAction | `manage_game_framework` |
| 45 | HandleManageSessionsAction | `manage_sessions` (CATCH-ALL) |
| 46 | HandleManageLevelStructureAction | `manage_level_structure` |
| 47 | HandleManageVolumesAction | `manage_volumes` |
| 48 | HandleManageNavigationAction | `manage_navigation` |
| 49 | HandleManageSplinesAction | Spline management |
| 50 | HandlePipelineAction | `manage_pipeline` |
| 51 | HandleTestAction | `manage_tests` |
| 52 | HandleLogAction | `manage_logs` |
| 53 | HandleDebugAction | `manage_debug` |
| 54 | HandleAssetQueryAction | Asset queries |
| 55 | HandleInsightsAction | `manage_insights` |

---

## 7. The 33 Advertised GitHub Tools - Full Reference

### Tool 1: `manage_asset`
- **Status:** WORKING
- **Dispatch:** Pattern A (`action` field) or Pattern C (direct `list_assets`)
- **Sub-action field:** `action` (via umbrella) or use direct action names

| Sub-action / Direct Action | Description |
|---------------------------|-------------|
| `list_assets` (direct) | List assets at path |
| `import` | Import external file |
| `duplicate` | Duplicate asset |
| `rename` | Rename asset |
| `move` | Move asset |
| `delete` | Delete asset |
| `create_folder` | Create content folder |
| `create_material` | Create new material |
| `create_material_instance` | Create material instance |
| `exists` | Check if asset exists |
| `validate` | Validate assets |
| `get_material_stats` | Get material statistics |
| `generate_report` | Generate asset report |

**Example:**
```json
{ "action": "list_assets", "payload": { "path": "/Game/Weapons", "recursive": true } }
```

---

### Tool 2: `control_actor`
- **Status:** WORKING
- **Dispatch:** Pattern A
- **Sub-action field:** `action` (in payload, case-insensitive)

| Sub-action | Description |
|-----------|-------------|
| `list` / `list_actors` | List all actors in level |
| `get` / `get_actor` / `get_actor_by_name` | Get actor details |
| `spawn` | Spawn actor by class |
| `spawn_blueprint` | Spawn from Blueprint |
| `delete` / `remove` | Delete actor |
| `set_transform` / `set_actor_transform` | Set position/rotation/scale |
| `get_transform` / `get_actor_transform` | Get transform |
| `set_visibility` / `set_actor_visibility` | Toggle visibility |
| `add_component` | Add component to actor |
| `set_component_properties` | Set component properties |
| `get_components` | List components |
| `duplicate` | Duplicate actor |
| `attach` | Attach to parent |
| `detach` | Detach from parent |
| `find_by_tag` | Find actors by tag |
| `find_by_name` | Find actor by name |
| `add_tag` / `remove_tag` | Manage tags |
| `delete_by_tag` | Delete all with tag |
| `apply_force` / `apply_force_to_actor` | Apply physics force |
| `set_blueprint_variables` | Set BP variables |
| `create_snapshot` / `restore_snapshot` | Actor state snapshots |
| `export` | Export actor |
| `get_bounding_box` | Get bounds |
| `get_metadata` | Get actor metadata |

**Example:**
```json
{ "action": "control_actor", "payload": { "action": "spawn", "className": "StaticMeshActor", "location": {"x": 0, "y": 0, "z": 100} } }
```

---

### Tool 3: `control_editor`
- **Status:** WORKING (write-only, needs targets)
- **Dispatch:** Pattern A
- **Sub-action field:** `action` (in payload, case-insensitive)

| Sub-action | Description |
|-----------|-------------|
| `play` | Start PIE (Play In Editor) |
| `stop` | Stop PIE |
| `eject` | Eject from possessed pawn |
| `possess` | Possess a pawn |
| `focus_actor` | Focus viewport on actor (requires `actorName`) |
| `set_camera` / `set_camera_position` / `set_viewport_camera` | Set viewport camera |
| `set_view_mode` | Set viewport mode (wireframe, lit, etc.) |
| `open_asset` | Open asset in editor |

**Example:**
```json
{ "action": "control_editor", "payload": { "action": "play" } }
```

---

### Tool 4: `manage_level`
- **Status:** WORKING
- **Dispatch:** Pattern A
- **Sub-action field:** `action` (in payload, case-insensitive)

| Sub-action | Description |
|-----------|-------------|
| `list` / `list_levels` | List all levels and maps |
| `load` / `load_level` | Load a level |
| `save` | Save current level |
| `save_as` / `save_level_as` | Save level as new file |
| `create_level` | Create new level |
| `stream` | Configure level streaming |
| `create_light` | Create light in level |
| `export_level` | Export level |
| `import_level` | Import level |
| `add_sublevel` | Add sublevel |

**Example:**
```json
{ "action": "manage_level", "payload": { "action": "list" } }
```

---

### Tool 5: `manage_lighting`
- **Status:** WORKING
- **Dispatch:** Pattern C (direct action names)
- **Sub-action field:** None - use direct action names

| Direct Action | Description |
|--------------|-------------|
| `list_light_types` | List available light classes |
| `spawn_light` | Spawn a light actor |
| `spawn_sky_light` | Spawn sky light |
| `build_lighting` | Build lightmaps |
| `ensure_single_sky_light` | Ensure only one sky light |
| `create_lighting_enabled_level` | Create lit level |
| `create_lightmass_volume` | Create lightmass importance volume |
| `setup_volumetric_fog` | Setup volumetric fog |
| `setup_global_illumination` | Configure GI |
| `configure_shadows` | Configure shadow settings |
| `set_exposure` | Set exposure settings |
| `set_ambient_occlusion` | Configure AO |

**Example:**
```json
{ "action": "list_light_types", "payload": {} }
{ "action": "spawn_light", "payload": { "type": "PointLight", "location": {"x": 0, "y": 0, "z": 300} } }
```

---

### Tool 6: `manage_performance`
- **Status:** WORKING (Pattern C only - direct action names)
- **Dispatch:** Pattern C (top-level action, NOT umbrella)
- **NOTE:** The registered `manage_performance` umbrella handler does NOT dispatch. Use direct action names.

| Direct Action | Description |
|--------------|-------------|
| `generate_memory_report` | Memory profiling report |
| `start_profiling` | Start CPU profiling |
| `stop_profiling` | Stop CPU profiling |
| `show_fps` | Show FPS counter |
| `show_stats` | Show statistics |
| `set_scalability` | Set scalability preset |
| `set_resolution_scale` | Set resolution scale |
| `set_vsync` | Toggle VSync |
| `set_frame_rate_limit` | Set frame rate cap |
| `configure_nanite` | Configure Nanite settings |
| `configure_lod` | Configure LOD settings |
| `configure_texture_streaming` | Texture streaming settings |
| `merge_actors` | Merge actors for optimization |

**Example:**
```json
{ "action": "show_fps", "payload": {} }
{ "action": "generate_memory_report", "payload": {} }
```

---

### Tool 7: `animation_physics`
- **Status:** WORKING
- **Dispatch:** Pattern A
- **Sub-action field:** `action` (in payload, case-insensitive)

| Sub-action | Description |
|-----------|-------------|
| `create_animation_bp` / `create_anim_blueprint` | Create Animation Blueprint |
| `create_blend_space` / `create_blend_tree` | Create Blend Space |
| `create_procedural_anim` | Create procedural animation |
| `create_state_machine` | Create state machine |
| `setup_ik` | Setup IK |
| `configure_vehicle` | Configure vehicle physics |
| `setup_physics_simulation` | Setup physics sim |
| `create_animation_asset` | Create animation asset |
| `setup_retargeting` | Setup retargeting |
| `play_montage` / `play_anim_montage` | Play animation montage |
| `add_notify` | Add anim notify |
| `cleanup` | Cleanup temp assets (requires `artifacts` array) |

**Also via Animation Authoring** (`subAction` field):
`create_animation_sequence`, `set_sequence_length`, `add_bone_track`, `set_bone_key`, `set_curve_key`, `add_notify`, `add_notify_state`, `add_sync_marker`, `create_montage`, `add_montage_section`, `create_blend_space_1d`, `create_blend_space_2d`, `create_aim_offset`, `create_anim_blueprint`, `add_state_machine`, `add_state`, `add_transition`, `create_control_rig`, `create_ik_rig`, `create_ik_retargeter`, `get_animation_info`

---

### Tool 8: `manage_effect`
- **Status:** PARTIAL (Pattern C works, umbrella broken)
- **Dispatch:** Pattern A (action field) + Pattern C (direct names)
- **Sub-action field:** `action` (in payload, case-insensitive)
- **NOTE:** The registered `manage_effect` umbrella falls through to `manage_sessions`. Use direct action names or the effect handler's `action` sub-action.

| Sub-action (via effect handler) | Description |
|------|-------------|
| `particle` | Spawn particle system |
| `niagara` / `spawn_niagara` | Spawn Niagara effect |
| `set_niagara_parameter` | Modify Niagara parameter |
| `activate_niagara` / `deactivate_niagara` | Toggle Niagara |
| `advance_simulation` | Step simulation |
| `create_dynamic_light` | Create dynamic light effect |
| `cleanup` | Cleanup effects |

| Direct Action (Pattern C) | Description |
|---------------------------|-------------|
| `create_niagara_system` | Create Niagara system asset |
| `spawn_niagara_actor` | Spawn Niagara in level |
| `modify_niagara_parameter` | Modify parameter |
| `create_niagara_ribbon` | Create ribbon renderer |
| `create_niagara_emitter` | Create emitter |
| `clear_debug_shapes` | Clear debug draw |

**Also via Niagara Authoring** (`subAction` field):
Full Niagara graph editing via `manage_niagara_authoring` or fallback handler.

---

### Tool 9: `manage_blueprint`
- **Status:** WORKING
- **Dispatch:** Pattern C (direct `list_blueprints`) + Pattern B for graph editing

| Direct Action / Sub-action | Description |
|---------------------------|-------------|
| `list_blueprints` (direct) | List all blueprints |
| `manage_blueprint_graph` (subAction) | Graph node editing |
| `blueprint_create` | Create new Blueprint |
| `blueprint_compile` | Compile Blueprint |
| `blueprint_add_variable` | Add variable |
| `blueprint_add_function` | Add function |
| `blueprint_add_event` | Add event |

---

### Tool 10: `build_environment`
- **Status:** WORKING (specific sub-actions only)
- **Dispatch:** Pattern A (`action` field, case-insensitive)

| Sub-action | Description |
|-----------|-------------|
| `add_foliage_instances` | Add foliage at locations |
| `get_foliage_instances` | Get foliage data |
| `remove_foliage` | Remove foliage |
| `paint_landscape` / `paint_landscape_layer` | Paint landscape |
| `sculpt_landscape` | Sculpt heightmap |
| `modify_heightmap` | Modify heightmap |
| `set_landscape_material` | Set landscape material |
| `create_landscape_grass_type` | Create grass type |
| `generate_lods` | Generate LODs |
| `bake_lightmap` | Bake lightmaps |
| `create_sky_sphere` | Create sky sphere |
| `set_time_of_day` | Set time of day |
| `create_fog_volume` | Create fog volume |
| `delete` | Delete environment element |

---

### Tool 11: `system_control`
- **Status:** WORKING
- **Dispatch:** Pattern A (`action` field)

| Sub-action | Description |
|-----------|-------------|
| `get_engine_version` | Get UE version info |
| `get_project_settings` | Get project settings |
| `get_feature_flags` | Get feature flags |
| `set_project_setting` | Set a project setting |
| `validate_assets` | Validate all assets |
| `engine_quit` | Quit editor |
| `inspect_object` | Inspect object by path |
| `get_property` / `set_property` | Get/set properties |
| `get_bounding_box` | Get actor bounds |
| `get_components` | Get components |
| `find_by_class` | Find objects by class |
| `inspect_class` | Inspect a UClass |

---

### Tool 12: `manage_sequence`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction` (case-insensitive, auto-prefixed with `sequence_`)

| subAction | Description |
|-----------|-------------|
| `list` | List all level sequences |
| `create` | Create new sequence |
| `set_display_rate` | Set display frame rate |
| `set_properties` | Set sequence properties |
| `open` | Open sequence in editor |
| `add_camera` | Add camera to sequence |
| `play` | Play sequence |
| `add_actor` | Bind actor to sequence |
| `list_bindings` | List sequence bindings |
| `add_track` | Add track |
| `list_tracks` | List tracks |
| `add_keyframe` | Add keyframe |

---

### Tool 13: `inspect`
- **Status:** WORKING (needs `objectPath`)
- **Dispatch:** Pattern A (`action` field)

| Sub-action | Description |
|-----------|-------------|
| `inspect_object` | Inspect object (requires `objectPath`) |
| `list_classes` | List UClasses |
| `world` | Inspect world |
| `get_component_property` | Get component property |
| `set_component_property` | Set component property |

---

### Tool 14: `manage_audio`
- **Status:** PARTIAL (direct action names work, umbrella broken)
- **Dispatch:** Pattern C (direct action names)
- **NOTE:** The registered `manage_audio` umbrella falls through. Use direct action names.

| Direct Action | Description |
|--------------|-------------|
| `create_sound_cue` / `audio_create_sound_cue` | Create Sound Cue |
| `play_sound_at_location` / `audio_play_sound_at_location` | Play sound at world location |
| `play_sound_2d` / `audio_play_sound_2d` | Play 2D sound |
| `create_sound_class` / `audio_create_sound_class` | Create Sound Class |
| `create_sound_mix` / `audio_create_sound_mix` | Create Sound Mix |
| `push_sound_mix` / `audio_push_sound_mix` | Push Sound Mix |
| `pop_sound_mix` / `audio_pop_sound_mix` | Pop Sound Mix |
| `play_sound_attached` / `audio_play_sound_attached` | Play attached sound |
| `fade_sound_out` / `audio_fade_sound_out` | Fade out |
| `fade_sound_in` / `audio_fade_sound_in` | Fade in |
| `create_ambient_sound` / `audio_create_ambient_sound` | Create ambient sound |
| `spawn_sound_at_location` / `audio_spawn_sound_at_location` | Spawn sound |
| `prime_sound` | Prime sound for playback |

**Also via Audio Authoring** (`subAction` field):
`create_sound_cue`, `add_cue_node`, `connect_cue_nodes`, `create_metasound`, `add_metasound_node`, `connect_metasound_nodes`, `create_attenuation_settings`, `configure_distance_attenuation`, `configure_spatialization`, `create_reverb_effect`, `get_audio_info`

---

### Tool 15: `manage_behavior_tree`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `create` | Create new BT asset |
| `add_node` | Add node to tree |
| `connect_nodes` | Connect nodes |
| `remove_node` | Remove node |
| `break_connections` | Break node connections |
| `set_node_properties` | Set node properties |

**Requires:** `assetPath` for all actions except `create`

---

### Tool 16: `manage_input`
- **Status:** WORKING
- **Dispatch:** Pattern A (`action` field)

| Sub-action | Description |
|-----------|-------------|
| `create_input_action` | Create Enhanced Input Action |
| `create_input_mapping_context` | Create Input Mapping Context |
| `add_mapping` | Add key mapping |
| `remove_mapping` | Remove key mapping |

**Requires:** `name` and `path`

---

### Tool 17: `manage_geometry`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `create_box` / `create_sphere` / `create_cylinder` / `create_cone` / `create_capsule` / `create_torus` / `create_plane` / `create_disc` / `create_stairs` / `create_spiral_stairs` / `create_ring` / `create_arch` / `create_pipe` / `create_ramp` | Create primitive meshes |
| `revolve` | Revolve profile |
| `boolean_union` / `boolean_subtract` / `boolean_intersection` | Boolean operations |
| `get_mesh_info` | Get mesh information |
| `recalculate_normals` / `flip_normals` | Normal operations |
| `simplify_mesh` / `subdivide` | Mesh simplification |
| `extrude` / `inset` / `outset` / `bevel` / `shell` | Mesh modeling |
| `bend` / `twist` / `taper` / `noise_deform` / `smooth` / `relax` / `stretch` / `spherify` / `cylindrify` | Mesh deformation |
| `mirror` / `array_linear` / `array_radial` | Instancing |
| `bridge` / `loft` / `sweep` / `loop_cut` | Advanced operations |
| `auto_uv` / `project_uv` / `transform_uvs` | UV operations |
| `convert_to_static_mesh` | Convert to StaticMesh |
| `generate_collision` | Generate collision |
| `duplicate_along_spline` | Spline instancing |

**Requires:** `actorName` for most operations

---

### Tool 18: `manage_skeleton`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `get_skeleton_info` | Get skeleton details |
| `list_bones` | List all bones |
| `list_sockets` | List sockets |
| `create_socket` / `configure_socket` | Socket management |
| `create_virtual_bone` | Create virtual bone |
| `create_physics_asset` | Create physics asset |
| `list_physics_bodies` / `add_physics_body` / `configure_physics_body` | Physics bodies |
| `add_physics_constraint` / `configure_constraint_limits` | Physics constraints |
| `rename_bone` / `set_bone_transform` | Bone editing |
| `create_morph_target` / `set_morph_target_deltas` / `import_morph_targets` | Morph targets |
| `bind_cloth_to_skeletal_mesh` / `assign_cloth_asset_to_mesh` | Cloth |
| `create_skeleton` / `add_bone` / `remove_bone` / `set_bone_parent` | Skeleton creation |
| `set_vertex_weights` / `auto_skin_weights` / `copy_weights` / `mirror_weights` | Skinning |

**Requires:** Skeleton asset path (via `skeletonPath` or `assetPath`)

---

### Tool 19: `manage_material_authoring`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `create_material` | Create new material |
| `set_blend_mode` / `set_shading_model` / `set_material_domain` | Material properties |
| `add_texture_sample` / `add_texture_coordinate` | Texture nodes |
| `add_scalar_parameter` / `add_vector_parameter` / `add_static_switch_parameter` | Parameters |
| `add_math_node` / `add_world_position` / `add_vertex_normal` / `add_pixel_depth` / `add_fresnel` / `add_reflection_vector` | Expression nodes |
| `add_panner` / `add_rotator` / `add_noise` / `add_voronoi` | UV/procedural nodes |
| `add_if` / `add_switch` / `add_custom_expression` | Logic nodes |
| `connect_nodes` / `disconnect_nodes` | Wire connections |
| `create_material_function` / `add_function_input` / `add_function_output` / `use_material_function` | Material functions |
| `create_material_instance` / `set_scalar_parameter_value` / `set_vector_parameter_value` / `set_texture_parameter_value` | Material instances |
| `create_landscape_material` / `create_decal_material` / `create_post_process_material` | Specialized materials |
| `add_landscape_layer` / `configure_layer_blend` | Landscape layers |
| `compile_material` | Compile material |
| `get_material_info` | Get material info |

**Requires:** `assetPath` for existing materials

---

### Tool 20: `manage_texture`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `create_noise_texture` / `create_gradient_texture` / `create_pattern_texture` | Create procedural textures |
| `create_normal_from_height` / `create_ao_from_mesh` | Generate maps |
| `set_compression_settings` / `set_texture_group` / `set_lod_bias` | Texture settings |
| `configure_virtual_texture` / `set_streaming_priority` | Streaming |
| `resize_texture` | Resize |
| `invert` / `desaturate` / `adjust_levels` / `blur` / `sharpen` | Image processing |
| `channel_pack` / `combine_textures` / `channel_extract` | Channel operations |
| `adjust_curves` | Curves adjustment |
| `get_texture_info` | Get texture info |

**Requires:** `assetPath`

---

### Tool 21: `manage_gas`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `add_ability_system_component` / `configure_asc` | ASC setup |
| `create_attribute_set` / `add_attribute` / `set_attribute_base_value` / `set_attribute_clamping` | Attributes |
| `create_gameplay_ability` / `set_ability_tags` / `set_ability_costs` / `set_ability_cooldown` / `set_ability_targeting` / `add_ability_task` / `set_activation_policy` / `set_instancing_policy` | Abilities |
| `create_gameplay_effect` / `set_effect_duration` / `add_effect_modifier` / `set_modifier_magnitude` / `add_effect_execution_calculation` / `add_effect_cue` / `set_effect_stacking` / `set_effect_tags` | Effects |
| `create_gameplay_cue_notify` / `configure_cue_trigger` / `set_cue_effects` | Gameplay Cues |
| `add_tag_to_asset` | Tag management |
| `get_gas_info` | Get GAS info |

**Requires:** `assetPath` (Blueprint with GAS component)

---

### Tool 22: `manage_character`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `create_character_blueprint` | Create character BP |
| `configure_capsule_component` / `configure_mesh_component` / `configure_camera_component` | Component config |
| `configure_movement_speeds` / `configure_jump` / `configure_rotation` | Movement |
| `add_custom_movement_mode` / `configure_nav_movement` | Custom movement |
| `setup_mantling` / `setup_vaulting` / `setup_climbing` / `setup_sliding` / `setup_wall_running` / `setup_grappling` | Advanced locomotion |
| `setup_footstep_system` / `map_surface_to_sound` / `configure_footstep_fx` | Footsteps |
| `get_character_info` | Get character info |

**Requires:** `blueprintPath`

---

### Tool 23: `manage_combat`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `create_weapon_blueprint` / `configure_weapon_mesh` / `configure_weapon_sockets` / `set_weapon_stats` | Weapon creation |
| `configure_hitscan` / `configure_projectile` / `configure_spread_pattern` / `configure_recoil_pattern` / `configure_aim_down_sights` | Firing mechanics |
| `create_projectile_blueprint` / `configure_projectile_movement` / `configure_projectile_collision` / `configure_projectile_homing` | Projectiles |
| `create_damage_type` / `configure_damage_execution` / `setup_hitbox_component` | Damage |
| `setup_reload_system` / `setup_ammo_system` / `setup_attachment_system` / `setup_weapon_switching` | Weapon systems |
| `configure_muzzle_flash` / `configure_tracer` / `configure_impact_effects` / `configure_shell_ejection` | VFX |
| `create_melee_trace` / `configure_combo_system` / `create_hit_pause` / `configure_hit_reaction` | Melee |
| `setup_parry_block_system` / `configure_weapon_trails` | Advanced melee |
| `get_combat_info` | Get combat info |

**Requires:** `blueprintPath`

---

### Tool 24: `manage_ai`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `create_ai_controller` / `assign_behavior_tree` / `assign_blackboard` | AI Controller |
| `create_blackboard_asset` / `add_blackboard_key` / `set_key_instance_synced` | Blackboard |
| `create_behavior_tree` / `add_composite_node` / `add_task_node` / `add_decorator` / `add_service` / `configure_bt_node` | Behavior Tree |
| `create_eqs_query` / `add_eqs_generator` / `add_eqs_context` / `add_eqs_test` / `configure_test_scoring` | EQS |
| `add_ai_perception_component` / `configure_sight_config` / `configure_hearing_config` / `configure_damage_sense_config` / `set_perception_team` | AI Perception |
| `create_state_tree` / `add_state_tree_state` / `add_state_tree_transition` / `configure_state_tree_task` | State Trees |
| `create_smart_object_definition` / `add_smart_object_slot` / `configure_slot_behavior` / `add_smart_object_component` | Smart Objects |
| `create_mass_entity_config` / `configure_mass_entity` / `add_mass_spawner` | Mass AI |
| `get_ai_info` | Get AI info |

---

### Tool 25: `manage_inventory`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `create_item_data_asset` / `set_item_properties` / `create_item_category` / `assign_item_category` | Items |
| `create_inventory_component` / `configure_inventory_slots` / `add_inventory_functions` / `configure_inventory_events` / `set_inventory_replication` | Inventory |
| `create_pickup_actor` / `configure_pickup_interaction` / `configure_pickup_respawn` / `configure_pickup_effects` | Pickups |
| `create_equipment_component` / `define_equipment_slots` / `configure_equipment_effects` / `add_equipment_functions` / `configure_equipment_visuals` | Equipment |
| `create_loot_table` / `add_loot_entry` / `configure_loot_drop` / `set_loot_quality_tiers` | Loot |
| `create_crafting_recipe` / `configure_recipe_requirements` / `create_crafting_station` / `add_crafting_component` | Crafting |
| `get_inventory_info` | Get inventory info |

---

### Tool 26: `manage_interaction`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `create_interaction_component` / `configure_interaction_trace` / `configure_interaction_widget` / `add_interaction_events` | Interaction system |
| `create_interactable_interface` | Interface |
| `create_door_actor` / `configure_door_properties` | Doors |
| `create_switch_actor` / `configure_switch_properties` | Switches |
| `create_chest_actor` / `configure_chest_properties` | Chests |
| `create_lever_actor` | Levers |
| `setup_destructible_mesh` / `add_destruction_component` | Destructibles |
| `create_trigger_actor` / `configure_trigger_events` | Triggers |
| `get_interaction_info` | Get interaction info |

---

### Tool 27: `manage_widget_authoring`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `create_widget_blueprint` / `set_widget_parent_class` | Widget creation |
| `add_canvas_panel` / `add_horizontal_box` / `add_vertical_box` / `add_overlay` / `add_grid_panel` / `add_wrap_box` / `add_scroll_box` / `add_size_box` / `add_border` | Layout |
| `add_text_block` / `add_rich_text_block` / `add_image` / `add_button` / `add_progress_bar` / `add_slider` / `add_check_box` / `add_text_input` / `add_combo_box` / `add_spin_box` / `add_list_view` / `add_tree_view` | Widgets |
| `set_anchor` / `set_alignment` / `set_position` / `set_size` / `set_padding` / `set_z_order` / `set_render_transform` / `set_visibility` / `set_style` | Styling |
| `bind_text` / `bind_visibility` / `bind_color` / `bind_enabled` / `bind_on_clicked` / `bind_on_hovered` / `bind_on_value_changed` / `create_property_binding` | Bindings |
| `create_widget_animation` / `add_animation_track` / `add_animation_keyframe` / `set_animation_loop` | Widget animation |
| `create_main_menu` / `create_pause_menu` / `create_hud_widget` / `create_settings_menu` / `create_loading_screen` | Premade templates |
| `add_health_bar` / `add_crosshair` / `add_ammo_counter` / `add_minimap` / `add_compass` / `add_interaction_prompt` / `add_objective_tracker` / `add_damage_indicator` | HUD elements |
| `create_inventory_ui` / `create_dialog_widget` / `create_radial_menu` | Game UI |
| `preview_widget` | Preview widget |
| `get_widget_info` | Get widget info |

**Requires:** `widgetPath`

---

### Tool 28: `manage_networking`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `set_property_replicated` / `set_replication_condition` / `configure_net_update_frequency` / `configure_net_priority` / `set_net_dormancy` / `configure_replication_graph` | Replication |
| `create_rpc_function` / `configure_rpc_validation` / `set_rpc_reliability` | RPCs |
| `set_owner` / `set_autonomous_proxy` / `check_has_authority` / `check_is_locally_controlled` | Authority |
| `configure_net_cull_distance` / `set_always_relevant` / `set_only_relevant_to_owner` | Relevancy |
| `configure_net_serialization` / `set_replicated_using` / `configure_push_model` | Serialization |
| `configure_client_prediction` / `configure_server_correction` / `add_network_prediction_data` / `configure_movement_prediction` | Prediction |
| `configure_net_driver` / `set_net_role` / `configure_replicated_movement` | Advanced |
| `get_networking_info` | Get networking info |

**Requires:** `blueprintPath` or `actorName`

---

### Tool 29: `manage_game_framework`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `create_game_mode` / `create_game_state` / `create_player_controller` / `create_player_state` / `create_game_instance` / `create_hud_class` | Create framework classes |
| `set_default_pawn_class` / `set_player_controller_class` / `set_game_state_class` / `set_player_state_class` | Class assignments |
| `configure_game_rules` / `setup_match_states` / `configure_round_system` / `configure_team_system` / `configure_scoring_system` | Game rules |
| `configure_spawn_system` / `configure_player_start` / `set_respawn_rules` / `configure_spectating` | Spawning |
| `get_game_framework_info` | Get framework info |

---

### Tool 30: `manage_sessions`
- **Status:** PARTIAL (catch-all handler, some sub-actions broken)
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `configure_local_session_settings` | Local session settings |
| `configure_session_interface` | Session interface |
| `configure_split_screen` / `set_split_screen_type` | Split screen |
| `add_local_player` / `remove_local_player` | Local players |
| `configure_lan_play` / `host_lan_server` / `join_lan_server` | LAN |
| `enable_voice_chat` / `configure_voice_settings` / `set_voice_channel` / `mute_player` / `set_voice_attenuation` / `configure_push_to_talk` | Voice chat |
| `get_sessions_info` | Get sessions info |

---

### Tool 31: `manage_level_structure`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `get_level_structure_info` | Get full level structure info |
| `create_level` / `create_sublevel` | Level creation |
| `configure_level_streaming` / `set_streaming_distance` / `configure_level_bounds` | Streaming |
| `enable_world_partition` / `configure_grid_size` | World Partition |
| `create_data_layer` / `assign_actor_to_data_layer` | Data Layers |
| `configure_hlod_layer` | HLOD |
| `create_minimap_volume` | Minimap |
| `open_level_blueprint` / `add_level_blueprint_node` / `connect_level_blueprint_nodes` | Level BP |
| `create_level_instance` / `create_packed_level_actor` | Level instances |

---

### Tool 32: `manage_volumes`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `create_trigger_volume` / `create_trigger_box` / `create_trigger_sphere` / `create_trigger_capsule` | Trigger volumes |
| `create_blocking_volume` / `create_kill_z_volume` / `create_pain_causing_volume` | Gameplay volumes |
| `create_physics_volume` | Physics volume |
| `create_audio_volume` / `create_reverb_volume` | Audio volumes |
| `create_cull_distance_volume` / `create_precomputed_visibility_volume` | Rendering volumes |
| `create_lightmass_importance_volume` | Lightmass volume |
| `create_nav_mesh_bounds_volume` / `create_nav_modifier_volume` | Navigation volumes |
| `create_camera_blocking_volume` | Camera volume |
| `set_volume_extent` / `set_volume_properties` | Volume properties |
| `get_volumes_info` | Get volumes info |

---

### Tool 33: `manage_navigation`
- **Status:** WORKING
- **Dispatch:** Pattern B
- **Sub-action field:** `subAction`

| subAction | Description |
|-----------|-------------|
| `configure_nav_mesh_settings` / `set_nav_agent_properties` / `rebuild_navigation` | NavMesh |
| `create_nav_modifier_component` / `set_nav_area_class` / `configure_nav_area_cost` | Nav areas |
| `create_nav_link_proxy` / `configure_nav_link` / `set_nav_link_type` | Nav links |
| `create_smart_link` / `configure_smart_link_behavior` | Smart links |
| `get_navigation_info` | Get navigation info |

---

## 8. Bonus Tools (Not Advertised on GitHub)

These exist in the plugin but aren't listed in the GitHub README:

| Tool | Dispatch | Description |
|------|----------|-------------|
| `console_command` | Pattern C | Execute any UE console command |
| `manage_render` | Pattern B (`subAction`) | Create render targets, Nanite rebuild, Lumen update |
| `manage_splines` | Pattern B (`subAction`) | Create/edit spline actors, scatter meshes along splines |
| `manage_pipeline` | Pattern B (`subAction`) | Run UBT |
| `manage_tests` | Pattern B (`subAction`) | Run automation tests |
| `manage_logs` | Pattern B (`subAction`) | Subscribe/unsubscribe to log streams |
| `manage_debug` | Pattern B (`subAction`) | Debug shape spawning |
| `manage_insights` | Pattern B (`subAction`) | Profiling sessions |
| `manage_world_partition` | Registered | World Partition management |
| `execute_editor_function` | Registered | Call arbitrary editor functions |
| `set_object_property` / `get_object_property` | Registered | Direct UObject property access |
| Array/Map/Set operations | Registered | 11 handlers for container manipulation |
| Source control | Registered | `source_control_checkout`, `source_control_submit` |
| `manage_ui` | Registered | UI/UMG operations |
| `control_environment` | Registered | Environment settings |

### Spline Sub-actions (complete)
`create_spline_actor`, `add_spline_point`, `remove_spline_point`, `set_spline_point_position`, `set_spline_point_tangents`, `set_spline_point_rotation`, `set_spline_point_scale`, `set_spline_type`, `create_spline_mesh_component`, `set_spline_mesh_asset`, `configure_spline_mesh_axis`, `set_spline_mesh_material`, `scatter_meshes_along_spline`, `configure_mesh_spacing`, `configure_mesh_randomization`, `create_road_spline`, `create_river_spline`, `create_fence_spline`, `create_wall_spline`, `create_cable_spline`, `create_pipe_spline`, `get_splines_info`

---

## 9. Test Results Summary

Live testing on 2026-02-11 against UE 5.7.2:

| Category | Status | Notes |
|----------|--------|-------|
| WebSocket handshake | PASS | Session established, protocol v1 |
| `manage_asset` / `list_assets` | PASS | Returns folders and assets |
| `control_actor` (list) | PASS | Lists all actors in level |
| `control_editor` | PASS | Needs actor/target parameter |
| `manage_level` (list) | PASS | Found Lvl_FirstPerson |
| `manage_lighting` / `list_light_types` | PASS | 5 light types |
| `manage_performance` | **USE DIRECT ACTIONS** | Umbrella broken, use `show_fps` etc. directly |
| `animation_physics` | PASS | Creates anim BPs |
| `manage_effect` | **USE DIRECT ACTIONS** | Umbrella broken, use `create_niagara_system` etc. |
| `manage_blueprint` / `list_blueprints` | PASS | Lists all BPs |
| `build_environment` | PASS | Foliage/landscape sub-actions work |
| `system_control` | PASS | Returns UE 5.7.2 version |
| `manage_sequence` (list) | PASS | 0 sequences |
| `inspect` | PASS | Needs objectPath |
| `manage_audio` | **USE DIRECT ACTIONS** | Umbrella broken, use `create_sound_cue` etc. |
| `manage_behavior_tree` | PASS | Creates BT assets |
| `manage_input` | PASS | Needs name+path |
| `manage_geometry` | PASS | Needs actorName |
| `manage_skeleton` | PASS | Needs skeletonPath |
| `manage_material_authoring` | PASS | Needs assetPath |
| `manage_texture` | PASS | Needs assetPath |
| `manage_gas` | PASS | Needs assetPath |
| `manage_character` | PASS | Needs blueprintPath |
| `manage_combat` | PASS | Needs blueprintPath |
| `manage_ai` | PASS | get_ai_info works |
| `manage_inventory` | PASS | get_inventory_info works |
| `manage_interaction` | PASS | get_interaction_info works |
| `manage_widget_authoring` | PASS | Needs widgetPath |
| `manage_networking` | PASS | Needs blueprintPath/actorName |
| `manage_game_framework` | PASS | get_game_framework_info works |
| `manage_sessions` | **PARTIAL** | Some sub-actions may not dispatch |
| `manage_level_structure` | PASS | Full level structure info |
| `manage_volumes` | PASS | get_volumes_info works |
| `manage_navigation` | PASS | NavMesh info returned |
| `console_command` | PASS | Executes UE commands |
| `manage_render` | PASS | Creates render targets |

---

## 10. Troubleshooting

### "Unknown manage_sessions action" Error
This means no handler recognized the action. The request fell through all 56 handlers and hit the `manage_sessions` catch-all. Check:
1. Is the action name spelled correctly?
2. Are you using the right sub-action field (`action` vs `subAction`)?
3. For `manage_performance`, `manage_audio`, `manage_effect` — use **direct action names** (Pattern C) instead of the umbrella handler.

### Handler Works But Returns Validation Error
This is normal for write-only handlers. They need:
- `actorName` — for actor-based operations
- `assetPath` — for asset-based operations
- `blueprintPath` — for Blueprint-based operations
- `skeletonPath` — for skeleton operations
- `widgetPath` — for widget operations

### Connection Refused on Port 8091
- Ensure Unreal Editor is running with the project open
- Check `DefaultGame.ini` has `bAlwaysListen=True`
- Verify the plugin is enabled in Edit > Plugins

### Claude Code Doesn't See MCP Tools
- Ensure `.mcp.json` exists in project root
- Restart Claude Code after creating `.mcp.json`
- Check `npx unreal-engine-mcp-server` runs without errors
