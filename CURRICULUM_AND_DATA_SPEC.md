# World-Model Curriculum and Dataset Specification

## Purpose

This project is a curriculum-oriented, action-conditioned visual simulation for
training world models. The model receives observations and an externally supplied
action vector. It is not responsible for choosing actions. Its job is to predict
the observation consequences of the supplied actions over one or more future
steps.

The intended transition model is:

```text
p(observation[t+1:t+k] | observation[<=t], action[t:t+k-1])
```

This document is the source of truth for the curriculum, controls, map contract,
dataset schema, and data-generation plan.

## Project lineage and recovery

- Complete source game folder:
  `C:\Users\baris\Documents\Unreal Projects\he_grenade_game`
- Curriculum project folder:
  `C:\Users\baris\Documents\Unreal Projects\he_grenade_game_curriculum`
- Complete-game baseline commit: `73525ac`
- Complete-game baseline tag in this repository:
  `curriculum-complete-game-baseline`
- Active first-iteration branch:
  `codex/curriculum-movement-v1`
- Unreal Engine version: 5.8

The source game is not modified by curriculum work. The baseline tag and Git
history retain the complete grenade, trajectory, breakable, and procedural-arena
logic so later curriculum iterations can selectively restore already-defined
systems.

Generated `Binaries`, `Intermediate`, `Saved`, and derived-data caches are not part
of the authored-project copy. Unreal regenerates them for this project.

## Invariants across all curriculum versions

### Observation and control

- First-person RGB observation.
- One centered crosshair.
- No mouse input.
- No jump or crouch actions.
- No gamepad or touch controls in the curriculum interface.
- Camera yaw and pitch are controlled only with arrow keys.
- Movement is controlled only with W, A, S, and D.
- Any action bits may be held and combined.
- Opposing inputs cancel deterministically:
  - W + S
  - A + D
  - Left Arrow + Right Arrow
  - Up Arrow + Down Arrow
- Camera pitch is clamped; yaw wraps normally.
- Action effects are rates per simulated second, not amounts per rendered frame.

### Canonical ten-bit action vector

The schema remains constant from the first curriculum version:

```text
[W, A, S, D, ArrowUp, ArrowDown, ArrowLeft, ArrowRight, Q, E]
```

Unavailable actions are forced to zero in earlier curriculum versions. They must
not be sampled as no-op actions before they are introduced, because doing so would
create conflicting transition examples across curriculum stages.

### Environment

- One fixed square map during Curriculum V1-V3.
- Fixed arena dimensions, object transforms, lighting, exposure, camera FOV, and
  rendering settings.
- Flat, indestructible floor.
- Indestructible perimeter walls.
- No glass or breakable tiles.
- No floor collapse.
- No procedural geometry changes.
- One representative of each learning-object type:
  - rectangle
  - triangle
  - sphere
  - hoop
  - ramp
- Objects are placed in a geometrically balanced layout while their distinct types
  provide orientation landmarks.
- Episode resets may randomize the valid player spawn transform during data
  generation, but do not change map geometry.

## Curriculum

### Curriculum V1: Movement

Status: current playable iteration.

Enabled actions:

- W
- A
- S
- D
- Arrow Up
- Arrow Down
- Arrow Left
- Arrow Right

Forced to zero:

- Q
- E

Visual and gameplay contract:

- One neutral white crosshair.
- Each learning object uses a distinct opaque, non-emissive, high-roughness matte
  color with no transparency or surface-grid texture.
- No trajectory visualization.
- No grenade actors.
- No grenade state, cooldown, explosion, damage, or break mechanics.
- No HUD speed, hop, timer, inventory, or status text.
- The fixed square curriculum arena is the only gameplay layout.

Training coverage should include:

- idle periods
- camera-only input
- movement-only input
- combined movement and camera input
- diagonal movement
- diagonal camera input
- short taps and long holds
- opposing input cancellation
- walking into obstacles
- sliding along walls and obstacle boundaries
- turning while moving

### Curriculum V2: Trajectory preview

Begins only after V1 is accepted and learned.

V1 behavior remains unchanged. Q is introduced as a level-triggered action:

- Q = 1: display exactly one green trajectory.
- Q = 0: display no trajectory.
- The trajectory is recomputed every observation step from the current player and
  camera state.
