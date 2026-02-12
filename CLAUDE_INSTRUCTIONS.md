# Claude Agent - Unreal Engine MCP Environment Guide

> **PURPOSE:** This is the single source of truth for Claude (AI assistant) at the start of every session.
> It covers: how the MCP connection works, how to call every tool, how to verify the connection is healthy.
>
> For game-specific design context and current development state, read `SESSION_HANDOFF.md`
> separately -- that file covers gameplay systems, current state, and future priorities.
>
> Plugin: [ChiR24/Unreal_mcp](https://github.com/ChiR24/Unreal_mcp) | Engine: UE 5.7.2
> Protocol: WebSocket `ws://127.0.0.1:8091` | Config: `.mcp.json` in project root
> Generated: 2026-02-11 from source code analysis + live WebSocket testing

---

## Environment

- **Project root:** `C:\Users\baris\Documents\Unreal Projects\he_grenade_game`
- **Engine:** UE 5.7.2 (`5.7.2-49658320+++UE5+Release-5.7`)
- **Main level:** `/Game/FirstPerson/Lvl_FirstPerson`
- **MCP server:** `unreal-engine-mcp-server@0.5.15` (via npx)
- **Protocol:** WebSocket on `ws://127.0.0.1:8091`
- **Plugin source:** [ChiR24/Unreal_mcp](https://github.com/ChiR24/Unreal_mcp) (cloned at `C:\Users\baris\Desktop\Unreal_mcp`)

### Critical Config: `.mcp.json`

The `.mcp.json` in the project root configures the MCP server that Claude Code spawns.
It **MUST** include `MCP_AUTOMATION_CLIENT_PORT=8091` -- the default is 8090, which mismatches
the UE plugin's listen port of 8091.

```json
{
  "mcpServers": {
    "unreal-engine": {
      "command": "npx",
      "args": ["unreal-engine-mcp-server"],
      "env": {
        "UE_PROJECT_PATH": "C:/Users/baris/Documents/Unreal Projects/he_grenade_game",
        "MCP_AUTOMATION_HOST": "127.0.0.1",
        "MCP_AUTOMATION_PORT": "8091",
        "MCP_AUTOMATION_WS_HOST": "127.0.0.1",
        "MCP_AUTOMATION_WS_PORT": "8091",
        "MCP_AUTOMATION_CLIENT_PORT": "8091",
        "LOG_LEVEL": "info",
        "MCP_ROUTE_STDOUT_LOGS": "true"
      }
    }
  }
}
```

### UE Plugin Config (DefaultGame.ini)

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

## Workflow Rules

### Build Cycle (MANDATORY)
- **No Live Coding / No Live Edit** -- these are disabled and must never be used.
- The standard cycle is: **close editor -> build -> reopen editor**.
- Claude has full permission to do this autonomously:
  1. `Stop-Process -Name 'UnrealEditor'` (PowerShell)
  2. `Build.bat he_grenade_gameEditor Win64 Development ...`
  3. `Start-Process UnrealEditor.exe ...`
- **Do NOT ask the user to restart/reopen the editor.** Handle it yourself.
- After reopening, wait for the editor to load, then continue working via MCP.

### When to Prompt the User
- **Visual testing only:** When code changes are built and you need the user to Play and give feedback on look/feel.
- **Impossible tasks:** If something genuinely cannot be done programmatically and requires manual editor interaction, explain what and why, then wait.
- **Everything else:** Do it yourself. Delete actors, create materials, compile, reopen -- all autonomous.

### Build Commands
```powershell
# Close editor
powershell -Command "Stop-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue"

# Build
powershell -Command "& 'C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat' he_grenade_gameEditor Win64 Development '-Project=C:\Users\baris\Documents\Unreal Projects\he_grenade_game\he_grenade_game.uproject' -WaitMutex -FromMsBuild"

# Reopen editor
powershell -Command "Start-Process 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe' -ArgumentList '\"C:\Users\baris\Documents\Unreal Projects\he_grenade_game\he_grenade_game.uproject\"'"
```

### MCP Material Authoring: connect_nodes Convention
When connecting material nodes to the main material output, use:
- `targetNodeId: "Main"` (the material result node)
- `targetPin: "BaseColor"`, `"Metallic"`, `"Roughness"`, `"Opacity"`, `"Normal"`, etc.

---

## How Tool Calling Works

You send a JSON message with an `action` string and a `payload` object.
The payload sometimes contains a sub-action field. There are **three dispatch patterns:**

### Pattern A -- `action` field in payload (case-insensitive)
```json
{ "action": "control_actor", "payload": { "action": "list" } }
```
**Used by:** `control_actor`, `control_editor`, `manage_level`, `manage_effect`, `build_environment`, `system_control`, `inspect`, `manage_input`, `animation_physics`

### Pattern B -- `subAction` field in payload (some case-sensitive)
```json
{ "action": "manage_navigation", "payload": { "subAction": "get_navigation_info" } }
```
**Used by:** `manage_sequence`, `manage_navigation`, `manage_level_structure`, `manage_volumes`, `manage_behavior_tree`, `manage_geometry`, `manage_skeleton`, `manage_gas`, `manage_character`, `manage_combat`, `manage_ai`, `manage_inventory`, `manage_interaction`, `manage_game_framework`, `manage_widget_authoring`, `manage_networking`, `manage_material_authoring`, `manage_texture`, `manage_sessions`, `manage_splines`, `manage_pipeline`, `manage_tests`, `manage_logs`, `manage_debug`, `manage_insights`

### Pattern C -- Direct action name (no sub-action at all)
```json
{ "action": "list_light_types", "payload": {} }
```
**Used by:** `list_assets`, `list_light_types`, `list_blueprints`, `console_command`, `spawn_light`, `create_sound_cue`, `show_fps`, `generate_memory_report`, `create_niagara_system`, all `audio_*` prefixed actions, all direct performance/lighting/audio action names

### What Are Handlers and Dispatchers?

**Handlers** are C++ functions registered by exact name. When your request's `action` matches a registered name, it goes directly to that function. There are **84 registered handler names**.

**Fallback Dispatchers** are an internal routing chain of 56 functions. If no registered handler matches your action name, the plugin tries each dispatcher in order. Some dispatchers recognize pattern-based action names (e.g., `HandleLightingAction` recognizes anything starting with `spawn_light`). You never call a dispatcher directly -- they're internal plumbing that explains WHY certain direct action names (Pattern C) work.

### Common Required Fields in Payload
- `actorName` -- for actor-targeted operations
- `assetPath` -- for asset-targeted operations
- `blueprintPath` -- for Blueprint-targeted operations
- `skeletonPath` -- for skeleton operations
- `widgetPath` -- for widget operations
- `objectPath` -- for inspect operations
- `path` -- for list/create operations (e.g., `/Game/Weapons`)

### Known Broken Umbrella Handlers

These registered names exist but do NOT dispatch correctly. Always use Pattern C direct action names instead:
- `manage_performance` -> use `show_fps`, `generate_memory_report`, etc.
- `manage_effect` -> use `create_niagara_system`, `spawn_niagara_actor`, etc.
- `manage_audio` -> use `create_sound_cue`, `play_sound_at_location`, etc.

### Catch-All Error

If no handler matches your action name, the request falls through to `HandleManageSessionsAction`, which returns `"Unknown manage_sessions action"`. This error means your action name or sub-action field name was wrong.

---

## All Callable Tools

Each tool below is marked:
- **TESTED** = Verified working via live WebSocket test on 2026-02-11
- **SOURCE** = Sub-actions read from C++ source code but not individually tested
- **BROKEN** = Tested and confirmed not working via the umbrella handler

---

### 1. `control_actor` -- Actor Management
- **Pattern:** A (`action` field, case-insensitive)
- **Test status:** TESTED (list sub-action confirmed working)

| Sub-action | Description | Tested? |
|-----------|-------------|---------|
| `list` / `list_actors` | List all actors in level | TESTED |
| `get` / `get_actor` / `get_actor_by_name` | Get actor details | SOURCE |
| `spawn` | Spawn actor by class | SOURCE |
| `spawn_blueprint` | Spawn from Blueprint | SOURCE |
| `delete` / `remove` | Delete actor | SOURCE |
| `set_transform` / `set_actor_transform` | Set position/rotation/scale | SOURCE |
| `get_transform` / `get_actor_transform` | Get transform | SOURCE |
| `set_visibility` / `set_actor_visibility` | Toggle visibility | SOURCE |
| `add_component` | Add component to actor | SOURCE |
| `set_component_properties` | Set component properties | SOURCE |
| `get_components` | List components | SOURCE |
| `duplicate` | Duplicate actor | SOURCE |
| `attach` / `detach` | Attach/detach from parent | SOURCE |
| `find_by_tag` / `find_by_name` | Find actors | SOURCE |
| `add_tag` / `remove_tag` / `delete_by_tag` | Tag management | SOURCE |
| `apply_force` / `apply_force_to_actor` | Apply physics force | SOURCE |
| `set_blueprint_variables` | Set BP variables on actor | SOURCE |
| `create_snapshot` / `restore_snapshot` | Actor state snapshots | SOURCE |
| `export` | Export actor | SOURCE |
| `get_bounding_box` | Get bounds | SOURCE |
| `get_metadata` | Get actor metadata | SOURCE |

**Example:**
```json
{ "action": "control_actor", "payload": { "action": "list" } }
{ "action": "control_actor", "payload": { "action": "spawn", "className": "StaticMeshActor", "location": {"x": 0, "y": 0, "z": 100} } }
```

---

### 2. `control_editor` -- Editor / Viewport Control
- **Pattern:** A (`action` field, case-insensitive)
- **Test status:** TESTED (handler responds, needs target parameters)

| Sub-action | Description | Tested? |
|-----------|-------------|---------|
| `play` | Start PIE (Play In Editor) | SOURCE |
| `stop` | Stop PIE | SOURCE |
| `eject` | Eject from possessed pawn | SOURCE |
| `possess` | Possess a pawn | SOURCE |
| `focus_actor` | Focus viewport on actor (needs `actorName`) | TESTED |
| `set_camera` / `set_camera_position` / `set_viewport_camera` | Set viewport camera | SOURCE |
| `set_view_mode` | Set viewport mode (wireframe, lit, etc.) | SOURCE |
| `open_asset` | Open asset in editor | SOURCE |

**Example:**
```json
{ "action": "control_editor", "payload": { "action": "play" } }
{ "action": "control_editor", "payload": { "action": "focus_actor", "actorName": "MyActor" } }
```

---

### 3. `manage_level` -- Level Management
- **Pattern:** A (`action` field, case-insensitive)
- **Test status:** TESTED (list sub-action confirmed working)

| Sub-action | Description | Tested? |
|-----------|-------------|---------|
| `list` / `list_levels` | List all levels and maps | TESTED |
| `load` / `load_level` | Load a level | SOURCE |
| `save` | Save current level | SOURCE |
| `save_as` / `save_level_as` | Save level as new file | SOURCE |
| `create_level` | Create new level | SOURCE |
| `stream` | Configure level streaming | SOURCE |
| `create_light` | Create light in level | SOURCE |
| `export_level` / `import_level` | Export/import level | SOURCE |
| `add_sublevel` | Add sublevel | SOURCE |

**Example:**
```json
{ "action": "manage_level", "payload": { "action": "list" } }
```

---

### 4. `list_assets` / `manage_asset` -- Asset Management
- **Pattern:** C (direct `list_assets`) or A (umbrella `manage_asset` with `action` field)
- **Test status:** TESTED (`list_assets` confirmed working)

| Action | Description | Tested? |
|--------|-------------|---------|
| `list_assets` (Pattern C) | List assets at path | TESTED |
| `import` | Import external file | SOURCE |
| `duplicate` | Duplicate asset | SOURCE |
| `rename` | Rename asset | SOURCE |
| `move` | Move asset | SOURCE |
| `delete` | Delete asset | SOURCE |
| `create_folder` | Create content folder | SOURCE |
| `create_material` | Create new material | SOURCE |
| `create_material_instance` | Create material instance | SOURCE |
| `exists` | Check if asset exists | SOURCE |
| `validate` | Validate assets | SOURCE |
| `get_material_stats` | Get material statistics | SOURCE |
| `generate_report` | Generate asset report | SOURCE |

**Example:**
```json
{ "action": "list_assets", "payload": { "path": "/Game/Weapons", "recursive": true } }
```

---

### 5. Lighting -- Direct Action Names
- **Pattern:** C (direct action names -- NO umbrella handler)
- **Test status:** TESTED (`list_light_types` confirmed working)
- **NOTE:** The registered `manage_lighting` umbrella does NOT work. Use direct action names.

| Direct Action | Description | Tested? |
|--------------|-------------|---------|
| `list_light_types` | List available light classes | TESTED |
| `spawn_light` | Spawn a light actor | SOURCE |
| `spawn_sky_light` | Spawn sky light | SOURCE |
| `build_lighting` | Build lightmaps | SOURCE |
| `ensure_single_sky_light` | Ensure only one sky light | SOURCE |
| `create_lighting_enabled_level` | Create lit level | SOURCE |
| `create_lightmass_volume` | Create lightmass importance volume | SOURCE |
| `setup_volumetric_fog` | Setup volumetric fog | SOURCE |
| `setup_global_illumination` | Configure GI | SOURCE |
| `configure_shadows` | Configure shadow settings | SOURCE |
| `set_exposure` | Set exposure settings | SOURCE |
| `set_ambient_occlusion` | Configure AO | SOURCE |

**Example:**
```json
{ "action": "list_light_types", "payload": {} }
{ "action": "spawn_light", "payload": { "type": "PointLight", "location": {"x": 0, "y": 0, "z": 300} } }
```

---

### 6. Performance -- Direct Action Names
- **Pattern:** C (direct action names -- NO umbrella handler)
- **BROKEN UMBRELLA:** `manage_performance` does NOT dispatch. Always use direct action names.

| Direct Action | Description | Tested? |
|--------------|-------------|---------|
| `show_fps` | Show FPS counter | SOURCE |
| `show_stats` | Show statistics | SOURCE |
| `generate_memory_report` | Memory profiling report | SOURCE |
| `start_profiling` / `stop_profiling` | CPU profiling | SOURCE |
| `set_scalability` | Set scalability preset | SOURCE |
| `set_resolution_scale` | Set resolution scale | SOURCE |
| `set_vsync` | Toggle VSync | SOURCE |
| `set_frame_rate_limit` | Set frame rate cap | SOURCE |
| `configure_nanite` | Configure Nanite settings | SOURCE |
| `configure_lod` | Configure LOD settings | SOURCE |
| `configure_texture_streaming` | Texture streaming settings | SOURCE |
| `merge_actors` | Merge actors for optimization | SOURCE |

**Example:**
```json
{ "action": "show_fps", "payload": {} }
```

---

### 7. Audio -- Direct Action Names + Audio Authoring
- **Pattern:** C (direct action names) + Audio Authoring via Pattern B
- **BROKEN UMBRELLA:** `manage_audio` does NOT dispatch. Use direct action names.

**Direct action names (Pattern C, case-insensitive):**

| Direct Action | Description | Tested? |
|--------------|-------------|---------|
| `create_sound_cue` / `audio_create_sound_cue` | Create Sound Cue | SOURCE |
| `play_sound_at_location` / `audio_play_sound_at_location` | Play sound at world location | SOURCE |
| `play_sound_2d` / `audio_play_sound_2d` | Play 2D sound | SOURCE |
| `create_sound_class` / `audio_create_sound_class` | Create Sound Class | SOURCE |
| `create_sound_mix` / `audio_create_sound_mix` | Create Sound Mix | SOURCE |
| `push_sound_mix` / `pop_sound_mix` | Push/Pop Sound Mix | SOURCE |
| `play_sound_attached` / `audio_play_sound_attached` | Play attached sound | SOURCE |
| `fade_sound_out` / `fade_sound_in` | Fade sound | SOURCE |
| `create_ambient_sound` / `audio_create_ambient_sound` | Create ambient sound | SOURCE |
| `spawn_sound_at_location` | Spawn sound | SOURCE |
| `prime_sound` | Prime sound for playback | SOURCE |

**Audio Authoring (Pattern B, `subAction` field, case-sensitive):**
```json
{ "action": "manage_audio_authoring", "payload": { "subAction": "create_metasound", ... } }
```
Sub-actions: `create_sound_cue`, `add_cue_node`, `connect_cue_nodes`, `set_cue_attenuation`, `set_cue_concurrency`, `create_metasound`, `add_metasound_node`, `connect_metasound_nodes`, `add_metasound_input`, `add_metasound_output`, `set_metasound_default`, `create_sound_class`, `set_class_properties`, `set_class_parent`, `create_sound_mix`, `add_mix_modifier`, `configure_mix_eq`, `create_attenuation_settings`, `configure_distance_attenuation`, `configure_spatialization`, `configure_occlusion`, `configure_reverb_send`, `create_dialogue_voice`, `create_dialogue_wave`, `set_dialogue_context`, `create_reverb_effect`, `create_source_effect_chain`, `add_source_effect`, `create_submix_effect`, `get_audio_info`

---

### 8. Effects / VFX / Niagara
- **Pattern:** C (direct action names) + effect handler via Pattern A
- **BROKEN UMBRELLA:** `manage_effect` does NOT dispatch via the registered handler. Use direct action names or the fallback effect handler.

**Direct action names (Pattern C):**

| Direct Action | Description | Tested? |
|--------------|-------------|---------|
| `create_niagara_system` | Create Niagara system asset | SOURCE |
| `spawn_niagara_actor` | Spawn Niagara in level | SOURCE |
| `modify_niagara_parameter` | Modify Niagara parameter | SOURCE |
| `create_niagara_ribbon` | Create ribbon renderer | SOURCE |
| `create_niagara_emitter` | Create emitter | SOURCE |
| `clear_debug_shapes` | Clear debug draw | SOURCE |

**Via effect fallback handler (Pattern A, `action` field):**

| Sub-action | Description | Tested? |
|-----------|-------------|---------|
| `particle` | Spawn particle system | SOURCE |
| `niagara` / `spawn_niagara` | Spawn Niagara effect | SOURCE |
| `set_niagara_parameter` | Modify parameter | SOURCE |
| `activate_niagara` / `deactivate_niagara` | Toggle | SOURCE |
| `advance_simulation` | Step simulation | SOURCE |
| `create_dynamic_light` | Create dynamic light effect | SOURCE |
| `cleanup` | Cleanup effects | SOURCE |

---

### 9. `animation_physics` -- Animation & Physics
- **Pattern:** A (`action` field, case-insensitive)
- **Test status:** TESTED (create_anim_blueprint confirmed working)

| Sub-action | Description | Tested? |
|-----------|-------------|---------|
| `create_animation_bp` / `create_anim_blueprint` | Create Animation Blueprint | TESTED |
| `create_blend_space` / `create_blend_tree` | Create Blend Space | SOURCE |
| `create_procedural_anim` | Create procedural animation | SOURCE |
| `create_state_machine` | Create state machine | SOURCE |
| `setup_ik` | Setup IK | SOURCE |
| `configure_vehicle` | Configure vehicle physics | SOURCE |
| `setup_physics_simulation` | Setup physics sim | SOURCE |
| `create_animation_asset` | Create animation asset | SOURCE |
| `setup_retargeting` | Setup retargeting | SOURCE |
| `play_montage` / `play_anim_montage` | Play animation montage | SOURCE |
| `add_notify` | Add anim notify | SOURCE |
| `cleanup` | Cleanup temp assets (requires `artifacts` array) | SOURCE |

**Animation Authoring (Pattern B, `subAction` field, case-sensitive):**
Sub-actions: `create_animation_sequence`, `set_sequence_length`, `add_bone_track`, `set_bone_key`, `set_curve_key`, `add_notify`, `add_notify_state`, `add_sync_marker`, `set_root_motion_settings`, `set_additive_settings`, `create_montage`, `add_montage_section`, `add_montage_slot`, `set_section_timing`, `add_montage_notify`, `set_blend_in`, `set_blend_out`, `link_sections`, `create_blend_space_1d`, `create_blend_space_2d`, `add_blend_sample`, `set_axis_settings`, `set_interpolation_settings`, `create_aim_offset`, `add_aim_offset_sample`, `create_anim_blueprint`, `add_state_machine`, `add_state`, `add_transition`, `set_transition_rules`, `add_blend_node`, `add_cached_pose`, `add_slot_node`, `add_layered_blend_per_bone`, `set_anim_graph_node_value`, `create_control_rig`, `add_control`, `add_rig_unit`, `connect_rig_elements`, `create_pose_library`, `create_ik_rig`, `add_ik_chain`, `create_ik_retargeter`, `set_retarget_chain_mapping`, `get_animation_info`

---

### 10. `list_blueprints` / `manage_blueprint_graph` -- Blueprints
- **Pattern:** C (direct `list_blueprints`, `blueprint_*`) + B for graph editing
- **Test status:** TESTED (`list_blueprints` confirmed working)

| Action | Description | Tested? |
|--------|-------------|---------|
| `list_blueprints` (direct, Pattern C) | List all blueprints | TESTED |
| `blueprint_create` (Pattern C) | Create new Blueprint | SOURCE |
| `blueprint_compile` (Pattern C) | Compile Blueprint | SOURCE |
| `blueprint_add_variable` (Pattern C) | Add variable | SOURCE |
| `blueprint_add_function` (Pattern C) | Add function | SOURCE |
| `blueprint_add_event` (Pattern C) | Add event | SOURCE |
| `manage_blueprint_graph` (Pattern B, `subAction`) | Graph node editing | SOURCE |

---

### 11. `build_environment` -- Landscape, Foliage, Environment
- **Pattern:** A (`action` field, case-insensitive)

| Sub-action | Description | Tested? |
|-----------|-------------|---------|
| `add_foliage_instances` / `get_foliage_instances` / `remove_foliage` | Foliage | SOURCE |
| `paint_landscape` / `paint_landscape_layer` / `sculpt_landscape` | Landscape painting | SOURCE |
| `modify_heightmap` | Modify heightmap | SOURCE |
| `set_landscape_material` / `create_landscape_grass_type` | Landscape materials | SOURCE |
| `generate_lods` / `bake_lightmap` | Build operations | SOURCE |
| `create_sky_sphere` / `set_time_of_day` / `create_fog_volume` | Environment | SOURCE |
| `delete` | Delete environment element | SOURCE |

---

### 12. `system_control` -- Engine & Project
- **Pattern:** A (`action` field)
- **Test status:** TESTED (`get_engine_version` confirmed working)

| Sub-action | Description | Tested? |
|-----------|-------------|---------|
| `get_engine_version` | Get UE version info | TESTED |
| `get_project_settings` | Get project settings | SOURCE |
| `get_feature_flags` | Get feature flags | SOURCE |
| `set_project_setting` | Set a project setting | SOURCE |
| `validate_assets` | Validate all assets | SOURCE |
| `console_command` | Execute console command | SOURCE |
| `engine_quit` | Quit editor | SOURCE |
| `inspect_object` | Inspect object by path | SOURCE |
| `get_property` / `set_property` | Get/set properties | SOURCE |
| `get_bounding_box` / `get_components` | Actor utilities | SOURCE |
| `find_by_class` / `inspect_class` | Class introspection | SOURCE |

---

### 13. `manage_sequence` -- Sequencer / Cinematics
- **Pattern:** B (`subAction` field, case-insensitive, auto-prefixed with `sequence_`)
- **Test status:** TESTED (list confirmed working, returned 0 sequences)

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `list` | List all level sequences | TESTED |
| `create` | Create new sequence | SOURCE |
| `set_display_rate` | Set display frame rate | SOURCE |
| `set_properties` | Set sequence properties | SOURCE |
| `open` | Open sequence in editor | SOURCE |
| `add_camera` | Add camera to sequence | SOURCE |
| `play` | Play sequence | SOURCE |
| `add_actor` | Bind actor to sequence | SOURCE |
| `list_bindings` / `list_tracks` | List bindings/tracks | SOURCE |
| `add_track` / `add_keyframe` | Add track/keyframe | SOURCE |

**Example:**
```json
{ "action": "manage_sequence", "payload": { "subAction": "list" } }
{ "action": "manage_sequence", "payload": { "subAction": "create", "name": "MyCinematic", "path": "/Game" } }
```

---

### 14. `inspect` -- Object Introspection
- **Pattern:** A (`action` field)
- **Test status:** TESTED (handler responds, needs objectPath)

| Sub-action | Description | Tested? |
|-----------|-------------|---------|
| `inspect_object` | Inspect object (requires `objectPath`) | TESTED |
| `list_classes` | List UClasses | SOURCE |
| `world` | Inspect world | SOURCE |
| `get_component_property` / `set_component_property` | Component properties | SOURCE |

---

### 15. `console_command` -- Execute UE Console Commands
- **Pattern:** C (direct action)
- **Test status:** TESTED (confirmed working)
- **NOTE:** Single commands only -- no chaining (`&&`, `||`, `;`), no pipes, no backticks.

**Example:**
```json
{ "action": "console_command", "payload": { "command": "stat fps" } }
{ "action": "console_command", "payload": { "command": "stat unit" } }
```

---

### 16. `manage_behavior_tree` -- Behavior Tree Editing
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (create confirmed working)
- **Requires:** `assetPath` for all sub-actions except `create`

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `create` | Create new BT asset | TESTED |
| `add_node` | Add node to tree | SOURCE |
| `connect_nodes` | Connect nodes | SOURCE |
| `remove_node` | Remove node | SOURCE |
| `break_connections` | Break node connections | SOURCE |
| `set_node_properties` | Set node properties | SOURCE |

---

### 17. `manage_input` -- Enhanced Input
- **Pattern:** A (`action` field)
- **Test status:** TESTED (handler responds, needs name+path)

| Sub-action | Description | Tested? |
|-----------|-------------|---------|
| `create_input_action` | Create Enhanced Input Action | TESTED |
| `create_input_mapping_context` | Create Input Mapping Context | SOURCE |
| `add_mapping` / `remove_mapping` | Manage key mappings | SOURCE |

---

### 18. `manage_geometry` -- Procedural Mesh (Geometry Script)
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (handler responds, needs actorName)
- **Requires:** `actorName` for most operations

| subAction | Description | Tested? |
|-----------|-------------|---------|
| **Primitives:** `create_box`, `create_sphere`, `create_cylinder`, `create_cone`, `create_capsule`, `create_torus`, `create_plane`, `create_disc`, `create_stairs`, `create_spiral_stairs`, `create_ring`, `create_arch`, `create_pipe`, `create_ramp` | Create primitive meshes | SOURCE |
| **Booleans:** `boolean_union`, `boolean_subtract`, `boolean_intersection` | Boolean operations | SOURCE |
| **Query:** `get_mesh_info` | Get mesh information | SOURCE |
| **Normals:** `recalculate_normals`, `flip_normals` | Normal operations | SOURCE |
| **Modeling:** `extrude`, `inset`, `outset`, `bevel`, `shell`, `chamfer` | Mesh modeling | SOURCE |
| **Deform:** `bend`, `twist`, `taper`, `noise_deform`, `smooth`, `relax`, `stretch`, `spherify`, `cylindrify` | Mesh deformation | SOURCE |
| **Instance:** `mirror`, `array_linear`, `array_radial` | Instancing | SOURCE |
| **Advanced:** `bridge`, `loft`, `sweep`, `loop_cut`, `revolve` | Advanced ops | SOURCE |
| **UV:** `auto_uv`, `project_uv`, `transform_uvs` | UV operations | SOURCE |
| **Convert:** `convert_to_static_mesh`, `generate_collision` | Conversion | SOURCE |
| **Mesh ops:** `simplify_mesh`, `subdivide`, `weld_vertices`, `fill_holes`, `remove_degenerates`, `remesh_uniform`, `merge_vertices`, `triangulate`, `poke` | Mesh editing | SOURCE |

---

### 19. `manage_skeleton` -- Skeleton, Physics Assets, Cloth
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (handler responds, needs skeletonPath)
- **Requires:** `skeletonPath` or `assetPath`

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `get_skeleton_info` / `list_bones` / `list_sockets` | Query | SOURCE |
| `create_socket` / `configure_socket` / `create_virtual_bone` | Sockets | SOURCE |
| `create_physics_asset` / `list_physics_bodies` / `add_physics_body` / `configure_physics_body` | Physics | SOURCE |
| `add_physics_constraint` / `configure_constraint_limits` | Constraints | SOURCE |
| `rename_bone` / `set_bone_transform` | Bone editing | SOURCE |
| `create_morph_target` / `set_morph_target_deltas` / `import_morph_targets` | Morph targets | SOURCE |
| `bind_cloth_to_skeletal_mesh` / `assign_cloth_asset_to_mesh` | Cloth | SOURCE |
| `create_skeleton` / `add_bone` / `remove_bone` / `set_bone_parent` | Skeleton creation | SOURCE |
| `set_vertex_weights` / `auto_skin_weights` / `copy_weights` / `mirror_weights` | Skinning | SOURCE |

---

### 20. `manage_material_authoring` -- Material Creation & Editing
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (handler responds, needs assetPath)
- **Requires:** `assetPath` for existing materials

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `create_material` | Create new material | SOURCE |
| `set_blend_mode` / `set_shading_model` / `set_material_domain` | Material properties | SOURCE |
| `add_texture_sample` / `add_texture_coordinate` | Texture nodes | SOURCE |
| `add_scalar_parameter` / `add_vector_parameter` / `add_static_switch_parameter` | Parameters | SOURCE |
| `add_math_node` / `add_world_position` / `add_vertex_normal` / `add_pixel_depth` / `add_fresnel` / `add_reflection_vector` | Expression nodes | SOURCE |
| `add_panner` / `add_rotator` / `add_noise` / `add_voronoi` | UV/procedural | SOURCE |
| `add_if` / `add_switch` / `add_custom_expression` | Logic | SOURCE |
| `connect_nodes` / `disconnect_nodes` | Wire connections | SOURCE |
| `create_material_function` / `add_function_input` / `add_function_output` / `use_material_function` | Material functions | SOURCE |
| `create_material_instance` / `set_scalar_parameter_value` / `set_vector_parameter_value` / `set_texture_parameter_value` | Instances | SOURCE |
| `create_landscape_material` / `create_decal_material` / `create_post_process_material` | Specialized | SOURCE |
| `add_landscape_layer` / `configure_layer_blend` | Landscape layers | SOURCE |
| `compile_material` | Compile | SOURCE |
| `get_material_info` | Query | SOURCE |

---

### 21. `manage_texture` -- Texture Creation & Processing
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (handler responds, needs assetPath)
- **Requires:** `assetPath`

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `create_noise_texture` / `create_gradient_texture` / `create_pattern_texture` | Procedural textures | SOURCE |
| `create_normal_from_height` / `create_ao_from_mesh` | Generate maps | SOURCE |
| `set_compression_settings` / `set_texture_group` / `set_lod_bias` | Settings | SOURCE |
| `configure_virtual_texture` / `set_streaming_priority` | Streaming | SOURCE |
| `resize_texture` / `invert` / `desaturate` / `adjust_levels` / `blur` / `sharpen` | Processing | SOURCE |
| `channel_pack` / `combine_textures` / `channel_extract` / `adjust_curves` | Channel ops | SOURCE |
| `get_texture_info` | Query | SOURCE |

---

### 22. `manage_gas` -- Gameplay Ability System
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (handler responds, needs assetPath)
- **Requires:** `assetPath` (Blueprint with GAS component)

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `add_ability_system_component` / `configure_asc` | ASC setup | SOURCE |
| `create_attribute_set` / `add_attribute` / `set_attribute_base_value` / `set_attribute_clamping` | Attributes | SOURCE |
| `create_gameplay_ability` / `set_ability_tags` / `set_ability_costs` / `set_ability_cooldown` / `set_ability_targeting` / `add_ability_task` / `set_activation_policy` / `set_instancing_policy` | Abilities | SOURCE |
| `create_gameplay_effect` / `set_effect_duration` / `add_effect_modifier` / `set_modifier_magnitude` / `add_effect_execution_calculation` / `add_effect_cue` / `set_effect_stacking` / `set_effect_tags` | Effects | SOURCE |
| `create_gameplay_cue_notify` / `configure_cue_trigger` / `set_cue_effects` | Gameplay Cues | SOURCE |
| `add_tag_to_asset` | Tags | SOURCE |
| `get_gas_info` | Query | SOURCE |

---

### 23. `manage_character` -- Character Creation & Locomotion
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (handler responds, needs blueprintPath)
- **Requires:** `blueprintPath`

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `create_character_blueprint` | Create character BP | SOURCE |
| `configure_capsule_component` / `configure_mesh_component` / `configure_camera_component` | Components | SOURCE |
| `configure_movement_speeds` / `configure_jump` / `configure_rotation` | Movement | SOURCE |
| `add_custom_movement_mode` / `configure_nav_movement` | Custom movement | SOURCE |
| `setup_mantling` / `setup_vaulting` / `setup_climbing` / `setup_sliding` / `setup_wall_running` / `setup_grappling` | Advanced locomotion | SOURCE |
| `setup_footstep_system` / `map_surface_to_sound` / `configure_footstep_fx` | Footsteps | SOURCE |
| `get_character_info` | Query | SOURCE |

---

### 24. `manage_combat` -- Weapons, Projectiles, Damage, Melee
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (handler responds, needs blueprintPath)
- **Requires:** `blueprintPath`

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `create_weapon_blueprint` / `configure_weapon_mesh` / `configure_weapon_sockets` / `set_weapon_stats` | Weapons | SOURCE |
| `configure_hitscan` / `configure_projectile` / `configure_spread_pattern` / `configure_recoil_pattern` / `configure_aim_down_sights` | Firing | SOURCE |
| `create_projectile_blueprint` / `configure_projectile_movement` / `configure_projectile_collision` / `configure_projectile_homing` | Projectiles | SOURCE |
| `create_damage_type` / `configure_damage_execution` / `setup_hitbox_component` | Damage | SOURCE |
| `setup_reload_system` / `setup_ammo_system` / `setup_attachment_system` / `setup_weapon_switching` | Systems | SOURCE |
| `configure_muzzle_flash` / `configure_tracer` / `configure_impact_effects` / `configure_shell_ejection` | VFX | SOURCE |
| `create_melee_trace` / `configure_combo_system` / `create_hit_pause` / `configure_hit_reaction` / `setup_parry_block_system` / `configure_weapon_trails` | Melee | SOURCE |
| `get_combat_info` | Query | SOURCE |

---

### 25. `manage_ai` -- AI Controllers, BT, EQS, Perception, State Trees
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (`get_ai_info` confirmed working)

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `create_ai_controller` / `assign_behavior_tree` / `assign_blackboard` | AI Controller | SOURCE |
| `create_blackboard_asset` / `add_blackboard_key` / `set_key_instance_synced` | Blackboard | SOURCE |
| `create_behavior_tree` / `add_composite_node` / `add_task_node` / `add_decorator` / `add_service` / `configure_bt_node` | Behavior Tree | SOURCE |
| `create_eqs_query` / `add_eqs_generator` / `add_eqs_context` / `add_eqs_test` / `configure_test_scoring` | EQS | SOURCE |
| `add_ai_perception_component` / `configure_sight_config` / `configure_hearing_config` / `configure_damage_sense_config` / `set_perception_team` | Perception | SOURCE |
| `create_state_tree` / `add_state_tree_state` / `add_state_tree_transition` / `configure_state_tree_task` | State Trees | SOURCE |
| `create_smart_object_definition` / `add_smart_object_slot` / `configure_slot_behavior` / `add_smart_object_component` | Smart Objects | SOURCE |
| `create_mass_entity_config` / `configure_mass_entity` / `add_mass_spawner` | Mass AI | SOURCE |
| `get_ai_info` | Query | TESTED |

---

### 26. `manage_inventory` -- Items, Equipment, Loot, Crafting
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (`get_inventory_info` confirmed working)

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `create_item_data_asset` / `set_item_properties` / `create_item_category` / `assign_item_category` | Items | SOURCE |
| `create_inventory_component` / `configure_inventory_slots` / `add_inventory_functions` / `configure_inventory_events` / `set_inventory_replication` | Inventory | SOURCE |
| `create_pickup_actor` / `configure_pickup_interaction` / `configure_pickup_respawn` / `configure_pickup_effects` | Pickups | SOURCE |
| `create_equipment_component` / `define_equipment_slots` / `configure_equipment_effects` / `add_equipment_functions` / `configure_equipment_visuals` | Equipment | SOURCE |
| `create_loot_table` / `add_loot_entry` / `configure_loot_drop` / `set_loot_quality_tiers` | Loot | SOURCE |
| `create_crafting_recipe` / `configure_recipe_requirements` / `create_crafting_station` / `add_crafting_component` | Crafting | SOURCE |
| `get_inventory_info` | Query | TESTED |

---

### 27. `manage_interaction` -- Interactables, Doors, Triggers
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (`get_interaction_info` confirmed working)

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `create_interaction_component` / `configure_interaction_trace` / `configure_interaction_widget` / `add_interaction_events` | Interaction system | SOURCE |
| `create_interactable_interface` | Interface | SOURCE |
| `create_door_actor` / `configure_door_properties` | Doors | SOURCE |
| `create_switch_actor` / `configure_switch_properties` | Switches | SOURCE |
| `create_chest_actor` / `configure_chest_properties` | Chests | SOURCE |
| `create_lever_actor` | Levers | SOURCE |
| `setup_destructible_mesh` / `add_destruction_component` | Destructibles | SOURCE |
| `create_trigger_actor` / `configure_trigger_events` | Triggers | SOURCE |
| `get_interaction_info` | Query | TESTED |

---

### 28. `manage_widget_authoring` -- UMG Widget Creation
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (handler responds, needs widgetPath)
- **Requires:** `widgetPath`

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `create_widget_blueprint` / `set_widget_parent_class` | Creation | SOURCE |
| **Layout:** `add_canvas_panel`, `add_horizontal_box`, `add_vertical_box`, `add_overlay`, `add_grid_panel`, `add_wrap_box`, `add_scroll_box`, `add_size_box`, `add_border` | Layout containers | SOURCE |
| **Widgets:** `add_text_block`, `add_rich_text_block`, `add_image`, `add_button`, `add_progress_bar`, `add_slider`, `add_check_box`, `add_text_input`, `add_combo_box`, `add_spin_box`, `add_list_view`, `add_tree_view` | Widget elements | SOURCE |
| **Styling:** `set_anchor`, `set_alignment`, `set_position`, `set_size`, `set_padding`, `set_z_order`, `set_render_transform`, `set_visibility`, `set_style` | Positioning/styling | SOURCE |
| **Bindings:** `bind_text`, `bind_visibility`, `bind_color`, `bind_enabled`, `bind_on_clicked`, `bind_on_hovered`, `bind_on_value_changed`, `create_property_binding` | Data bindings | SOURCE |
| **Animation:** `create_widget_animation`, `add_animation_track`, `add_animation_keyframe`, `set_animation_loop` | Widget animation | SOURCE |
| **Templates:** `create_main_menu`, `create_pause_menu`, `create_hud_widget`, `create_settings_menu`, `create_loading_screen` | Premade templates | SOURCE |
| **HUD:** `add_health_bar`, `add_crosshair`, `add_ammo_counter`, `add_minimap`, `add_compass`, `add_interaction_prompt`, `add_objective_tracker`, `add_damage_indicator` | HUD elements | SOURCE |
| **Game UI:** `create_inventory_ui`, `create_dialog_widget`, `create_radial_menu` | Game UI | SOURCE |
| `preview_widget` / `get_widget_info` | Preview/Query | SOURCE |

---

### 29. `manage_networking` -- Replication, RPCs, Prediction
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (handler responds, needs blueprintPath/actorName)
- **Requires:** `blueprintPath` or `actorName`

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `set_property_replicated` / `set_replication_condition` / `configure_net_update_frequency` / `configure_net_priority` / `set_net_dormancy` / `configure_replication_graph` | Replication | SOURCE |
| `create_rpc_function` / `configure_rpc_validation` / `set_rpc_reliability` | RPCs | SOURCE |
| `set_owner` / `set_autonomous_proxy` / `check_has_authority` / `check_is_locally_controlled` | Authority | SOURCE |
| `configure_net_cull_distance` / `set_always_relevant` / `set_only_relevant_to_owner` | Relevancy | SOURCE |
| `configure_net_serialization` / `set_replicated_using` / `configure_push_model` | Serialization | SOURCE |
| `configure_client_prediction` / `configure_server_correction` / `add_network_prediction_data` / `configure_movement_prediction` | Prediction | SOURCE |
| `configure_net_driver` / `set_net_role` / `configure_replicated_movement` | Advanced | SOURCE |
| `get_networking_info` | Query | SOURCE |

---

### 30. `manage_game_framework` -- Game Mode, State, Controllers
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (`get_game_framework_info` confirmed working)

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `create_game_mode` / `create_game_state` / `create_player_controller` / `create_player_state` / `create_game_instance` / `create_hud_class` | Framework classes | SOURCE |
| `set_default_pawn_class` / `set_player_controller_class` / `set_game_state_class` / `set_player_state_class` | Assignments | SOURCE |
| `configure_game_rules` / `setup_match_states` / `configure_round_system` / `configure_team_system` / `configure_scoring_system` | Game rules | SOURCE |
| `configure_spawn_system` / `configure_player_start` / `set_respawn_rules` / `configure_spectating` | Spawning | SOURCE |
| `get_game_framework_info` | Query | TESTED |

---

### 31. `manage_sessions` -- Sessions, Split-screen, LAN, Voice
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (PARTIAL)
- **NOTE:** This handler is also the CATCH-ALL. Unrecognized actions from other handlers produce errors here.

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `configure_local_session_settings` / `configure_session_interface` | Session config | SOURCE |
| `configure_split_screen` / `set_split_screen_type` / `add_local_player` / `remove_local_player` | Split screen | SOURCE |
| `configure_lan_play` / `host_lan_server` / `join_lan_server` | LAN | SOURCE |
| `enable_voice_chat` / `configure_voice_settings` / `set_voice_channel` / `mute_player` / `set_voice_attenuation` / `configure_push_to_talk` | Voice chat | SOURCE |
| `get_sessions_info` | Query | SOURCE |

---

### 32. `manage_level_structure` -- Sublevels, World Partition, Data Layers
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (`get_level_structure_info` confirmed working)

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `get_level_structure_info` | Get full level structure info | TESTED |
| `create_level` / `create_sublevel` | Level creation | SOURCE |
| `configure_level_streaming` / `set_streaming_distance` / `configure_level_bounds` | Streaming | SOURCE |
| `enable_world_partition` / `configure_grid_size` | World Partition | SOURCE |
| `create_data_layer` / `assign_actor_to_data_layer` | Data Layers | SOURCE |
| `configure_hlod_layer` | HLOD | SOURCE |
| `create_minimap_volume` | Minimap | SOURCE |
| `open_level_blueprint` / `add_level_blueprint_node` / `connect_level_blueprint_nodes` | Level BP | SOURCE |
| `create_level_instance` / `create_packed_level_actor` | Level instances | SOURCE |

---

### 33. `manage_volumes` -- Trigger, Blocking, Physics, Audio Volumes
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (`get_volumes_info` confirmed working)

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `create_trigger_volume` / `create_trigger_box` / `create_trigger_sphere` / `create_trigger_capsule` | Triggers | SOURCE |
| `create_blocking_volume` / `create_kill_z_volume` / `create_pain_causing_volume` | Gameplay | SOURCE |
| `create_physics_volume` / `create_audio_volume` / `create_reverb_volume` | Physics/Audio | SOURCE |
| `create_cull_distance_volume` / `create_precomputed_visibility_volume` | Rendering | SOURCE |
| `create_lightmass_importance_volume` | Lightmass | SOURCE |
| `create_nav_mesh_bounds_volume` / `create_nav_modifier_volume` | Navigation | SOURCE |
| `create_camera_blocking_volume` | Camera | SOURCE |
| `set_volume_extent` / `set_volume_properties` | Properties | SOURCE |
| `get_volumes_info` | Query | TESTED |

---

### 34. `manage_navigation` -- NavMesh, Nav Links, Pathfinding
- **Pattern:** B (`subAction` field)
- **Test status:** TESTED (`get_navigation_info` confirmed working)

| subAction | Description | Tested? |
|-----------|-------------|---------|
| `configure_nav_mesh_settings` / `set_nav_agent_properties` / `rebuild_navigation` | NavMesh | SOURCE |
| `create_nav_modifier_component` / `set_nav_area_class` / `configure_nav_area_cost` | Nav areas | SOURCE |
| `create_nav_link_proxy` / `configure_nav_link` / `set_nav_link_type` | Nav links | SOURCE |
| `create_smart_link` / `configure_smart_link_behavior` | Smart links | SOURCE |
| `get_navigation_info` | Query | TESTED |

---

## Additional Registered Handlers

These are callable via Pattern C (direct action name) or their specific pattern:

### `manage_render` -- Render Targets
- **Pattern:** B (`subAction`)
- **Test status:** TESTED (`create_render_target` confirmed working)
- Sub-actions: `create_render_target`, `attach_render_target_to_volume`, `nanite_rebuild_mesh`, `lumen_update_scene`

### `manage_splines` -- Spline Actors & Mesh Scattering
- **Pattern:** B (`subAction`)
- Sub-actions: `create_spline_actor`, `add_spline_point`, `remove_spline_point`, `set_spline_point_position`, `set_spline_point_tangents`, `set_spline_point_rotation`, `set_spline_point_scale`, `set_spline_type`, `create_spline_mesh_component`, `set_spline_mesh_asset`, `configure_spline_mesh_axis`, `set_spline_mesh_material`, `scatter_meshes_along_spline`, `configure_mesh_spacing`, `configure_mesh_randomization`, `create_road_spline`, `create_river_spline`, `create_fence_spline`, `create_wall_spline`, `create_cable_spline`, `create_pipe_spline`, `get_splines_info`

### Property & Data Handlers (Pattern C, direct names)
- `execute_editor_function`, `set_object_property`, `get_object_property`
- Array: `array_append`, `array_remove`, `array_insert`, `array_get_element`, `array_set_element`, `array_clear`
- Map: `map_set_value`, `map_get_value`, `map_remove_key`, `map_has_key`, `map_get_keys`, `map_clear`
- Set: `set_add`, `set_remove`, `set_contains`, `set_clear`

### Asset Workflow (Pattern C, direct names)
- `get_asset_references`, `get_asset_dependencies`, `fixup_redirectors`, `source_control_checkout`, `source_control_submit`, `bulk_rename_assets`, `bulk_delete_assets`, `generate_thumbnail`

### Landscape & Foliage (Pattern C, direct names)
- `create_landscape`, `create_procedural_terrain`, `create_landscape_grass_type`, `sculpt_landscape`, `set_landscape_material`, `edit_landscape`, `add_foliage_type`, `create_procedural_foliage`, `paint_foliage`, `add_foliage_instances`, `remove_foliage`, `get_foliage_instances`

### Niagara VFX (Pattern C, direct names)
- `create_niagara_system`, `create_niagara_ribbon`, `create_niagara_emitter`, `spawn_niagara_actor`, `modify_niagara_parameter`

### Animation (Pattern C, direct names)
- `create_anim_blueprint` (TESTED), `play_anim_montage`, `setup_ragdoll`

### Material (Pattern C, direct names)
- `add_material_texture_sample`, `add_material_expression`, `create_material_nodes`, `rebuild_material`

### Sequencer (Pattern C, direct names)
- `add_sequencer_keyframe`, `manage_sequencer_track`, `add_camera_track`, `add_animation_track`, `add_transform_track`

### Utility Handlers (Pattern B, `subAction`)
- `manage_pipeline` (subAction: `run_ubt`)
- `manage_tests` (subAction: `run_tests`)
- `manage_logs` (subAction: `subscribe`, `unsubscribe`)
- `manage_debug` (subAction: `spawn_category`)
- `manage_insights` (subAction: `start_session`)
- `manage_world_partition` -- World Partition management
- `manage_ui` -- UI/UMG operations
- `control_environment` -- Environment settings

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| `"Unknown manage_sessions action"` | Wrong action name or wrong sub-action field (`action` vs `subAction`) | Check the pattern (A/B/C) for that tool |
| Validation error | Handler works but needs a required field | Add `actorName`, `assetPath`, `blueprintPath`, etc. |
| Connection refused | Unreal Editor not running or plugin not loaded | Start UE Editor with McpAutomationBridge |
| MCP tools not visible in Claude Code | `.mcp.json` not loaded | Restart Claude Code after creating/modifying `.mcp.json` |
| Performance/audio/effect calls fail | Used umbrella handler | NEVER use umbrella handler, always use Pattern C direct names |
| `control_actor.list` fails | Editor is in PIE mode | Stop PIE first or use resources for runtime info |
| Console command errors | Tried chaining commands | Single commands only -- no `&&`, `||`, `;`, pipes, or backticks |
| Script passes but MCP shows disconnected | Expected on fresh start (lazy connection) | Run a tool call first (Step 2 below), then re-check |
| MCP resource shows port 8090 | `.mcp.json` missing port env vars | Ensure `MCP_AUTOMATION_CLIENT_PORT=8091` |

---

## Connection Health Check

At the start of each conversation, run the following to verify everything works.

### Step 1: Run the health check script

```bash
node "C:\Users\baris\Documents\Unreal Projects\he_grenade_game\check.mjs"
```

This performs 4 non-destructive (read-only) checks:
1. **TCP port 8091 reachable** -- is the Unreal Editor running with the bridge plugin?
2. **WebSocket handshake** -- does bridge_hello / bridge_ack succeed?
3. **get_engine_version** -- can we read data from the engine? (Pattern A)
4. **list_light_types** -- does a Pattern C direct action work?

None of these create, modify, or delete anything in the editor.

Expected output when healthy:
```
=== MCP Automation Bridge Health Check ===
  [PASS] TCP port reachable -- 127.0.0.1:8091
  [PASS] WebSocket handshake (bridge_hello/ack) -- engine=... session=...
  [PASS] Read-only action: get_engine_version -- 5.7.2-...
  [PASS] Read-only action: list_light_types -- 5 light types returned
Result: 4/4 checks passed -- ALL OK
```

### Step 2: Wake up Claude Code's MCP connection

**Expected behavior:** On a fresh session start, the MCP server uses **lazy connection** -- it
does not connect to the UE bridge until the first tool call is made. This means the first
`ue://health` resource check will show `status: "disconnected"`. **This is normal and not an
error.** Run a tool call first to wake up the connection:

```
ToolSearch(query="select:mcp__unreal-engine__system_control")
mcp__unreal-engine__system_control({ "action": "console_command", "command": "stat none" })
```

### Step 3: Verify MCP resource shows connected

After the tool call in Step 2, check health:

```
ReadMcpResourceTool(server="unreal-engine", uri="ue://health")
```

Confirm `status` is `"connected"` and `unrealConnection.status` is `"connected"`.

### Report

If all 3 steps pass, report:
> MCP bridge health check passed. Connection confirmed on ws://127.0.0.1:8091. Ready to work.

### Health Check Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| TCP port unreachable | Unreal Editor not running / plugin not loaded | Start UE Editor with McpAutomationBridge |
| Handshake timeout | Plugin loaded but bridge not initialized | Check DefaultGame.ini bAlwaysListen=True |
| Action error | Editor in PIE mode or bridge busy | Stop PIE, retry |
| Claude MCP tools missing | .mcp.json not loaded | Restart Claude Code |
| Script passes but MCP shows disconnected | Expected on fresh start (lazy connection) | Run a tool call first (Step 2), then re-check |
| MCP resource shows port 8090 | .mcp.json missing port env vars | Ensure MCP_AUTOMATION_CLIENT_PORT=8091 |
