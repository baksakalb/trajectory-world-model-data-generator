# Trajectory World Model Data Generator

## Purpose

This Unreal Engine project is a curriculum-based, action-conditioned visual
simulation for training world models. The model does not choose actions. It
receives observations and externally supplied actions and learns their visual and
physical consequences:

```text
p(observation[t+1:t+k] | observation[<=t], action[t:t+k-1])
```

The initial model input is planned to contain only RGB history and the canonical
ten action bits. Privileged state is recorded for dataset balancing, validation,
debugging, and evaluation, but is not automatically exposed to the model.

This document is the source of truth for the current environment, curriculum,
controls, build workflow, data contract, collection policy, and remaining work.

## Repository and project lineage

- GitHub repository:
  `https://github.com/baksakalb/trajectory-world-model-data-generator`
- GitHub default branch: `main`
- Initial curriculum commit: `340d6c5`
- Unreal Engine version: 5.8
- Original complete game:
  `C:\Users\baris\Documents\Unreal Projects\he_grenade_game`
- Complete-game baseline commit: `73525ac`
- Complete-game baseline tag:
  `curriculum-complete-game-baseline`
- Current curriculum project folder after the planned move:
  `C:\Users\baris\Desktop\he_grenade_game_curriculum`

The original game is not modified by curriculum work. Its full grenade,
trajectory, breakable, and procedural-arena systems remain available through Git
history and the baseline tag for later curriculum stages.

Generated `Binaries`, `Intermediate`, `Saved`, and derived-data caches are not
authored source. Unreal regenerates them locally.

## Current status

Curriculum V1 is implemented as a human-inspectable game and has passed:

- Unreal Editor Development target compilation
- non-Editor Win64 Development target compilation
- unattended runtime startup validation
- fixed arena construction validation
- deterministic lighting construction validation
- curriculum material import validation
- pyramid mesh import validation

Not yet implemented:

- autonomous collection agent
- fixed-step frame recorder
- GPU RGB readback
- Parquet metadata writer
- WebDataset-style shard writer
- packaged data-generator executable
- parallel worker launcher
- dataset validation/reporting suite

The next engineering milestone is the real packaged data-generation pipeline, not
a throwaway screen-recording prototype.

## Canonical action interface

The action schema remains stable across every curriculum stage:

```text
[W, A, S, D, ArrowUp, ArrowDown, ArrowLeft, ArrowRight, Q, E]
```

Rules:

- Every action is represented by one Boolean bit.
- Multiple bits may be active simultaneously.
- Actions may be held for any number of observation steps.
- W/A/S/D control movement.
- Arrow keys control camera pitch and yaw.
- No mouse, gamepad, touch, jump, or crouch input is part of the curriculum.
- Opposing pairs cancel deterministically:
  - W + S
  - A + D
  - ArrowLeft + ArrowRight
  - ArrowUp + ArrowDown
- Camera pitch is clamped.
- Camera yaw wraps normally.
- Q and E remain present but are forced to zero before their curriculum stage.
- Action effects are rates per simulated second, not per rendered frame.

## Fixed Curriculum V1 environment

### Arena

- One deterministic square arena.
- Side length: 3,200 cm.
- One continuous indestructible floor.
- Four indestructible perimeter walls.
- No procedural map changes.
- No glass, breakable tiles, collapse, damage, or explosions.
- Map geometry does not change within or between V1 episodes.

### Learning objects

The arena contains exactly one of each:

- blue rectangular prism
- orange solid square-based pyramid
- green sphere
- purple hoop
- red ramp

The objects form a geometrically balanced layout while their different shapes and
colors provide orientation landmarks.

The pyramid is one genuine closed static mesh with:

- one square base
- four triangular faces
- explicit per-face normals
- smoothing disabled across hard edges
- authored UV coordinates
- complex-as-simple solid collision

It is not constructed from boxes or other composite pieces.

The ramp is 500 × 260 × 36 cm and pitched by -18 degrees. Its center height is
calculated from its rotated support geometry:

```text
supportHeight =
    abs(sin(pitch)) * length / 2
  + abs(cos(pitch)) * thickness / 2
```

This places its lowest edge exactly at ground Z = 0 instead of using an
approximate transform that leaves a gap.

### Materials