- Q may be held while any movement and camera actions are active.
- E remains forced to zero.
- No physical grenade is spawned.
- The trajectory runs until the simulated grenade reaches rest.
- There is no red trajectory, explosion marker, fuse, charge, or damage state.

The trajectory should be sufficiently thick and high-contrast at training
resolution. Dataset sampling should keep Q visible for roughly 40-50 percent of
V2 frames so the line is not a sparse target under whole-frame loss.

### Curriculum V3: Persistent harmless grenades

V1 and V2 behavior remains unchanged. E is introduced:

- E is edge-triggered.
- A transition from E = 0 to E = 1 requests one throw.
- Holding E does not repeatedly throw.
- E must be released before another throw can be requested.
- An E press during cooldown is ignored and is not buffered.
- An accepted throw uses exactly the same launch position and velocity as the Q
  trajectory for that state.
- If Q and E are active together, preview and launch use the same state snapshot.

Cooldown:

- The centered crosshair is green when a throw is available.
- An accepted throw changes the crosshair to red.
- Cooldown lasts exactly 2.0 seconds of simulated time.
- At the canonical 20 Hz observation rate, cooldown lasts exactly 40 transitions.
- The crosshair returns to green on the deterministic final cooldown transition.
- Q remains available during cooldown and its line remains green.

Grenade behavior:

- No inventory and no arbitrary grenade limit.
- The two-second cooldown naturally limits creation rate.
- Grenades never explode, damage, or break anything.
- Grenades bounce and roll until resting.
- A resting grenade remains visible at its final transform until episode reset.
- Resting grenades do not collide with the player or later grenades in V3.
- Existing grenades do not change the Q preview.
- Simulation ticking stops once a grenade is resting.
- All persistent grenades are removed at episode reset.

### Curriculum V4: Map generalization

Begins only after movement, trajectory preview, and persistent projectile execution
are learned on the fixed map.

Variation is introduced one factor at a time:

1. Rearrange known objects inside the same square arena.
2. Generate many layouts from recorded deterministic seeds.
3. Vary square-arena dimensions.
4. Vary quantities and combinations of known object types.
5. Introduce new object types, materials, or lighting only later.

One generated map remains fixed for an entire episode. Training and evaluation use
disjoint seed sets. Earlier curriculum data remains in the training mixture to
reduce catastrophic forgetting.

Possible later stages, not part of V1-V4:

- grenade-to-grenade collision
- player-to-resting-grenade collision
- break mechanics
- variable observation timestep
- new physics parameters

## Simulation and observation timing

Three rates are intentionally distinct:

| Layer | Initial specification | Meaning |
| --- | ---: | --- |
| Grenade physics | 120 Hz fixed step | Six projectile substeps per observation |
| Actions and RGB observations | 20 Hz fixed step | One dataset transition every 50 ms |
| Wall-clock generation | Uncapped | Run as fast as the worker permits |

Fast generation must not use world time dilation. A worker advances fixed simulated
time without waiting for real time. If writing or encoding cannot keep up, the
simulation waits rather than dropping observations.

The initial rendering specification is:

- 160 x 96 RGB
- fixed FOV
- no audio
- no VSync
- no motion blur
- no depth of field
- no film grain
- no automatic exposure
- no temporal camera effects
- no ray tracing
- simplified deterministic lighting

A visual pilot must confirm that the crosshair and trajectory remain at least two
training pixels thick. Resolution may be increased before large-scale collection
if that condition is not met.

## Episode contract

- Initial duration: 60 simulated seconds.
- At 20 Hz: 1,200 transitions and 1,201 observations.
- Reset removes all persistent episode actors and restores the fixed map state.
- Reset is an explicit episode boundary.
- Loss is never computed across an episode boundary.
- Data-generation resets may choose a deterministic valid spawn position and
  orientation from the episode seed.

Exact alignment:

```text
observation[0] = initial observation
action[0]      = action transforming observation[0] into observation[1]
observation[1] = resulting observation
...
action[N-1]    = action transforming observation[N-1] into observation[N]
```

## Random-agent policy

Random inputs must be temporally persistent. Sampling every bit independently with
probability 0.5 every frame would overproduce jitter and cancellation.

Initial duration mixture:

- short taps: 1-3 observation steps
- normal holds: 4-20 observation steps
- long holds: 21-60 observation steps

Sampling should balance:

