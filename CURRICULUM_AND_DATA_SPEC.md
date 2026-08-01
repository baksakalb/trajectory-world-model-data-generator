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

The first packaged single-worker generator preflight is also implemented and has
passed:

- Win64 Development packaging
- deterministic seeded semi-Markov actions
- fixed 20 Hz simulation and observation stepping
- synchronous GPU RGB readback from the player camera
- directly streamed lossless PNG observations inside a tar shard
- aligned frame, transition, and episode JSONL metadata
- bounded episode count with automatic process exit
- shard checksum generation
- offline count, continuity, dimension, and checksum validation
- MP4 review generation exclusively from stored shard observations
- exact same-seed replay across all tested state, action, and RGB records
- deterministic coverage-guided object-orbit, ramp-traversal, and hoop-passage missions
- privileged per-frame viewpoint bins and verified per-episode interaction counters

The generator now supports all three fixed-map curriculum stages from one
executable:

- `-Stage=movement` for V1
- `-Stage=trajectory` for V2
- `-Stage=throw` for V3

`Generate_Curriculum_Comparison.bat` runs identical 12-second seeds for all
three stages at both 320 × 320 and 384 × 384, validates every authoritative
shard, and derives review MP4s from the stored observations.

Still required before production-scale collection:

- asynchronous GPU RGB readback
- lossless WebP encoding
- Parquet metadata
- target-size shard rollover
- full elevation, projected-size, occlusion, and action-distribution reporting
- resumable one-worker-per-GPU batch launcher
- human-play capture mode using the same observation/action contract
- final production mission distributions and camera-pitch distribution

The preflight deliberately proves the gameplay loop, synchronization, direct
sharding, replay, and inspection workflow before those storage and throughput
upgrades. It does not use screen recording.

## Packaged generator preflight usage

The packaged folder contains:

```text
he_grenade_game.exe
Generate_Small_Pilot.bat
generator-config.json
Tools/review_dataset.py
```

Edit `generator-config.json` to select:

- curriculum stage
- episode count and duration
- starting seed
- worker ID
- observation rate
- RGB dimensions
- output directory
- whether `coverage_guided` collection is enabled

Double-click `Generate_Small_Pilot.bat` to run the configured pilot. The default
configuration produces two deterministic ten-second episodes and then exits.

The equivalent direct command is:

```text
he_grenade_game.exe -GenerateDataset
  -GeneratorConfig="generator-config.json"
  -RenderOffscreen -unattended -nosound -NoSplash -NoVSync
```

Command-line values such as `-Stage=`, `-Episodes=`, `-EpisodeSeconds=`, `-SeedStart=`,
`-WorkerId=`, `-ObservationRate=`, `-Width=`, `-Height=`, and `-Output=` override
the JSON configuration.

`-TrajectoryShowcase` is an inspection-only trajectory policy with continuous
trajectory visibility and controlled camera motion. Its dataset metadata is
marked `inspection_only_trajectory_showcase`; it must not be mixed into training.

Validate a completed run with:

```text
python Tools/review_dataset.py GeneratedData/small-pilot --validate-only
```

To derive review videos, install FFmpeg and omit `--validate-only`, optionally
passing `--ffmpeg="C:\path\to\ffmpeg.exe"`. The converter reads every PNG directly
from the authoritative shard in frame-index order. It never renders a second
gameplay pass.

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

- brown rectangular prism
- yellow solid square-based pyramid
- orange sphere
- magenta hoop
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

The floor uses a fixed neutral gray. Each wall uses a distinct muted matte
color with approximately matched luminance:

- north: terracotta
- south: blue-gray
- east: ochre
- west: mauve

The four colors provide orientation and make every corner visually unique
without competing with the green trajectory or red cooldown crosshair.

### Lighting and rendering environment

Lighting is constructed deterministically at runtime. Level-authored directional
lights, skylights, and post-process volumes are replaced.

Current lighting contract:

- one fixed diagonal directional key light
- key rotation: -42 degrees pitch, -35 degrees yaw
- warm key color
- key intensity: 10.0
- directional source angle: 3.0 degrees for readable soft shadows
- contact shadows enabled
- one neutral opposing directional fill
- fill rotation: -25 degrees pitch, 145 degrees yaw
- fill intensity: 2.0
- fill shadows disabled
- one low-strength neutral/cool skylight fill
- neutral non-shadowing skylight intensity: 1.4
- ambient occlusion intensity: 0.25
- ambient occlusion radius: 140 cm
- unbound post-process volume
- manual exposure
- exposure bias: 1.45
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