Every arena surface uses a dedicated opaque matte material:

- blend mode: Opaque
- roughness: 1.0
- specular: 0.0
- no opacity input
- no emissive input
- no grid texture
- no glossy reflection
- no legacy white object material

Floor and walls also use fixed non-white matte colors.

### Lighting and rendering environment

Lighting is constructed deterministically at runtime. Level-authored directional
lights, skylights, and post-process volumes are replaced.

Current lighting contract:

- one fixed diagonal directional key light
- key rotation: -42 degrees pitch, -35 degrees yaw
- warm key color
- key intensity: 6.0
- directional source angle: 3.0 degrees for readable soft shadows
- contact shadows enabled
- one low-strength neutral/cool skylight fill
- skylight intensity: 0.65
- ambient occlusion intensity: 0.65
- ambient occlusion radius: 140 cm
- unbound post-process volume
- manual exposure
- exposure bias: 0.5
- physical-camera exposure disabled
- automatic exposure adaptation disabled
- volumetric clouds removed
- exponential height fog removed
- stable clear sky gradient retained for ambient illumination

Lighting remains identical across V1 episodes. Lighting randomization, if ever
used, belongs to a later generalization curriculum.

### Player and camera

- First-person player mesh is invisible.
- Third-person player mesh is invisible.
- Player mesh shadows are disabled.
- Collision capsule and movement collision remain active.
- One centered neutral white crosshair is rendered.
- Fixed first-person camera FOV.

### Movement behavior

The character uses the existing sharp Quake-like movement component:

- walk speed: 1,100 cm/s
- ground acceleration: 30,000 cm/s²
- ground braking deceleration: 2,600 cm/s²
- ground friction: 7.0

The nominal time to reach walking speed is approximately:

```text
1100 / 30000 = 0.0367 seconds
```

At a planned 20 Hz observation rate, acceleration is effectively completed within
one observation interval. Collection should therefore emphasize action
transitions, direction changes, collision, sliding, camera-relative movement, and
visual parallax rather than assuming a long acceleration curve.

## Curriculum

### V1: movement

Enabled:

- W, A, S, D
- ArrowUp, ArrowDown, ArrowLeft, ArrowRight

Forced to zero:

- Q
- E

No trajectory, grenade, cooldown, inventory, damage, break, jump, crouch, or
additional HUD mechanics exist in V1.

### V2: held trajectory preview

V1 behavior remains unchanged and Q is introduced as a level-triggered action:

- Q = 1 shows exactly one green trajectory.
- Q = 0 hides the trajectory.
- Q may be combined with every movement and camera action.
- The trajectory is recomputed from the current state every observation step.
- The predicted path continues until the simulated grenade reaches rest.
- E remains forced to zero.
- No physical grenade is spawned.
- No red line, explosion marker, fuse, charge, or damage state is shown.

Q should be visible in approximately 40–50% of V2 training frames so it is not a
sparse whole-frame prediction target.

### V3: persistent harmless grenades

V1 and V2 remain unchanged and E is introduced:

- E is edge-triggered.
- E = 0 to E = 1 requests one throw.
- Holding E does not repeatedly throw.
- E must be released before another request.
- Requests during cooldown are ignored and not buffered.
- An accepted throw uses the same launch state and trajectory as Q.

Cooldown:

- green crosshair: throw available
- red crosshair: cooldown active
- cooldown duration: exactly 2.0 simulated seconds
- at 20 Hz: exactly 40 transitions
- Q remains available and green during cooldown

Grenades:

- no inventory
- no arbitrary grenade count
- creation is limited only by cooldown
- never explode, damage, or break anything
- bounce and roll until resting
- remain visible at rest until episode reset
- stop ticking once resting
- do not collide with the player or later grenades in V3
- do not affect Q preview
- are all removed at episode reset

### V4: map generalization

Begins only after V1–V3 are learned on the fixed map:

1. Rearrange known objects inside the same square arena.
2. Generate layouts from deterministic recorded seeds.
3. Vary arena dimensions.
4. Vary quantities and combinations of known objects.
5. Introduce new shapes, materials, or lighting only later.

One layout remains fixed for an entire episode. Training and evaluation use
disjoint seed sets. Earlier curriculum data remains in the mixture.

## Simulation timing