- movement only
- camera only
- movement plus camera
- idle
- valid diagonals
- deliberate opposing-input cases in approximately 5-10 percent of segments

V2 additions:

- varied Q hold durations
- Q during walking
- Q during camera rotation
- Q while walking and rotating
- Q active for roughly 40-50 percent of frames

V3 additions:

- accepted E presses while green
- ignored E presses while red
- complete red-to-green cooldown sequences
- throws with Q held
- throws without Q held
- high- and low-density persistent-grenade episodes

## Dataset schema

The build records privileged state for verification even when the model receives
only RGB observations and action bits.

Per-episode fields:

- dataset schema version
- curriculum version
- episode ID
- worker ID
- random seed
- build revision / Git commit
- Unreal Engine version
- map identifier and map/configuration hash
- observation rate
- internal physics rate
- image width and height
- camera FOV
- initial player transform
- requested episode length
- actual episode length
- termination/reset reason
- data checksum information

Per-observation fields:

- episode ID
- frame index
- integer simulated timestamp or simulation-step count
- RGB observation
- player position XYZ
- player velocity XYZ
- camera yaw, pitch, and roll
- grounded state
- collision/contact diagnostics
- crosshair state:
  - Neutral
  - Ready
  - Cooldown
- cooldown remaining in observation steps
- Q visibility
- all grenade states, with stable per-episode IDs:
  - grenade ID
  - position XYZ
  - velocity XYZ
  - resting flag

Per-transition fields:

- episode ID
- source frame index
- ten-bit requested action mask
- ten individual action-bit columns
- effective/cancelled movement and camera axes
- E request edge
- E accepted-throw event
- resulting cooldown state
- observation-valid flag

Curriculum V1 retains the complete schema:

- Q and E bits are zero.
- Q visibility is false.
- grenade collection is empty.
- crosshair state is Neutral.
- cooldown remaining is zero.

## Packaged data-generation build

The Unreal Editor is used to author, inspect, build, cook, and package the project.
Bulk workers run the packaged executable without the editor.

Each worker will eventually:

1. Reset an episode using a deterministic seed.
2. Obtain a deterministic random action.
3. Apply action bits directly rather than synthesizing operating-system keystrokes.
4. Advance one fixed 50 ms observation interval.
5. Render the final observation, including the crosshair and later trajectory.
6. Associate asynchronous GPU readback with the exact frame index.
7. Write the RGB observation and privileged metadata.
8. Block instead of dropping frames when output queues are full.

Recommended worker parameters:

- curriculum version
- worker ID
- seed start/range
- episode count/range
- output directory
- observation rate
- render resolution

The build is packaged once and launched in parallel with disjoint worker and seed
ranges. Each process writes independent shards. The optimal number of processes
per GPU is established by benchmarking rather than assumed.

Proposed artifact structure:

```text
episode_manifest.json
observations.mkv
steps.parquet
```

- `observations.mkv`: constant-rate lossless or visually lossless RGB observations.
- `steps.parquet`: indexed action and privileged-state records.
- `episode_manifest.json`: build, seed, configuration, length, and checksum data.

Ordinary MP4 files may be generated for human review but are not the sole source
of training data.

## Data-volume rollout

Collection scales only after correctness checks:

1. 10,000-frame pipeline and alignment test.
2. 100,000-frame visual and action-distribution pilot.
3. 1,000,000-frame first training run.
4. Scale toward 10,000,000 frames only after validation rollouts justify it.

At 20 observations per second, one million frames represent approximately
13.9 simulated hours.

## Evaluation gates

V1:

- multi-step player position error
- yaw and pitch error
- velocity error
- collision/contact accuracy
- long-rollout visual drift

V2:

- trajectory-mask overlap
- endpoint error
- geometric distance between predicted and reference curves
- correct appearance/disappearance timing for Q

V3:

- accepted versus ignored E behavior
- exact red-to-green cooldown transition
- projectile position and velocity error
- agreement between Q preview and thrown path
- resting-location error
- persistence of resting grenades

Progression requires multi-step rollout quality, not only low one-frame pixel loss.

## Current implementation boundary

The current working version is Curriculum V1 only. The packaged data recorder,
random-agent worker, and high-throughput dataset writer are intentionally deferred
until the human-played Unreal Editor version has been inspected and accepted.