The final resolution will be locked after matched-seed visual inspection. The
current training candidate is 320 × 320 RGB at 20 Hz, compared against a
384 × 384 authoritative-source candidate. The compact crosshair and trajectory
ribbon use fixed screen-pixel widths rather than scaling with resolution. The
trajectory ribbon is rasterized from every 120 Hz simulation point with a
one-pixel antialiased fringe so its low-resolution edges remain smooth.

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

Camera pitch must not be sampled uniformly. Ordinary eye-height views are the
dominant gameplay distribution and must receive substantially more frames than
views looking steeply upward or downward. Moderate pitch changes remain common
enough to learn vertical camera dynamics. Near-limit up/down views remain
present but deliberately sparse. The exact near-eye, moderate, and extreme
mixture is an open parameter to choose after reviewing pitch histograms from
both automated and human-play pilots.

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

### Implemented coverage-guided mission schedule

Coverage guidance is enabled by default for dataset generation and can be
disabled with `coverage_guided: false` or `-NoCoverageGuided`. It never
teleports, rotates, or corrects the player inside an episode. Privileged state
only selects canonical action bits and measures coverage.

Every repeating ten-episode block contains:

| Slot | Mission |
| ---: | --- |
| 0 | canonical-spawn semi-Markov reference |
| 1 | orbit and continuously view the rectangle |
| 2 | orbit and continuously view the pyramid |
| 3 | orbit and continuously view the sphere |
| 4 | orbit and continuously view the hoop |
| 5 | orbit and continuously view the ramp |
| 6 | physically traverse the ramp from its usable low side |
| 7 | physically pass through the hoop opening one to three times |
| 8–9 | randomized semi-Markov action diversity |

Mission geometry is sampled once from the deterministic episode random stream.
The recorded seed therefore reproduces the exact mission, while different seeds
vary it:

- object approach position, orbit radius, direction, per-sector angle, and
  radial waypoint jitter
- ramp low-side start, far-side goal, and lateral approach offsets
- hoop starting side, entry and exit offsets, and required passage count
- initial camera yaw and pitch offsets

Object missions use twelve perturbed azimuth goal regions rather than one fixed
circle. Translation pauses when camera yaw falls too far behind, allowing
ordinary arrow-key input to recover the target without hidden camera snapping.
A viewpoint bin is credited only while the target is inside the accepted camera
angles and no other object occludes the line of sight.

Ramp traversal is credited only after the character capsule rises substantially
above floor height and reaches the ground beyond the high edge. Hoop passage is
credited only when the capsule crosses the hoop plane inside both its lateral
and vertical safe corridors.

Guided episodes terminate immediately when their measured requirement succeeds.
If movement actions produce less than one centimeter of displacement for one
second, the generic no-progress watchdog terminates the attempt as a failure.
The ordinary episode time limit remains a generic mission timeout. This avoids
per-obstacle recovery exceptions and prevents post-success duplicate frames.

Frames record the active mission, target, visibility, azimuth bin, distance
band, waypoint, success/failure state, and no-progress counter. Episode metadata
records all sampled mission parameters, requested and actual length, outcome,
and termination reason. Dataset metadata summarizes successes and failures.

The packaged validation run `seeded-missions-validation-128` exercised two
independently sampled ten-episode schedules at 20 Hz. All fourteen guided
missions succeeded with zero mission failures. Both ramp variants traversed the
incline, the hoop variants successfully completed one and three requested
passages, and both variants of every object mission reached all twelve visible
azimuth bins. Early termination reduced the run from a possible 4,000
transitions to 1,962 without adding stationary post-success footage.

The 384 × 384 review exposed an important open design issue: repeated hoop
passages can look unnaturally fast because the character reaches its real
1,100 cm/s speed quickly and immediately reverses between nearby goals. The
current one-to-three-passage sampler is therefore validation scaffolding, not an
approved production distribution. Mission count, approach length, action
variation, pauses, and whether repeated interactions belong in one episode or
separate seeded episodes will be decided through visual review before the large
V1 collection.

### Planned human-play collection

The user will also play the game and contribute human-generated trajectories.
Human data complements rather than replaces guided and semi-Markov data:

- guided missions guarantee rare geometry and interaction coverage
- seeded semi-Markov play supplies broad canonical-action transitions
- human play supplies natural navigation, hesitation, corrections, attention,
  and action combinations that scripted policies may underrepresent