Initial target:

| Layer | Rate | Meaning |
| --- | ---: | --- |
| Grenade physics in V2/V3 | 120 Hz | Six substeps per observation |
| Actions and RGB | 20 Hz | One transition every 50 ms |
| Wall-clock generation | Uncapped | Run as fast as rendering/output allow |

Generation must not use time dilation. Simulation advances by fixed time without
waiting for real time. If readback or output cannot keep up, the worker blocks
instead of dropping frames.

The final resolution will be locked after visual inspection. The current
recommended starting point is 256 × 256 RGB at 20 Hz. Crosshair and future
trajectory lines must remain clearly resolvable at the chosen training
resolution.

Disable:

- audio
- VSync
- motion blur
- depth of field
- film grain
- automatic exposure
- temporal camera effects not required by the task
- ray tracing

## Episode and transition contract

Recommended first packaged-build pilot:

- 32 episodes
- 30 simulated seconds per episode
- 600 transitions per episode at 20 Hz
- 601 observations per complete episode
- 19,200 total pilot transitions
- deterministic replay pairs included

Exact alignment:

```text
observation[0] = initial observation
action[0]      = action applied to observation[0]
observation[1] = result after one fixed step
...
action[N-1]    = action applied to observation[N-1]
observation[N] = final result
```

Each logical record represents:

```text
(observation[t], state[t], action[t], observation[t+1], state[t+1])
```

Resets occur only between episodes. Training sequences never cross an episode
boundary. No teleport, camera reset, or hidden discontinuity occurs inside an
episode.

Episode initialization may deterministically randomize:

- valid non-overlapping player position
- yaw across 0–360 degrees
- pitch within an approved range
- zero initial velocity

Approximately 10% of episodes should retain the canonical fixed spawn and camera
orientation for reference and replay testing.

## Meaningful collection-agent design

The agent must not sample every bit independently every frame. That would
overproduce jitter, cancellation, wall-sticking, and visually redundant data.

Use a seeded semi-Markov behavior policy:

1. Select a behavior mode.
2. Select an action combination.
3. Hold it for a sampled number of observation steps.
4. Select a meaningful transition or new mode.

Recommended frame-level coverage targets:

| Behavior | Target |
| --- | ---: |
| stationary | 8% |
| movement only | 30% |
| camera only | 18% |
| movement plus camera | 34% |
| opposing/canceling actions | 5% |
| deliberate collision behavior | 5% |

Recommended hold-duration mixture:

- 25% short: 1–3 steps
- 40% medium: 4–12 steps
- 25% long: 13–40 steps
- 10% very long: 41–100 steps

Important transitions:

```text
idle -> W -> idle
W -> S
A -> D
W -> W+A -> A
camera-left -> idle-camera
camera-left -> camera-right
movement-only -> movement+camera
movement+camera -> camera-only
```

Movement coverage must include:

- every cardinal direction
- all meaningful diagonals
- one- and two-frame taps
- sustained movement
- direction reversal
- movement while yawing
- movement while pitching
- direct collisions
- glancing/sliding collisions
- recovery from collision
- paths near every object
- paths through every arena region

Camera coverage must include:

- slow and fast yaw sweeps
- short corrections
- sustained rotation
- direction reversal
- pitch movement
- pitch-limit behavior
- full yaw coverage
- camera-only and camera-plus-movement states

Collision behavior:

1. Deliberately approach a wall or object.
2. Continue pressing into it for 3–15 steps.
3. Recover by reversing, turning, or strafing.

Target approximately 8–15% near-contact/contact frames, distributed across every
object and perimeter walls. Intended action bits are always recorded even when
movement is blocked.

The collector may use privileged position, contact, visibility, and visitation
statistics to balance the dataset. This does not make those fields model inputs.
A 16 × 16 arena visitation grid can upweight underrepresented regions.

## Dataset schema

### Per episode

- schema version
- curriculum version
- episode ID
- worker ID
- deterministic seed
- Git commit/build revision
- Unreal Engine version
- map/configuration hash
- observation and physics rates
- RGB dimensions
- camera FOV
- initial player transform
- requested and actual episode length
- termination reason
- shard and checksum information

### Per observation