A capture-only human mode still needs to be implemented. It must disable the
action override, record the same 20 Hz RGB/action/state alignment, write the same
authoritative shard format, and tag every episode with a distinct
`human_keyboard` collection policy and session ID. Human and automated episodes
must remain identifiable so mixture weights can be changed during training.
Whole human sessions—not neighboring clips from one session—must be assigned to
train or held-out evaluation to avoid temporal leakage.

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
- collection mission and seed-sampled mission parameters
- mission-required and mission-success flags
- achieved viewpoint-bin mask and verified interaction counts
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
- active collection mission, target, waypoint, and coverage bins
- mission success/failure and no-progress counter
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

### Current same-GPU worker benchmark

The packaged synchronous PNG preflight was benchmarked at 384 × 384 on the
trajectory stage using identical 20-second seeded workloads:

- one worker: 20 simulated seconds in 19.42 seconds after generator
  configuration (22.27 seconds from process launch)
- three concurrent workers: 60 aggregate simulated seconds in 396.79 seconds
- aggregate three-worker throughput: 0.151 simulated seconds per wall second
- scaling versus one worker: 0.147×
- all four resulting shards passed validation

Three Unreal D3D12 capture processes therefore show severe negative scaling on
the tested single-GPU desktop. Production collection on this machine must use
one worker per GPU unless a later asynchronous-readback implementation is
re-benchmarked. Multiple workers remain valid across separate GPUs or machines.

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

1. Approximately 10,000 frames for trainer and schema smoke tests.
2. Implement coverage, pitch, action, collision, duplicate-frame, and
   human-versus-automated mixture reports.
3. Generate a serious initial Movement V1 dataset of approximately 500,000
   frames at the working 384 × 384 candidate resolution.
4. Reserve approximately 10% using disjoint seeds/sessions for held-out
   evaluation.
5. Train and inspect multi-step rollouts before extending Movement V1 toward one
   million frames.
6. Add V2 trajectory data and then V3 throw data only after the preceding
   curriculum gate passes.

At 20 Hz, 500,000 frames represent approximately 6.94 simulated hours. With the
current lossless PNG preflight this is estimated at roughly 70–75 GB and seven
to eight hours of single-worker desktop generation. One million frames represent
approximately 13.9 simulated hours and 140–150 GB before the planned lossless
WebP/storage improvements.

The working training budget is approximately USD 100 using RunPod for storage
and training. This rules out retaining thousands of hours or multi-terabyte
lossless datasets for the first iteration. Collection must be coverage-driven,
resumable, and evaluated incrementally. The tested desktop configuration uses
one generator process per GPU; the three-worker same-GPU benchmark was severely
slower than sequential generation.

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

## Current agreed plan and immediate next work

The curriculum order remains:

1. Movement V1
2. held trajectory V2
3. persistent harmless throw V3

The current arena, lighting, object colors, compact crosshair, smoothed
trajectory, fixed 20 Hz observation rate, and authoritative frame-first
data/video workflow are accepted working foundations. MP4 is always derived
from saved authoritative observations; gameplay and review video are never
generated as separate passes.

The working visual candidate is 384 × 384. It preserves more meaningful object
and trajectory geometry than the lower candidates while remaining manageable
for the initial budget. It is not a permanent commitment for every future model
or encoder.

Mission mechanics are implemented and validated, but their production
distributions are intentionally not yet frozen. The next design session will
decide how missions vary and how much of the mixture they occupy. Required
principles:

- sample mission variables deterministically from the recorded episode seed
- specify goal regions and measurable outcomes rather than one exact path
- vary start position, approach direction, lateral offset, camera offset,
  movement pattern, and relevant interaction count
- use only the canonical action bits during an episode
- never teleport, snap the camera, or correct velocity inside an episode
- terminate immediately on verified success
- use generic no-progress and time-limit failures rather than obstacle-specific
  recovery scripts
- retain failed attempts as identifiable diagnostics and exclude them from the
  accepted training mixture unless deliberately studying collision failure
- guarantee views around all five objects plus successful ramp and hoop examples
- avoid unnaturally rapid repeated hoop/ramp interactions merely to inflate
  counts

Before the 500,000-frame Movement V1 collection:

- visually review and approve the final seeded mission distributions
- choose and verify an eye-height-dominant pitch mixture with sparse extremes
- implement human keyboard capture in the same authoritative schema
- decide initial guided/semi-Markov/human mixture weights
- implement asynchronous frame-ID-keyed GPU readback
- implement lossless WebP + Parquet tar shards
- implement target-size shard rollover and resumable sequential batches
- implement complete coverage and distribution reports
- run a small mixed-policy pilot with disjoint held-out seeds and human sessions
- train the smoke model before authorizing the serious 500,000-frame run