- episode ID
- frame index
- integer simulation step/timestamp
- RGB observation key
- player position XYZ
- player velocity XYZ
- camera yaw, pitch, roll
- grounded state
- contact/collision diagnostics
- crosshair state: Neutral, Ready, or Cooldown
- cooldown remaining in steps
- Q visibility
- stable per-episode grenade records:
  - grenade ID
  - position XYZ
  - velocity XYZ
  - resting flag

### Per transition

- episode ID
- source frame index
- packed ten-bit requested action mask
- ten individual Boolean action columns
- effective/cancelled movement axes
- effective/cancelled camera axes
- E request edge
- E accepted event
- resulting cooldown state
- observation-valid flag

V1 retains the complete schema with deterministic defaults:

- Q = 0
- E = 0
- Q visibility = false
- grenade collection = empty
- crosshair state = Neutral
- cooldown remaining = 0

## Final storage design

Implement the real sharded format immediately. Do not first create a
millions-of-files PNG/JSON dataset that must later be replaced.

Recommended structure:

```text
movement_v1/
  dataset.json
  shard-000000.tar
  shard-000001.tar
  ...
```

Each approximately 500 MB–1 GB tar shard contains:

- lossless WebP RGB frames
- `frames.parquet`
- `episodes.parquet`
- shard manifest and checksum

WebDataset-style tar shards avoid millions of filesystem entries while retaining
exact frame access. Ordinary MP4 files may be produced separately for human
review, but are not the authoritative training source because temporal codecs
complicate exact frame access and introduce inter-frame artifacts.

## Packaged build-first workflow

The Unreal Editor is used for authoring and visual inspection. Dataset correctness
is judged using the packaged executable.

Implementation sequence:

1. Freeze and visually accept Curriculum V1.
2. Implement the in-game seeded action policy.
3. Implement fixed-step action/state/RGB synchronization.
4. Implement asynchronous GPU readback with exact frame IDs.
5. Implement lossless WebP, Parquet, and tar-shard output.
6. Package a Win64 Development data-generator build.
7. Generate the 32-episode pilot using the packaged executable.
8. Validate alignment, determinism, coverage, and shard integrity.
9. Add the external multi-worker launcher.
10. Benchmark safe processes per GPU.
11. Scale only after the pilot passes.

The packaged build must accept:

- curriculum version
- worker ID
- seed start/range
- episode count/range
- output directory
- observation rate
- render resolution
- shard-size target

Each worker writes independent shards using disjoint worker IDs and seed ranges.

## Validation requirements

Generation fails if:

- RGB and metadata counts differ
- frame indices are discontinuous
- timestamps are non-monotonic
- an action record is missing
- an observation is dropped
- a shard is incomplete
- a checksum fails
- deterministic replay materially disagrees in authoritative state

Automatic reports must include:

- action and action-combination frequencies
- hold-duration distribution
- position visitation heatmap
- yaw and pitch histograms
- linear and angular velocity histograms
- contact frequency by object
- distance-to-object distributions
- stationary-frame percentage
- duplicate/near-duplicate RGB percentage
- missing-frame count
- episode-length consistency
- shard sizes and checksums
- replay-test results

Data-volume rollout:

1. 19,200-transition packaged pilot.
2. 100,000-frame distribution and visual pilot.
3. 1,000,000-frame first training run.
4. Scale toward 10,000,000 frames only after rollout evaluation.

At 20 Hz, one million frames represent approximately 13.9 simulated hours.

## Curriculum evaluation gates

V1:

- multi-step player-position error
- camera yaw/pitch error
- velocity error
- collision/contact accuracy
- visual rollout drift

V2:

- trajectory-mask overlap
- endpoint error
- curve-distance error
- correct Q appearance/disappearance timing

V3:

- accepted versus ignored E behavior
- exact red-to-green cooldown transition
- projectile position/velocity error
- Q preview versus thrown-path agreement
- resting-location error
- resting-grenade persistence

Progression is based on multi-step rollout quality, not only one-frame pixel loss.

## Immediate next milestone

After final human visual acceptance of the fixed V1 arena, implement the real
packaged data generator:

- deterministic semi-Markov collector
- fixed 20 Hz transition loop
- synchronized RGB and privileged state
- lossless WebP + Parquet tar shards
- packaged Win64 Development executable
- 32-episode validation pilot
