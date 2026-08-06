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

This README is the source of truth for the current environment, curriculum,
controls, build workflow, data contract, collection policy, and remaining work.

## Project-level design record

This section is the short repository-level explanation of what the project is,
what works now, which production decisions are frozen, and what must be built
next. The later sections are the detailed authoritative specification.

### What the project trains

The project generates action-conditioned first-person trajectories for a visual
world model. The model observes RGB history plus the requested keyboard action
bits and predicts the consequences. The generator—not the model—chooses actions
during automated collection. Position, velocity, contact, mission, visibility,
and other privileged fields are recorded for validation and balancing; they are
not automatically part of the model input.

The curriculum is intentionally staged:

1. Movement V1 learns player translation, camera motion, collision, ramp
   traversal, and hoop passage.
2. Held-trajectory V2 adds the visible held-grenade trajectory controlled by Q.
3. Persistent harmless-throw V3 adds edge-triggered E throws, grenade flight,
   rest, and cooldown without destruction.

The serious first collection is Movement V1. V2 and V3 reuse the same fixed
world, action/state/RGB alignment, collection framework, and storage contract
after Movement V1 passes its model-training gates.

### Decisions that are frozen

| Topic | Final project decision |
| --- | --- |
| Initial observation | 384 x 384 RGB at 20 Hz |
| Physics | fixed-step simulation; grenade physics uses 120 Hz in V2/V3 |
| Controls | ten canonical Boolean bits: W, A, S, D, four arrows, Q, and E |
| Environment | one fixed, deterministic arena layout for an entire episode |
| First production source | automated Movement V1 only; human capture is deferred |
| Production size and mixture | the user chooses credited frame target X; the frozen Movement V1 frame mixture is 55% semi-Markov, 20% object, 15% contact/recovery, 5% ramp, and 5% hoop |
| Evaluation reserve | assigned before collection through disjoint prescribed recipe identities |
| Automated behavior | frame-balanced semi-Markov play plus object, contact, ramp, and hoop missions |
| Replay | exact replay keys, explicit categorical scenario cells, stratified continuous values, and stateless jitter |
| Production scheduling | one central controller creates a global budget-driven recipe plan before worker assignment; workers do not select production missions independently |
| Budget semantics | X counts only semantically credited training observations; diagnostic semantic-failure frames do not consume X, complete discrete coverage is mandatory, and infeasible budgets are rejected before generation |
| Toy-plan meaning | the complete discrete catalog with minimal repetition and the coarsest progressive continuous coverage; never a reduced object/mission catalog |
| Distributed execution | one synchronous generator process per GPU; scale with separate RunPod workers rather than multiple processes on one GPU |
| Work boundary | one assignment block produces one shard; interrupted partial shards are discarded and the assignment is retried |
| Controller ledger | immutable plans, recipes, assignments, attempts, and validated results on persistent storage; derived progress can be rebuilt |
| Guided movement | canonical action bits only after spawn; no mid-episode teleport, camera snap, velocity correction, or hidden steering |
| Object attention | movement path and camera gaze are independent; orbiting never requires keeping the object centered or visible |
| Mission ending | success is latched, followed by 0.75-1.50 seconds of coherent, varied continuation |
| Ordinary collision | useful contact remains, but continuous contact is bounded and followed by a world-space escape |
| Balancing | successful pre-success observation frames, not episode counts |
| Failed missions | a valid semantic failure resolves its recipe, remains diagnostic, receives no intended coverage credit, and never causes an infinite retry loop |
| Review video | derived only from authoritative stored observations; MP4 is never the training source |
| Capture implementation | retain synchronous GPU readback for the first dataset; asynchronous readback is not a production gate |
| Production storage | lossless WebP plus typed Parquet metadata inside independently validated tar shards |
| Platform order | Windows and Linux controller validation are complete; the same immutable 300k plan passed on Windows and an RTX A6000 RunPod |

Both the PNG/JSONL reference path and the lossless-WebP/typed-Parquet production
path are implemented on Windows. A paired 19,232-observation pilot measures the
actual storage and generation tradeoff below; the production encoder defaults to
its fastest lossless effort because higher lossless effort was not useful for
this collection workload. Central prescribed scheduling, assignment/result
tracking, whole-shard retry, and reconstructable inventory are now implemented
and validated on Windows. The same synchronous generator now cross-compiles and
packages for Linux x86-64 with native static libwebp. The packaged ELF launches
under Ubuntu 22.04 in WSL2 and has no missing shared libraries, but local rendered
qualification stops at Vulkan initialization because this PC's WSL environment
exposes only Mesa llvmpipe rather than the RTX 4060 and therefore cannot satisfy
Unreal's Vulkan SM6 profile. The packaged build has now passed the authoritative
RunPod GPU-runtime gate and the complete prescribed 300k Linux parity run. The
allocation policy and plan identity were unchanged between platforms.

### Linux qualification state

The local Linux result separates build correctness from GPU-runtime
qualification:

- **Proved locally:** the UE 5.8 v26 Clang toolchain compiles and links the
  Linux target; native static `libwebp` and `libsharpyuv` link successfully; a
  full Development `BuildCookRun` completes; the packaged x86-64 ELF is valid;
  Ubuntu 22.04 reports no missing shared libraries; and execution reaches
  Unreal's rendering initialization.
- **Not proved locally:** rendered frame capture and Linux WebP/Parquet shard
  validation. WSL2 on this PC exposes only Mesa `llvmpipe`, a CPU software
  Vulkan device. It does not expose the RTX 4060 as an Unreal-compatible Vulkan
  SM6 device, so Unreal stops with `VK_ERROR_INCOMPATIBLE_DRIVER` before the
  dataset generator begins rendering.
- **Interpretation:** this is a limitation of the local WSL GPU environment,
  not evidence of a compile, link, packaging, controller, or libwebp defect.
- **RunPod rendered-runtime gate passed (2026-08-05):** the packaged Linux build
  initializes Unreal's `VULKAN_SM6` RHI on an NVIDIA RTX A6000 (48 GB, driver
  580.159.03), loads `Lvl_FirstPerson`, and constructs the fixed Movement V1
  arena. An unused legacy `MI_ArenaWallGrid` constructor reference was removed
  after its uncooked parent emitted a fallback warning; the rebuilt package
  repeats the smoke test without that warning. No dataset-generation arguments
  were used during either smoke test.
- **Verified RunPod setup:** pod `movement-v1-linux-graphics` used the
  `runpod/kasm-docker:cuda11` desktop image with NVIDIA graphics/Vulkan driver
  capabilities enabled. The allocation exposed approximately 9 vCPUs, 50 GB
  RAM, a 60 GB container disk, and a persistent 20 GB `/workspace` volume.
  RunPod's browser terminal remained unavailable, so commands were executed over
  the SSH gateway with a PTY and mirrored into a local live log. The build and
  controller payload were uploaded to `/workspace/movement_v1`.
- **Corrected package verified:** after removing the unused wall-material
  reference, UE 5.8 Development `BuildCookRun` succeeded again and produced
  archive SHA-256
  `A7881C0ED1FBB3923B8769D1AEA585C23534D7EB8DB5CC009F147AF3BE0A0097`.
  The corrected remote build initialized the RTX A6000 Vulkan SM6 renderer,
  loaded the map, and built the arena without the legacy ShaderMap warning.
- **Authoritative gameplay capture verified:** one diagnostic Movement V1
  episode ran for 10 seconds at 10 observations/s, 640x360, seed `424242`, and
  worker `98`. Validation reported 1 episode, 100 transitions, and 101
  observations. `Scripts/review_dataset.py` rendered the review MP4 strictly
  from the stored RGB observations; desktop/X11 recordings are not accepted as
  gameplay evidence or training data.
- **Prescribed Linux 300k parity gate passed (2026-08-06):** the RTX A6000 ran
  the unchanged immutable Windows plan `plan-c9487d1dfac92531` with one worker,
  seed start `1000`, 20 Hz capture, 64x64 lossless WebP observations, and typed
  Parquet metadata. The reconstructed inventory reports `complete: true`,
  `coverage_complete: true`, 300,015 accepted observations, 3,202 episodes,
  296,813 transitions, 27 successful assignments, all 887/887 discrete cells,
  zero semantic failures, and zero technical failures. The 15-frame overshoot
  is the expected clean recipe-boundary behavior.
- **Independent Linux revalidation passed:** `verify-plan` reproduced the valid
  887-cell plan and all 27 published tar shards independently passed checksum,
  schema, WebP, Parquet, episode, transition, and observation validation. The
  persistent report is stored as
  `collections/linux-windows-300k-parity-20260806/verification-20260806.log`
  under the RunPod `/workspace/movement_v1` payload.
- **Visual review evidence generated:** two successful Linux episodes were
  rendered for each of the 21 primary mission subtypes (42 MP4s total), covering
  eight semi-Markov initial behavior families, four object-view modes, five
  contact-recovery styles, both ramp directions, and both hoop directions. Each
  clip is indexed by recipe, seed, complete discrete cell, frame count, and goal
  in `review-videos/manifest.json`; review MP4s remain derived QA artifacts and
  are not training inputs.
- **Retention:** stopping the pod preserves its local `/workspace` volume while
  storage billing continues; terminating the pod removes that volume. Critical
  collection metadata and data still require an independent backup or migration
  to portable network/object storage.

### RunPod RTX 4090 and network-volume handoff (2026-08-06)

The earlier A6000 pod was intentionally terminated after its parity result; its
pod-local 300k collection was deliberately discarded. A 100 GB RunPod network
volume named `world_model_trajectory` (volume ID `uuhi6dkd8h`) now exists in
`CA-MTL-3`. Network volumes are data-center-bound. At the time of creation the
A6000 and 4090 allocations in that data center were out of capacity, so no
production pod has yet been attached to this volume.

A separate temporary, non-persistent RTX 4090 pod qualified the exact corrected
Linux package and capture path. It used `runpod/kasm-docker:cuda11`, a 60 GB
container disk, SSH enabled, Jupyter disabled, and
`NVIDIA_DRIVER_CAPABILITIES=graphics,compat32,utility`. The host exposed an RTX
4090 with driver `580.159.04`, `DISPLAY=:1`, and the NVIDIA Vulkan ICD at
`/etc/vulkan/icd.d/nvidia_icd.json`. Ordinary `vulkaninfo` against the Kasm X11
display failed with X `BadMatch`; with `DISPLAY` unset it enumerated the RTX 4090
and NVIDIA Vulkan driver successfully. This X11 diagnostic failure does not
affect the authoritative offscreen generator path: Unreal selected
`VULKAN_SM6`, loaded `Lvl_FirstPerson`, and built the fixed Movement V1 arena and
lighting under `-RenderOffscreen`.

The same pod then completed a bounded production-format capture at 384x384 RGB,
20 Hz, lossless WebP effort 0: one five-second episode, 100 transitions, and 101
observations. Parquet finalization completed and
`Scripts/review_dataset.py --validate-only` passed all 1/100/101 records. The RTX
4090 is therefore approved as a generator GPU, subject to repeating the same
short qualification on the eventual network-volume pod before serious
collection.

File transfer has a strict version and integrity gate. The SSH gateway does not
provide the SCP subsystem. `runpodctl` 2.8.1 produced full-size but
checksum-mismatched, gzip-invalid copies of the 488,501,066-byte payload, even
after overlapping stale send/receive processes were removed and a clean retry
was performed. After upgrading both sender and receiver to `runpodctl` 2.9.0,
the transfer passed gzip integrity and matched the authoritative payload SHA-256
`A7881C0ED1FBB3923B8769D1AEA585C23534D7EB8DB5CC009F147AF3BE0A0097`.
Never extract or execute a transferred payload before both checks pass. The old
Kasm system pip was also incomplete; bootstrapping current pip with
`https://bootstrap.pypa.io/get-pip.py` and then installing
`Scripts/requirements-production.txt` supplied Pillow 12.1.1 and PyArrow 25.0.0.

The next session must resume in this order:

1. Select a network-volume data center with usable RTX 4090 capacity, deciding
   explicitly whether to retain/wait on the existing `CA-MTL-3` volume or create
   the production volume elsewhere.
2. Deploy one pod with that network volume mounted at `/workspace`, using the
   qualified Kasm template and exactly one generator process on one GPU.
3. Confirm GPU identity, NVIDIA graphics capabilities, Vulkan ICD, headless
   Vulkan enumeration, mount identity, capacity, and write access.
4. Transfer with `runpodctl` 2.9.0 on both ends; verify exact byte size, SHA-256,
   and `gzip -t` before extraction.
5. Install the pinned Python production requirements, verify ELF dependencies,
   and repeat the Unreal offscreen smoke test.
6. Generate, finalize, and validate one small production-format shard directly
   on the network volume; stop/restart the pod and prove the shard persists and
   revalidates.
7. Only after those gates pass, create and verify the immutable serious Movement
   V1 collection plan and authorize the real generation run.

### First-dataset collection plan

The first production dataset is automated Movement V1. Its accepted-frame target
X remains a user input, while the mission and nested frame shares are frozen and
recorded in every immutable, versioned `collection-plan.json`. The Windows
planner and execution contract are validated, and Linux reproduced them without
changing the policy. The plan allocates semi-Markov, object-view, contact/recovery, ramp, and hoop
coverage; enumerates the required discrete cells and continuous strata; assigns
disjoint train/evaluation recipe identities; and reserves a bounded set of makeup
recipes for valid semantic failures. A smaller requested dataset reduces
intentional repetition and continuous refinement, not the supported missions,
objects, directions, or discrete scenario cells.

These sources serve different purposes. Semi-Markov play supplies broad,
coherent action transitions. Guided missions guarantee rare geometry and
interaction coverage. Human capture is deliberately outside the first
production scope and may be added as a later, separately versioned source.

Finite mission choices are enumerated instead of left to chance. Object
missions cover all five objects, approach/observe, pass-by, partial/full orbit,
both orbit directions, and independent gaze plans. Contact missions cross all
five objects and four walls with direct/glancing approaches, five recovery
styles, and five movement-versus-camera facing profiles. Ramp and hoop missions
cover both travel directions, center and diagonal/oblique paths, and forward,
backward, both strafe, and free-attention facing profiles. Continuous geometry
such as radius, start/end points, offsets, and durations is stratified and
jittered inside validated safe ranges.

The frozen mission frame shares are 55% semi-Markov, 20% object view, 15%
contact/recovery, 5% ramp, and 5% hoop. Object targets are equal; object modes
target 40/35/20/5 for approach/pass-by/partial-orbit/full-orbit; gaze targets
40/25/20/15; orbit directions are equal. Contact targets, recovery styles, and
approaches are equal, with 35/15/15/15/20 facing. Ramp and hoop directions are
equal, paths are 50/25/25, and facing is again 35/15/15/15/20.

Recipe counts are derived from those frame shares using the versioned
per-scenario duration calibration in
`Scripts/movement_v1_duration_calibration.json`. The calibration comes from a
300,177-frame Windows prescribed run and prevents long modes from being
overrepresented merely because an episode lasts longer. With the complete
887-cell mandatory floor and current calibration, the smallest plan which can
also satisfy all frozen aggregate shares is 344,534 credited frames. The
limiting constraint is mandatory contact-facing coverage. Smaller plans are
diagnostic only and production plan creation rejects them explicitly.

Mission success does not cut the footage abruptly. After the ordinary objective
and endpoint hold succeed, the generator preserves the completing action for a
short lead and then uses one of six coherent continuation styles for a total
0.75-1.50-second post-success rollout. Mission credit freezes on the success
observation, while the continuation remains valid transition data. Ordinary
semi-Markov wall/object contact uses a short sampled dwell followed by an
explicit escape, preventing long blocked holds without deleting useful
collision and sliding examples.

The central controller dispatches immutable recipe blocks until accepted-frame
quotas are met, a bounded reserve is exhausted, or a coverage bucket is explicitly
marked blocked. Only clean episode/sequence boundaries may be used for final
selection. Distribution reports, shard checksums, deterministic replay checks,
mission-semantic validation, and visual review are required before the serious
collection is accepted.

### Implemented now versus still pending

Implemented and validated now:

- the fixed three-stage environment and canonical controls
- fixed-step action/state/RGB alignment and exact replay
- the finalized schema-v10 automated mission and natural-play policy
- post-success continuation and bounded natural-play collision escape
- direct PNG/JSONL tar preflight output, checksums, validation, and MP4 review
- native lossless-WebP tar observations and typed Parquet metadata
- paired-format benchmarking and exact decoded-pixel/metadata comparison
- focused, mixed, stress, replay, 384 x 384 video, and all-frame mission QA
- optional immutable prescribed-recipe manifests in the packaged Windows game
- a deterministic breadth-first planner covering 887 complete discrete cells:
  32 semi-Markov, 120 object, 675 contact, 30 ramp, and 30 hoop cells
- immutable assignments, attempt claims, results, inventory snapshots,
  whole-shard retry, duplicate suppression, graceful stop, and bounded reserves
- a coverage-complete 20 Hz Windows toy: 887/887 recipes and cells credited,
  zero semantic failures, and 54,868 validated WebP observations

Still required before serious automated Movement V1 production:

- freeze the numerical production plan only after the Windows and Linux gates
  pass
- add Linux libwebp support, package Linux x86-64, and validate on RunPod
- implement complete action, spatial, contact, duplication, and storage reports
- run a small trainer/model smoke test

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
- Current curriculum project folder:
  `C:\Users\baris\Desktop\he_grenade_game_curriculum`

The original game is not modified by curriculum work. Its full grenade,
trajectory, breakable, and procedural-arena systems remain available through Git
history and the baseline tag for later curriculum stages.

Generated `Binaries`, `Intermediate`, `Saved`, and derived-data caches are not
authored source. Unreal regenerates them locally.

### Audited source lineage

The preserved implementation checkpoints are:

| Commit | Implemented work |
| --- | --- |
| `340d6c5` | fixed Movement V1 arena, canonical keyboard controls, matte curriculum assets, pyramid source/import, minimal HUD, and the first version of this specification |
| `37cb24d` | accepted lighting, wall/object palette, corrected ramp support geometry, and revised pyramid geometry/import |
| `b6eee94` | packaged seeded data-generator preflight, all three fixed-map stages, direct tar writing, PNG/JSONL metadata contract, validator/review tool, deterministic replay tests, coverage-guided missions, resolution comparisons, and the same-GPU worker benchmark |
| `aaf3a21` | finalized schema-v10 automated mission behavior, categorical cells, stratified parameters, mission-semantic validation, post-success continuation, and bounded natural-play collision escape |
| `486511f` | native lossless-WebP encoding, typed Parquet finalization, cross-format validation, and paired storage benchmark |
| `892cceb` | engine-authoritative camera pitch alignment and focused deterministic regression |

The current Windows baseline includes the schema-v10 mission policy, the
one-shard WebP/Parquet production-v1 path, prescribed episode recipes, and the
central controller/worker ledger. It does not include Linux libwebp linkage, a
Linux packaged build, asynchronous frame readback, or in-process shard rollover.

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

The current project revision retains that preflight path and adds:

- observation-frame-balanced semi-Markov, object-view, contact/recovery,
  ramp-traversal, and hoop-passage missions
- a replayable hybrid sampler: complete categorical scenario cells,
  stratified continuous ranges, and stateless per-parameter hashing
- object approach/observe, pass-by, partial-orbit, and full-orbit submodes
- target-center, target-offset, travel-direction, and roam/reacquire
  camera-gaze plans which remain independent from the movement path
- explicit clockwise and counter-clockwise orbit cells, with full orbits
  returning to their starting azimuth after visiting all 12 bins
- five explicit contact-recovery styles crossed with all nine contact targets
- direct and left/right glancing contact approaches, followed by an outward
  collision-clearance leg and one of five recovery styles
- shared forward, backward, strafe-left, strafe-right, and free-attention
  locomotion-facing profiles for contact approaches, ramps, and hoops
- center and two diagonal ramp paths in both traversal directions
- center and two oblique hoop paths in both passage directions
- four camera-attention styles inside the free-attention locomotion profile
- realized movement-versus-camera angle counters which gate mission success
- interpolated hoop-plane crossing coordinates rather than endpoint-only tests
- primary-objective, final-goal, and post-objective completion requirements
- a latched 0.75-1.50-second post-success rollout which continues the
  completion motion and then applies one of six replayable natural-play styles
- bounded natural-play collision dwell followed by a world-space escape action,
  so long semi-Markov holds cannot keep scraping the same wall
- privileged per-frame viewpoint, mission-phase, pitch-band, and verified
  interaction counters
- per-episode mission forcing for focused tests and inspection
- selectable `png_jsonl` and `webp_parquet` storage paths
- native libwebp lossless observation encoding directly into the tar
- explicit typed PyArrow schemas for frame, transition, and episode Parquet
  payloads, appended to the tar without copying or re-encoding its observations
- validation and review support for both image/metadata formats
- exhaustive PNG-versus-WebP decoded-pixel and normalized-metadata comparison
- assignment-manifest parsing with prescribed mission/scenario indices and
  nested continuous sample ordinals while retaining the automatic QA policy
- controller-owned plans and disjoint assignment blocks; stable logical replay
  identity separated from physical executor/attempt identity
- immutable attempt/result records, exact whole-shard retries, reconstructed
  inventory, idempotent reserve activation, duplicate-result suppression, and
  graceful stop between assignments
- prescribed episode identity fields in typed episode Parquet metadata and
  controller-result semantic/technical outcomes

The retained reference/preflight data path is:

```text
player camera
  -> SceneCapture2D
  -> synchronous render-target ReadPixels
  -> trajectory/crosshair rasterization in the captured pixel buffer
  -> lossless PNG compression
  -> one directly written tar shard per worker run
```

The production path uses the same capture and alignment logic, but sends the
captured BGRA pixels through native lossless libwebp and writes `.webp` members
directly into the tar. It stages the three metadata streams beside the tar;
`Scripts/finalize_production_dataset.py` converts them with explicit PyArrow
schemas, appends `frames.parquet`, `transitions.parquet`, `episodes.parquet`, and
`manifest.json`, updates `dataset.json` and the MD5, and removes staging files
only after success. This split keeps Arrow out of the Unreal runtime while
avoiding an image-copy or re-encode pass.

`Scripts/review_dataset.py` validates either format, including checksum, record
counts, frame/transition continuity, image dimensions, and observation presence,
and derives review MP4s only from the authoritative tar observations. The
generator refuses to overwrite an output directory that already contains
`dataset.json`. Production v1 still deliberately supports exactly one shard per
run.

The generator now supports all three fixed-map curriculum stages from one
executable:

- `-Stage=movement` for V1
- `-Stage=trajectory` for V2
- `-Stage=throw` for V3

`Generate_Curriculum_Comparison.bat` runs identical 12-second seeds for all
three stages at both 320 × 320 and 384 × 384, validates every authoritative
shard, and derives review MP4s from the stored observations.

Still required before production-scale collection:

- Linux libwebp linkage, Linux x86-64 packaging, and RunPod validation
- full elevation, projected-size, occlusion, action, collision-duration, and
  duplication reporting

The current baseline deliberately proves the gameplay loop, synchronization,
direct sharding, replay, and inspection workflow before distributed orchestration.
It does not use screen recording. A same-GPU concurrency experiment was also
completed and rejected for this desktop. The first distributed design therefore
keeps synchronous capture and scales by running one process on each separate
RunPod GPU.

### Windows prescribed-controller evidence

The coverage-complete packaged Windows toy used one primary recipe for every
finite cell at 64 x 64 and the reference 20 Hz control/observation rate. Its
single assignment produced 887 episodes, 53,981 transitions, and 54,868
lossless-WebP observations. Native Parquet finalization, checksum/schema/mission
validation, and inventory reconstruction all passed. Credited outcomes were
32/32 semi-Markov, 120/120 object-view, 675/675 contact/recovery, 30/30 ramp,
and 30/30 hoop cells, with zero semantic failures.

A controlled interruption left attempt 000 unresolved; the same 32-recipe
assignment then failed production-format validation at attempt 001 because an
integration bug had ignored manifest capture settings. After that bug was fixed,
attempt 002 reused the exact logical recipes and passed with 705 WebP
observations. Replaying that prescribed manifest produced identical normalized
metadata and decoded RGBA for all 705 frames, with SHA-256
`c14db252967e917241feaaf3ce4f475701d1156683e88a25430d79171e3bf965`.

A separate deliberately coarse 5 Hz full-catalog diagnostic was technically
valid but resolved 659 guided recipes as semantic failures. It did not block the
ledger, credited only successful cells, activated exactly 659 predefined
reserves, and treated a repeated activation request as a no-op. The 20 Hz pass
shows those cells are valid at the accepted reference rate.

## Packaged generator usage

The packaged folder contains:

```text
he_grenade_game.exe
Generate_Small_Pilot.bat
generator-config.json
Tools/review_dataset.py
Tools/finalize_production_dataset.py
```

Edit `generator-config.json` to select:

- curriculum stage
- episode count and duration
- starting seed
- worker ID
- observation rate
- RGB dimensions
- storage format and lossless-WebP effort
- output directory
- whether `coverage_guided` collection is enabled

Double-click `Generate_Small_Pilot.bat` to run the configured pilot. The default
configuration produces two deterministic ten-second production-format episodes,
finalizes Parquet metadata, validates the result, and then exits.

The equivalent direct command is:

```text
he_grenade_game.exe -GenerateDataset
  -GeneratorConfig="generator-config.json"
  -RenderOffscreen -unattended -nosound -NoSplash -NoVSync
```

Command-line values such as `-Stage=`, `-Episodes=`, `-EpisodeSeconds=`,
`-SeedStart=`, `-WorkerId=`, `-ObservationRate=`, `-Width=`, `-Height=`,
`-StorageFormat=`, `-WebPEffort=`, and `-Output=` override the JSON
configuration.

For centrally prescribed collection, create and verify an immutable plan, then
run the assignments owned by a logical worker:

```text
python Scripts/dataset_controller.py plan <collection-root> --frame-budget <credited-frames> --workers <count>
python Scripts/dataset_controller.py verify-plan <collection-root>
python Scripts/dataset_controller.py plan-distribution <collection-root>
python Scripts/dataset_worker.py <collection-root> --executable <he_grenade_game.exe> --worker-id <id>
python Scripts/dataset_controller.py inventory <collection-root> --write-snapshot
```

The planner loads the versioned Windows per-scenario duration calibration by
default and rejects a frame budget below its computed complete-coverage plus
nested-share lower bound. `plan-distribution` reports the expected frame mixture
before any GPU work begins. The worker passes `-RecipeManifest=<attempt-request.json>` to the
game, finalizes Parquet, validates the shard, and only then publishes an
immutable validated result. Put a file named `STOP` in the collection root to
prevent another assignment from starting after the current one. Use
`activate-reserves` after inventory reconstruction to assign the predefined
reserves for valid semantic failures. `inventory` reconstructs credited frame
and nested distributions from immutable results; semantic-failure frames appear
in produced diagnostics but do not consume X.

Use `-EpisodeIndices=0+18+19+30` to regenerate only specific deterministic
episode indices while preserving their original seeds, IDs, and policy choices.

Focused mission validation can additionally use:

- `-Mission=semi_markov|object_view|contact_recovery|ramp_traverse|hoop_pass`
- `-ObjectViewMode=approach_observe|pass_by|partial_orbit|full_orbit`
- `-CoverageTarget=rectangle|pyramid|sphere|hoop|ramp` for object view
- `-CoverageTarget=rectangle|pyramid|sphere|hoop|ramp|north_wall|south_wall|east_wall|west_wall`
  for contact/recovery
- `-MissionDirection=uphill|downhill` for the ramp
- `-MissionDirection=positive_x_to_negative_x|negative_x_to_positive_x` for the
  hoop
- `-MissionDirection=clockwise|counter_clockwise` for an orbit-mode object QA
  run

The equivalent JSON fields are `mission_override`,
`object_view_mode_override`, `coverage_target_override`, and
`mission_direction_override`. Overrides are recorded in `dataset.json` and are
for QA, replay, and targeted gap filling; ordinary production collection leaves
them empty.

`-MissionReviewSuite` is an inspection-only shortcut that forces exactly 60
episodes: one semi-Markov reference; ten object approach/pass episodes; twenty
object-orbit episodes covering every target, both orbit modes, and both
directions; all nine contact targets; ten ramp episodes; and ten hoop episodes.
For each ramp direction and each hoop direction, the suite visibly demonstrates
forward, backward, strafe-left, strafe-right, and free-attention movement while
covering all three path profiles. Object episodes rotate the four gaze patterns.
Contact episodes rotate approach, recovery, and locomotion-facing profiles. It
records a unique descriptive
`mission_review_slug` on every
episode and frame and marks the run
`inspection_only_mission_review_suite`. The review tool uses those slugs as MP4
filenames. This suite is for human visual acceptance and must not be mixed into
the frame-balanced training collection.

`-TrajectoryShowcase` is an inspection-only trajectory policy with continuous
trajectory visibility and controlled camera motion. Its dataset metadata is
marked `inspection_only_trajectory_showcase`; it must not be mixed into training.

Finalize and validate a production run with:

```text
python Tools/finalize_production_dataset.py GeneratedData/small-pilot
python Tools/review_dataset.py GeneratedData/small-pilot --validate-only
```

To derive review videos, install FFmpeg and omit `--validate-only`, optionally
passing `--ffmpeg="C:\path\to\ffmpeg.exe"`. The converter reads every PNG or WebP
directly from the authoritative shard in frame-index order. It never renders a
second gameplay pass.

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

The accepted working resolution for the initial dataset is 384 × 384 RGB at
20 Hz; 320 × 320 remains the smaller comparison result rather than the current
choice. The compact crosshair and trajectory ribbon use fixed screen-pixel widths
rather than scaling with resolution. The trajectory ribbon is rasterized from
every 120 Hz simulation point with a one-pixel antialiased fringe so its
low-resolution edges remain smooth.

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

Exactly every tenth ordinary production episode retains the canonical fixed
spawn and camera orientation for reference and replay testing. Explicit mission
overrides take precedence during focused QA.

## Meaningful collection-agent design

The agent must not sample every bit independently every frame. That would
overproduce jitter, cancellation, wall-sticking, and visually redundant data.

Use a seeded semi-Markov behavior policy:

1. Select a behavior mode.
2. Select an action combination.
3. Hold it for a sampled number of observation steps.
4. Select a meaningful transition or new mode.

Implemented semi-Markov action-selection weights:

| Behavior | Target |
| --- | ---: |
| stationary | 8% |
| movement only | 30% |
| camera only | 18% |
| movement plus camera | 34% |
| opposing/canceling actions | 5% |
| deliberate collision behavior | 5% |

Implemented hold-duration mixture:

- 25% short: 1–3 steps
- 40% medium: 4–12 steps
- 25% long: 13–40 steps
- 10% very long: 41–100 steps

Important transitions:

```text
idle -> W -> idle
W -> S
A -> D
W -> W+A/W+D -> A/D
camera-left/right -> idle-camera -> opposite camera direction
W -> W+camera-left/right -> camera-left/right
S+A/S+D -> W+D/W+A
pitch-up -> pitch-down -> idle
```

When no transition script is already active and a hold has ended, 30% of
decisions start one of eight short seeded transition scripts covering the
patterns above. Every asymmetric strafe, diagonal, or yaw script has a
per-episode mirrored form; left and right are not fixed by script type. The
mutable semi-Markov stream is initialized from a mixed replay key rather than
directly from consecutive integer seeds. The remaining decisions use the
weighted action and hold samplers. Q is independently present on approximately 45% of semi-Markov V2/V3
action holds; guided V2/V3 missions alternate Q in half-second blocks. V3 E
requests remain edge-triggered and are scheduled every 30–70 observation steps
after an initial 8–24-step offset.

Ordinary semi-Markov contact is allowed briefly because useful sliding,
glancing, and collision footage must remain in the dataset. At the start of each
continuous contact event, the policy stratifies a dwell limit from 4-10
observation frames. Reaching that limit cancels the current held action or
transition script and applies an 8-14-step world-space escape. Wall escapes use
the wall's inward normal; object escapes point away from the object; unknown
contacts fall back to the opposite velocity or camera-forward direction.
World-space escape is converted back into camera-relative WASD on every step and
may therefore change the requested keys while the camera turns. This is
collision recovery inside natural play, not the deliberate contact/recovery
mission described below.

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

Camera pitch is not sampled uniformly. The active Unreal
`PlayerCameraManager` is the authoritative pitch contract; its current limits
are -70 degrees downward and +80 degrees upward. The character, generator,
metadata writer, and validator read those runtime limits rather than maintaining
a second independent range. The finalized automated observation-frame targets
are:

| Camera pitch | Band | Target |
| --- | --- | ---: |
| -15 to +15 degrees | eye height | 75% |
| -40 to -15 or +15 to +40 degrees | moderate | 20% |
| -60 to -40 or +40 to +70 degrees | extreme | 4% |
| -70 to -60 or +70 to +80 degrees | near engine limit | 1% |

The final prescribed collection plan converts these percentages into exact frame
quotas. Selection uses deficits in actual recorded automated observation frames.

Non-eye targets choose downward pitch 58% of the time and upward pitch 42% of
the time. Eye-height samples are concentrated around level with a -2-degree
downward bias. Selection uses deficits in actual recorded observation frames.
The controller stops pitch input at the sampled target, and once a non-eye band
has filled its current frame budget it steers back toward eye height even if a
long movement hold is still active. This prevents a rare steep target from
accidentally becoming several seconds of near-limit footage. Contradictory
ArrowUp+ArrowDown examples are preserved and have zero effective pitch axis.

An arrow action at an engine pitch boundary legitimately produces no additional
camera motion. `dataset.json` records the effective pitch minimum, maximum, and
action rate. Camera metadata uses the normalized post-engine controller
rotation that the action contract updates. `SceneCapture2D` still copies the
engine camera component transform for RGB capture; that component is not used
as action metadata because Unreal can update it at a different point in the
frame. Validation checks every transition against the recorded pitch action,
fixed step, and engine-limit saturation.

A focused regression regenerated only deterministic episode indices 0, 18, 19,
and 30 with `-EpisodeIndices=0+18+19+30`. These were the clean control and the
three cases that exposed the former controller/camera-limit discrepancy. The
lossless-WebP/Parquet result contained 2,404 observations and 2,400 transitions.
All four episodes passed full validation with zero pitch-contract mismatches and
zero maximum numerical error. The formerly inspected transitions were:

| Episode | Transition | Recorded pitch | Pitch action | Expected pitch |
| --- | ---: | ---: | ---: | ---: |
| `w210-e000000` | 0 to 1 | 0 to 0 | 0 | 0 |
| `w210-e000018` | 0 to 1 | -29.069036 to -25.319036 | +1 | -25.319036 |
| `w210-e000019` | 0 to 1 | 37.657810 to 41.407810 | +1 | 41.407810 |
| `w210-e000030` | 369 to 370 | -60 to -60 | 0 | -60 |

The generated MP4 reviews also decoded and played normally. This regression
confirms that the fix preserves Unreal's camera behavior while aligning action
metadata with the same engine-authoritative pitch limits.

Collision behavior:

1. Deliberately approach a wall or object.
2. Continue pressing into it for a stratified 0.20-0.70 seconds.
3. Recover with an enumerated backward, strafe-left/right, or
   diagonal-left/right path.

Contact/recovery has a dedicated 5% share of the final frame mixture and is
balanced across all five objects and all four perimeter walls. Intended action
bits are always recorded even when movement is blocked.

The collector uses privileged position, contact, visibility, mission, and
viewpoint statistics to balance future missions. This does not make those fields
model inputs. A broader spatial visitation report remains part of the separate
production reporting task.

### Frozen centralized production-controller design

The current automatic policy is implemented and remains the Windows reference
and QA path, but it is not the final distributed production scheduler. Production
uses one central controller and simple replaceable workers.

Before production begins, the controller writes an immutable, versioned
collection plan containing the build and container identities, stage and capture
settings, accepted-frame targets, train/evaluation allocation, discrete scenario
catalog, continuous parameter strata, intentional repetition rules, and bounded
reserve recipes. Numerical targets belong to that plan rather than being
hard-coded into the orchestration layer.

The primary user inputs are the requested credited-frame target, worker count,
train/evaluation allocation, and capture/build configuration. Worker count affects
only execution partitioning; it must not change the globally selected recipe set.
The planner calculates the required recipe count from calibrated per-scenario
durations, complete base catalog, highest
feasible continuous refinement depth, reserve recipes, assignment sizes, expected
clean-boundary overage, and recipe-to-worker allocation before execution.

Each prescribed episode recipe contains:

- a globally unique recipe ID and stable replay identity
- train or evaluation split
- mission and complete discrete scenario cell
- continuous stratum indices
- progressive continuous sample ordinal and refinement level
- intentional repetition index
- plan/schema version

The finite discrete cells cross the already implemented mission dimensions:
object target, object-view mode, gaze plan, and orbit direction; contact target,
approach, recovery, and locomotion-facing profile; and ramp/hoop direction, path,
and facing profile. Semi-Markov recipes similarly cover behavior families,
meaningful action/transition scripts, mirroring, hold-duration bands, pitch bands,
and collision behavior. Repeating a cell is permitted only through an explicit
new recipe and repetition index. Generating the same recipe ID twice is a retry
or a duplicate attempt, never new coverage.

Discrete planning is coverage-first. The deterministic selection priority is:

1. represent every top-level mission family
2. represent every value of every categorical dimension
3. cover unseen pairs of categorical values
4. cover every complete joint scenario cell
5. add new continuous samples to the least-covered cells and spatial regions
6. add intentional repetitions while filling accepted-frame deficits

Stable hashing breaks otherwise equal priorities. Every full discrete cell must
be scheduled at least once before an avoidable repetition of that cell. If a
requested accepted-frame target is too small to execute the mandatory discrete
catalog, plan creation fails with the estimated minimum budget and uncovered
requirements. The planner never silently deletes objects, missions, directions,
modes, or joint cells to make a small plan fit.

The canonical recipe/refinement order is independent of requested budget and
worker count. Once the mandatory discrete layer is present, a larger feasible
plan extends the smaller plan with later recipes rather than reshuffling or
replacing its earlier broad samples. Runtime semantic outcomes may activate
predefined reserve recipes, but cannot change the identity of already planned
work.

Continuous coverage is progressive because a continuum cannot be exhausted. The
controller assigns a deterministic nested low-discrepancy sample ordinal across
the normalized continuous parameter vector. Early samples occupy widely separated
regions; later samples fill progressively smaller gaps. A larger budget retains
the same coverage structure and increases refinement rather than replacing broad
samples with a new unrelated draw.

The controller prescribes continuous strata/sample ordinals rather than
duplicating Unreal's geometry code. Unreal maps them into each scenario's
validated safe range, applies the existing named bounded stateless jitter, and
records every realized value. It must not sample broadly and repair invalid
geometry by clamping. A recipe that cannot construct valid geometry is a recorded
configuration/semantic result.

The controller is the sole assignment authority. It precomputes the global
mandatory, weighted-refinement, deterministic-tail, and semantic-reserve recipe set
first, then groups those recipes into assignment blocks and assigns each block to
a physical worker. It records attempts and heartbeats, imports validated results,
updates accepted-frame deficits, dispatches predefined reserve recipes, and
produces the final shard index. Workers never choose production missions and
never coordinate directly with one another. A recipe's logical worker/replay
identity remains fixed across retries; the RunPod or PC which executes an attempt
is recorded separately.

One assignment block produces one shard through one packaged Unreal invocation.
The worker finalizes Parquet, performs complete validation, computes the checksum,
publishes the shard, and writes an immutable result. The assignment size is a
configuration chosen after benchmarking; correctness does not depend on a
specific megabyte target. There is no in-process multi-shard rollover and no
partial-shard salvage in the first production design. The final base window and
all tail work use single-recipe assignments, so a one-worker stop overshoots X by
at most one completed recipe. Multiple workers advance in bounded dispatch waves.

The persistent ledger uses ordinary immutable files rather than a distributed
database:

```text
collection/
  plan/collection-plan.json
  plan/recipes.jsonl
  assignments/assignment-000001.json
  attempts/assignment-000001/attempt-000001/
  results/assignment-000001.json
  shards/
  controller-snapshot.json
  inventory.json
```

Plans, recipes, assignments, attempt records, and validated results are
authoritative. Snapshots and inventories are derived and must be reconstructable
by scanning those immutable records. Workers write only inside their unique
attempt directories. A controller restart therefore cannot lose the definition
of completed or remaining work.

Result handling is explicit:

| Result | Controller action |
| --- | --- |
| valid mission success | resolve the recipe and credit qualifying pre-success frames to its intended coverage bucket |
| valid semantic mission failure | resolve the recipe as failed, retain it as diagnostic data, give it no intended coverage credit, and consider the next predefined reserve recipe |
| technical failure | do not resolve the recipe; retry it after the technical problem is corrected |
| interrupted assignment | reject the partial shard and retry the same assignment in a new attempt |
| duplicate validated attempt | accept one result and mark the other redundant |

Semantic failure is never retried without limit. Every coverage bucket has a
finite base recipe set, finite reserve set, and explicit failure allowance. If
its reserve is exhausted, that bucket becomes `coverage_blocked` while unrelated
work continues. Resolving it requires a documented code/configuration fix or a
new plan amendment; the controller never silently deletes or substitutes the
requirement.

Accepted-frame quotas measure successful qualifying pre-success observations.
Post-success continuation and technically valid semantic failures remain
authoritative stored transition data but are tracked separately. The final
training index selects the approved mixture at clean episode or sequence
boundaries. Semi-Markov episodes have no guided semantic objective; technically
valid completed observations receive their ordinary policy credit.

A graceful stop prevents new assignments and lets active blocks finalize. An
immediate stop leaves an attempt without a validated result, so the controller
can identify and reassign it. At all times the inventory must report planned,
undispatched, active, validated, failed, interrupted, blocked, and remaining work,
plus accepted frames by coverage bucket and every published shard checksum.

The first implementation keeps synchronous `ReadPixels` and runs one generator
process per GPU. Additional throughput comes from separate RunPod workers using
the same packaged build, container, and preferably GPU class. Assignments are
movable and should be distributed so a mission family is not permanently
correlated with one renderer or machine.

#### Coverage-complete toy-plan gate

`Toy` describes scale, repetition, resolution, and continuous refinement—not a
smaller behavioral specification. The Windows controller toy plan must use the
same complete discrete catalog as production, including every object and wall,
all mission families and submodes, both relevant directions, every gaze,
approach, recovery, path, facing, and semi-Markov category, and every complete
joint scenario cell. It uses the minimum intentional repetition and coarsest
progressive continuous layer which can exercise that catalog.

The planner computes the minimum feasible recipe/frame estimate for this gate.
If an arbitrary requested toy budget is below that minimum, it rejects the plan
and reports the missing budget rather than producing a coverage-incomplete toy.
The resulting toy may therefore contain more frames than an informal initial
guess. Cheap capture settings may reduce runtime, but they must not remove a
discrete case or bypass ordinary mission execution and validation.

After mandatory discrete coverage, any additional toy budget selects the next
recipes from the same progressive continuous/refinement order used by production.
Changing the worker count repartitions those recipes only; it cannot change which
recipes the toy plan contains.

### Implemented automatic collection-agent and mission policy

Coverage guidance is implemented and enabled by default in the current automatic
Windows path. It can be disabled with
`coverage_guided: false` or `-NoCoverageGuided`. A guided mission may choose the
episode's initial valid spawn and camera offset. After frame zero it never
teleports, snaps the camera, changes velocity, or applies anything outside the
canonical action bits. Privileged state only selects those bits, balances future
episodes, measures success, and records diagnostics.

The automatic reference policy currently normalizes its five implemented mission
weights to 61.111% semi-Markov, 22.222% object view, and 5.556% each contact,
ramp, and hoop. These are implementation defaults and QA evidence, not the final
distributed collection plan. Automatic mission selection uses the largest
deficit in accepted successful observation frames, not episode counts.
Early-success episodes therefore do not underweight their mission merely because
they are shorter. Every tenth episode remains a forced canonical-spawn
semi-Markov reference unless an explicit QA override is supplied.

`seed_start + episode_index` remains the human-readable replay key, but it is not
fed directly into Unreal's linear random stream. Named parameters use independent
64-bit mixed hashes of dataset seed, worker, episode, parameter name, and sample
index. Finite mission categories are scheduled as explicit cells. Continuous
values cycle through parameter-specific bins using the global replay ordinal
`seed_start + episode_index`, with stateless jitter inside each bin.
Frame-deficit ties are hash-ordered instead of always choosing the lowest-numbered
bucket. Ramp and hoop schedulers exhaust unseen joint cells before returning to
weighted accepted-frame deficits. Contact exhausts the 135 base cells and uses
global accepted-facing deficits when assigning their child facing cells. This
preserves exact replay, prevents independent categorical dimensions from
accidentally correlating, and avoids the consecutive-seed correlation which
previously made the canonical orbit suite one-sided.

Failed diagnostic missions remain recorded but do not advance balancing
counters. Guided starts use stratified yaw offsets in [-12, 12] degrees and
pitch offsets in [-6, 6] degrees.

#### Object view/navigation

Object-view frames are balanced equally across the rectangle, pyramid, sphere,
hoop, and ramp.
The scheduler first balances the submode by accepted frames, then chooses the
largest-deficit complete scenario cell inside that mode. There are 120 cells:
20 approach, 20 pass-by, 40 partial-orbit, and 40 full-orbit combinations. Each
cell fixes target and gaze pattern; orbit cells additionally fix direction.
This hierarchical choice prevents short runs from becoming blocks of only one
mode while still converging to the frozen frame targets:

| Submode | Object-view share | Requirement |
| --- | ---: | --- |
| approach and observe | 40% | reach the sampled view point and keep the target visibly verified for 0.5–1.5 seconds |
| pass by | 35% | traverse a long chord while accumulating 0.3–1.0 seconds of verified visibility |
| partial orbit | 20% | visit 4-7 seeded position azimuth bins |
| full orbit | 5% | visit all 12 position azimuth bins |

Camera gaze is separate from translation. The scenario still records one of
four reproducible plan templates—target center, target offset, travel direction,
or roam/reacquire—but scheduling feedback uses the actual intent recorded on
every accepted observation rather than assuming the template label equals the
realized footage:

| Realized gaze intent | Object-view frame target | Behavior |
| --- | ---: | --- |
| target center | 40% | steer toward the object's predefined visual center |
| target offset | 25% | steer toward a seeded offset point on or near the object |
| travel direction | 20% | look along the current world-space path |
| survey point | 15% | look at a bounded nearby arena point before reacquisition |

All gaze phases use named stateless parameters and ordinary arrow-key actions;
the camera never snaps after spawn. Offset points scale with the selected
object's collision radius. Survey points are bounded to the playable sampling
area. Roam/reacquire starts at a hash-selected phase rather than always starting
at target center. Only approach/observe forces target-center reacquisition at
its final observation point. Pass-by and both orbit modes keep their scheduled
independent gaze through completion.

Orbit radius is sampled directly inside the intersection of the target-relative,
collision-safe, and arena-safe range. It is never sampled broadly and then
clamped. Eight radius strata and bounded waypoint jitter prevent maximum-radius
spikes. Orbit start bins cover all twelve 30-degree sectors. Partial orbits visit
4-7 bins. Full orbits use 13 path waypoints: twelve distinct azimuth bins plus a
return to the starting azimuth. Clockwise and counter-clockwise are explicit
equal cells rather than an unchecked Boolean draw.

Approach and pass-by geometry uses bounded stratified candidates. A candidate is
accepted only if both endpoints are inside the sampling arena and its segment
clears the learning objects; no endpoint is repaired by clamping to +/-1450 cm.
Failure to construct valid geometry is an explicit generator error rather than
silently retaining the final invalid attempt. Approach starts add 430-760 cm to
the observation radius. Pass-by starts and goals lie 620-1,000 cm on opposite
sides of the target; lateral clearance is the target collision radius plus
120-310 cm.

Translation and camera gaze are independent. The controller continuously
converts the world-space path direction into camera-relative WASD, allowing
natural forward motion, strafing, or backpedaling while the crosshair looks
elsewhere. It does not pause translation merely because the target is outside
the camera view.

Orbit success is based only on the player's visited position azimuth bins.
Separately, a visibility bin is recorded when the object's visual center is
within 42 degrees of camera yaw and 38 degrees of camera pitch and the trace is
unobstructed or hits that object. This preserves measurable visibility coverage
without teaching that orbiting requires camera lock.

#### Contact and recovery

Contact frames are balanced equally across nine targets: all five learning
objects and the north, south, east, and west walls. The base scheduler has 135
target x recovery x approach cells: nine targets, five recovery styles, and
direct/glance-left/glance-right approaches. Each base cell has five
locomotion-facing children, producing 675 complete categorical cells. The five
facing targets are 35% forward, 15% backward, 15% strafe-left, 15% strafe-right,
and 20% free attention. Object approaches also stratify eight angular sectors;
wall approaches stratify eight along-wall bands. Candidate starts and recovery
goals must lie inside the arena, and approaches crossing a non-target learning
object are rejected rather than clamped.

Wall contact points use `wall_center + inward_normal * 80 cm`, placing them on
the arena side of the wall. The agent presses through the expected surface
point, holds the commanded contact for 0.20-0.70 seconds, and then follows the
scheduled recovery style. Recovery first moves 280 cm outward from the target
to clear collision before continuing to the styled recovery goal. Recovery
separation is held for 0.25-0.80 seconds.

At least two observations must verify contact with the selected actor. Brief
collision-contact flicker does not erase an otherwise continuous commanded hold,
but moving over 220 cm from the expected contact point resets the attempt.
Recovery requires loss of the selected contact, over 180 cm separation from the
contact point, and arrival within 180 cm of the sampled recovery goal. The
generic no-progress watchdog is disabled only during the intentional hold phase.
The selected locomotion-facing profile is measured on the initial approach leg
until first verified contact; later reacquisition after contact flicker does not
rewrite that completed measurement. Free-attention episodes choose among
objective-center, objective-offset, travel/reacquire, and scan/reacquire styles.
Movement always follows the world-space path through camera-relative WASD.

#### Ramp traversal

Ramp frames are split 50/50 between uphill and downhill traversal. The complete
scheduler contains 30 direction x path x facing cells:
two directions, center/diagonal-left-to-right/diagonal-right-to-left paths, and
the same five locomotion-facing profiles used by contact. Center versus the two
diagonal paths target 50/25/25 accepted frames; facing targets are
35/15/15/15/20. Every allowed cell is visited once before weighted repetition.
Uphill starts sample ground positions 620-930 cm from the low-side
approach, lateral entry and exit offsets, and a far-side goal. Because the
standalone ramp has no upper platform, downhill episodes validly start the
capsule on the sampled high ramp surface, then descend to a randomized far-side
ground goal.

The primary objective requires evidence that the capsule mounted the ramp,
crossed beyond the
appropriate far edge (X below -340 cm uphill or above +340 cm downhill), and
remained within the ramp's lateral corridor. The mission then continues naturally
to its sampled far-side goal and holds the endpoint briefly before success.
For non-free profiles, at least five moving observations within the ramp region
must be recorded and at least 45% must realize the requested movement-relative
camera angle. Forward is within 45 degrees, backward is at least 135 degrees,
and the two strafes occupy the corresponding 90-degree sectors.

#### Hoop passage

Hoop frames are split 50/50 between passage directions. Its 30 complete cells
cross both directions, center/oblique-left-to-right/
oblique-right-to-left paths, and all five locomotion-facing profiles. The path
and facing shares, unseen-cell pass, and realized-facing gate match the ramp
policy. Each episode performs exactly one natural passage—never rapid repeated
reversals—with start and goal distances independently sampled from 520-650 cm.
Center paths use offsets within 18 cm; oblique paths enter 75-110 cm to one side
and leave 75-110 cm on the other.

The primary passage is credited by interpolating the exact crossing point
between the observations on either side of the hoop plane at X = 700 cm. That
interpolated point must lie within 90 cm of the opening center laterally and
between Z = 80 and 145 cm. Endpoint sampling can no longer falsely accept or
reject a fast crossing. The agent continues to the sampled far-side goal and
completes a short post-objective hold before the episode succeeds.

#### Termination, recording, and failure handling

Guided completion now has two distinct phases. First, after the primary
objective, the agent must arrive within the mission's final-goal radius and
remain there for a stratified 0.20-0.65-second post-objective hold. That
establishes mission success. Success is then latched and the episode records a
separate stratified 0.75-1.50-second post-success rollout instead of cutting on
the success frame.

The first 3-6 post-success actions preserve the exact mission-completing action
after removing E. The remainder uses one of six explicitly enumerated,
replayable styles:

- `continue`
- `gentle_turn`
- `glance_reacquire`
- `strafe_blend`
- `ease_and_observe`
- `drift_and_settle`

Style assignment cycles through all six with a replay-key offset. Duration is
stratified, and left/right mirroring alternates with a replay-key offset. The
styles make held, coherent changes rather than drawing new bits on every frame.
They may continue to view the mission target, look beside it, lose it
temporarily, or leave it entirely out of view; orbit completion never implies
camera lock. Any contact during this rollout bypasses the ordinary dwell
allowance and immediately requests a world-space escape.

Mission credit is frozen at the success observation: post-success frames do not
alter visible/visited bins, facing ratios, objective counters, final-goal
credit, or accepted-frame balancing. They remain authoritative observations and
transitions for training, and the episode separately records distance to goal at
success and distance at the final tail frame. This prevents abrupt endings
without allowing the extra natural play to rewrite which mission was completed.

Before success, less than one centimeter of displacement for one second while
movement is commanded triggers the generic no-progress failure; the configured
episode time limit produces a mission timeout. Failed attempts remain in the
shard as identifiable diagnostic episodes and are excluded from the accepted
training mixture unless a later experiment deliberately includes them.

Frames record mission, target, object mode, gaze pattern, current gaze intent
and world target, contact phase, contact approach/recovery, locomotion-facing
profile, ramp/hoop path profile and direction, visibility, visited and visible
azimuth masks, distance bin, pitch band, waypoint, verified counters, running
facing-match counters and camera/movement angle, interpolated hoop crossing,
outcome, mission phase, success-frame index, post-success progress/style,
natural-play contact streak/limit, escape-active state, and no-progress state.
Episode records include the complete seeded gaze plan, joint scenario indices,
all sampled path/facing parameters, achieved facing ratio, post-success
duration/style, success and final distances, natural-play escape count, maximum
contact run, and termination reason. `dataset.json` reports accepted successful
observation frames by mission, object submode, object gaze pattern and actual
intent, orbit direction, object target, contact target/approach/recovery/facing,
ramp direction/path/facing, hoop direction/path/facing, free-attention camera
style, pitch band, and a separate total of post-success observations.
This is the contract used to assemble by frames rather than by episode counts.

#### Final policy validation

The finalized-agent schema is now `*-preflight-10` and the collection policy is
`training_frame_balanced_final_agent_v5`. It passed the Unreal Editor Development
build after the post-success and natural-play escape changes. The validator
retains all schema-v9 mission checks and additionally requires an internal
success observation, an exact 0.75-1.50-second tail, one of the six valid styles,
latched success on every tail frame, exact post-success counters, frozen mission
counters, a separate success distance, internally consistent semi-Markov
contact streaks, and a maximum natural-play contact run no longer than one
second.

The earlier schema-v9 focused production-policy results remain the core-mission
evidence:

- Ramp: 60/60 successes, 1,683 transitions, 1,743 observations, all 30
  direction x path x facing cells, and a 1.0 minimum non-free realized-facing
  ratio.
- Hoop: 60/60 successes, 1,696 transitions, 1,756 observations, all 30
  direction x path x facing cells, a 1.0 minimum non-free realized-facing
  ratio, and every interpolated crossing inside the physical opening.
- Contact: 135/135 successes, 6,367 transitions, 6,502 observations, all 135
  target x recovery x approach base cells exactly once, all five facing
  profiles, and a 1.0 minimum non-free initial-approach facing ratio.
- Object view: 120/120 successes, 4,603 transitions, 4,723 observations, all
  targets/modes/gaze templates and both orbit directions, 61 weighted complete
  scenario cells, and 32 distinct orbit radii. Actual gaze-intent frames were
  40.93% center, 24.96% offset, 19.67% travel, and 14.44% survey against the
  40/25/20/15 targets.

The schema-v9 mixed-policy soak used 200 episodes, replay keys 38001-38200, a
ten-second cap, and 64 x 64 RGB for fast logic QA. Its authoritative shard
contained 14,774 transitions and 14,974 observations and passed checksum, count,
continuity, image, mission, tail, geometry, realized-behavior, and run-level
diversity validation. All 154 guided missions succeeded with zero failures.
Accepted automated frame shares were 61.75% semi-Markov, 22.23% object view,
5.40% contact/recovery, 5.30% ramp, and 5.32% hoop. Actual object gaze intents
were 41.36% center, 24.45% offset, 19.86% travel, and 14.33% survey. Survey
targets stayed within 1,394.6 cm on both arena axes, with no exact boundary
coordinate. These finite-run results do not replace the frozen quotas; the
accepted-frame controller converges as collection grows.

The final 384 x 384 schema-v10 inspection shard
`Saved/PostSuccessMissionReview384Final` contains 3,886 transitions and 3,946
observations in 60 episodes. All 59 guided missions succeeded and contributed
1,333 post-success observations. Tail lengths cover the full 15-30-frame range
at 20 Hz, with a 23-frame median. The styles occur 10 times each except
`drift_and_settle`, which occurs 9 times because there are 59 guided episodes.
Its semi-Markov reference invoked five bounded collision escapes and had a
nine-frame maximum contact run.

The review tool generated all 60 uniquely named MP4s. A separate 16-page audit
contains every frame of the semi-Markov reference plus six pre-success context
frames and every post-success frame from every guided mission. Visual inspection
found no corrupt, discontinuous, or abrupt success transition. Target gaze
varied as intended, including object-centered, offset, intermittent, and
off-object orbit views. Several tails ended while the camera faced nearby
geometry; metadata showed continued travel rather than stationary wall contact,
and actual contact observations selected escape actions.

A separate final-code semi-Markov stress shard,
`Saved/NaturalPlayEscape20`, contains 4,800 transitions and 4,820 observations
across 20 episodes. It invoked 70 escape events. Contact occupied 646
observations, or 13.4%, inside the intended 8-15% range. The maximum continuous
contact run was 16 frames (0.8 seconds at 20 Hz), versus the prior observed
57-plus-frame wall scrape, and every episode remained below the validator's
one-second ceiling. Both final shards pass checksum, continuity, image, schema,
mission, post-success, and collision-escape validation.

Two independent ten-episode replay runs using replay keys 39001-39010 produced
the same 663 tar member names and byte-identical payloads. Episode, transition,
frame, and RGB records all matched exactly. Global replay stratification and
per-parameter mixing therefore add coverage without weakening exact replay.

### Deferred human-play collection

Human keyboard capture is not part of the first production implementation or a
gate for automated Movement V1 collection. If added later, it must be introduced
as a separately versioned source, preserve the same 20 Hz RGB/action/state
alignment and authoritative shard contract, and remain identifiable by collection
policy and session ID. Whole sessions—not neighboring clips—must remain within a
single train or evaluation split. No current automated quota reserves space for
human data.

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
- collection mission, scenario index, object-view mode, object-gaze pattern,
  orbit/ramp/hoop direction, contact approach/recovery style, ramp/hoop path
  profile, locomotion-facing profile, free-attention camera style, and stateless
  sampled mission parameters
- mission-required, primary-objective, accepted-for-balancing, and
  mission-success flags
- achieved/required position-bin masks, separately achieved visible-bin masks,
  verified contact hold, traversal, and recovery counts
- facing-moving and facing-matched frame counts, realized match ratio, and
  interpolated hoop crossing Y/Z
- required/achieved post-objective steps and final distance to the completion
  goal
- mission-success frame index; required/achieved post-success steps; exact
  post-success observation count and style; distance at success versus distance
  on the final tail frame
- natural-play collision-escape count and maximum consecutive contact steps
- termination reason
- shard and checksum information

The prescribed-controller schema extension must additionally record the
collection-plan ID/version, recipe ID, assignment ID, attempt ID, stable logical
worker/replay identity, physical executor ID, train/evaluation split, prescribed
discrete cell, prescribed continuous stratum indices, intentional repetition
index, progressive sample ordinal/refinement level, technical result, semantic
result, coverage-credit decision, and any plan-amendment ID. Plan, recipe,
assignment, attempt, executor, split, scenario, repetition, and progressive
sample fields are now present in prescribed episode Parquet rows. Technical and
semantic outcomes plus coverage credit are stored in the immutable controller
result, where they can reflect finalization and validation rather than an
in-progress Unreal episode. Plan amendments remain deferred until one is
actually required; the automatic Windows schema-v10 path remains backward
compatible and leaves prescribed fields null.

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
- active collection mission, target, object-view mode, gaze pattern and intent,
  gaze world target, orbit direction, contact phase/approach/recovery style,
  locomotion-facing profile, free-attention camera style, ramp/hoop path and
  direction, waypoint, visited/visible coverage bins, and pitch band
- running realized-facing counters, current movement-versus-camera yaw delta,
  and interpolated hoop crossing diagnostics
- primary-objective and post-objective progress
- mission phase (`mission`, `success`, or `post_success`), mission-success frame,
  post-success progress and style
- natural-play contact streak and sampled limit, plus whether the action that
  produced the observation was a forced escape
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

## Production storage format

The generator supports two selectable formats:

- `png_jsonl`: the retained reference/preflight path
- `webp_parquet`: native lossless-WebP observations followed by typed Parquet
  finalization

Set `storage_format` in the generator config or pass
`-StorageFormat=png_jsonl|webp_parquet`. The production path also accepts
`webp_lossless_effort` or `-WebPEffort=0..9`; the measured default is `0`.

Target structure:

```text
movement_v1/
  dataset.json
  checksums.md5
  shard-000000.tar
  shard-000001.tar
  ...
```

Each completed production-v1 tar contains:

- lossless WebP RGB frames
- `frames.parquet`
- `transitions.parquet`
- `episodes.parquet`
- `manifest.json`; the shard MD5 is recorded beside it

WebDataset-style tar shards avoid millions of filesystem entries while retaining
exact frame access. Ordinary MP4 files may be produced separately for human
review, but are not the authoritative training source because temporal codecs
complicate exact frame access and introduce inter-frame artifacts.

After the packaged generator exits successfully, finalize and validate a
production run:

```powershell
python Scripts/finalize_production_dataset.py <dataset-directory>
python Scripts/review_dataset.py <dataset-directory> --validate-only
```

`Scripts/benchmark_storage_formats.py` runs both formats with identical stage,
seeds, episodes, duration, resolution, and observation rate. It times capture
and finalization separately, validates both outputs, and calls
`Scripts/compare_dataset_formats.py` to compare every normalized metadata record
and every decoded RGBA pixel.

The centralized design keeps the proven one-shard-per-Unreal-invocation format.
The controller groups prescribed recipes into an assignment of configurable
size, and the worker publishes that shard only after finalization and validation.
Interrupted partial shards are retried as whole assignments. In-process
target-size rollover and partial-shard continuation are deliberately not required
for the first production dataset. Review MP4s already decode either PNG or WebP
members from the authoritative tar.

### PNG/JSONL versus lossless-WebP/Parquet benchmark

The paired packaged-build benchmark used one process, Movement V1 forced
semi-Markov play, 32 episodes of 30 seconds, seeds 61001-61032, 384 x 384 RGB at
20 Hz, and the same worker/configuration for both formats. That produced 19,200
transitions and 19,232 observations representing 960 simulated seconds.

| Metric | PNG + JSONL | lossless WebP effort 0 + Parquet | Production change |
| --- | ---: | ---: | ---: |
| Native capture | 692.947 s | 437.223 s | -36.90% |
| Parquet finalization | 0 s | 5.371 s | +5.371 s |
| End-to-end generation | 692.947 s | 442.593 s | **-36.13% (1.566x faster)** |
| Simulated seconds / wall second | 1.385 | 2.169 | +56.57% |
| Shard bytes | 2,794,387,968 | 2,002,821,120 | -28.33% |
| Complete dataset-directory bytes | 2,794,391,532 | 2,002,825,925 | **-28.33%** |
| Bytes / observation | 145,299 | 104,140 | -28.33% |
| Validation | 12.719 s | 13.696 s | +0.977 s |

The Parquet members total only 636,972 bytes
(`frames.parquet` 589,281, `transitions.parquet` 30,122, and
`episodes.parquet` 17,569), so image encoding dominates both storage and
generation time. Exhaustive comparison decoded all 19,232 PNG/WebP pairs to
RGBA and compared every normalized frame, transition, and episode record. They
were identical; the aggregate decoded-RGBA SHA-256 was
`dcbc53c837cf15f7397a8603d698e2440bac711433185cfea825b163dadf0218`.

An initial higher-effort lossless-WebP run reached 1,597,987,496 bytes
(42.81% below PNG) but took 3,703.748 seconds end to end, or 5.35x the PNG time.
That tradeoff was rejected. Effort 0 is therefore the production default: it is
about 25.33% larger than that higher-effort WebP output, but 88.05% faster and
also faster than PNG.

WebP lossless effort changes encoder search time and compressed size, not image
quality. Effort 0 and the higher-effort result both decode to exactly the same
integer RGB pixels as PNG. The exhaustive comparison above confirms this for
every benchmark observation. Effort 0 was selected because it preserves that
lossless guarantee while improving both generation throughput and storage size
relative to PNG; higher effort spends much more CPU to find a smaller encoding
of those same pixels.

### Training-time representation

PNG and WebP are storage encodings, not the representation consumed by the
model. A training loader reads a WebP member from its tar shard, decodes it to a
`uint8` RGB array shaped `[height, width, 3]`, converts it to a PyTorch tensor
shaped `[channels, height, width]`, and then applies the model's selected numeric
dtype and normalization. Because lossless PNG and lossless WebP decode to the
same RGB integers, the same loader preprocessing produces identical training
tensors.

Parquet supplies the aligned frame, action, episode, and privileged-state
records. A one-step training example therefore pairs the decoded RGB tensor for
`observation[t]` and the action tensor for `action[t]` with the decoded target
tensor for `observation[t+1]`. Multi-step examples extend that same aligned
sequence; the compressed image format is no longer relevant after decoding.

## Packaged build-first workflow

The Unreal Editor is used for authoring and visual inspection. Dataset correctness
is judged using the packaged executable.

Audited workflow state:

| Step | State | Current evidence or remaining work |
| --- | --- | --- |
| Freeze and visually accept the fixed V1 environment | complete | commits `340d6c5` and `37cb24d`; packaged visual and resolution pilots |
| Implement the replayable action and mission policy | complete | global replay stratification, mixed per-parameter hashing, mirrored semi-Markov scripts, accepted-frame balancing, complete scenario cells, realized-facing gates, interpolated crossings, bounded natural-play contact escape, and six post-success rollout styles in schema v10 |
| Implement fixed-step action/state/RGB synchronization | complete preflight | fixed observation step, aligned `N` transitions and `N+1` observations, packaged deterministic replay |
| Package a Win64 Development generator | complete preflight | bounded packaged runs and automatic exit validated |
| Validate preflight alignment and shard integrity | complete | PNG/JSONL validator, checksums, review MP4s, comparison runs, and mission-validation runs |
| Benchmark safe processes on the desktop GPU | complete; parallel result rejected | three workers on the same RTX 4060 were far slower than one |
| Freeze mission semantics and coverage dimensions | complete | 120 object cells; 135 contact base/675 full cells; 30 ramp and 30 hoop cells; explicit bidirectional paths; five locomotion-facing profiles; six post-success styles; schema-v10 focused, stress, video, and all-frame QA |
| Implement lossless WebP and Parquet tar payloads | complete production v1 | native libwebp observations, explicit PyArrow schemas, tar append finalization, checksums, validation, review, and exact equivalence tool |
| Run the production-format 32-episode pilot | complete | paired PNG/JSONL and WebP/Parquet run: 19,232 observations, identical seeds/settings, exhaustive decoded-pixel and metadata equivalence |
| Freeze centralized prescribed-controller architecture | complete design | immutable plans and recipes; controller-owned assignments; one assignment/one shard; reconstructable ledger; bounded semantic failure; whole-shard technical retry |
| Update README to the controller architecture | complete design checkpoint | documentation is committed before generator/controller implementation |
| Implement prescribed recipe input on Windows | complete | packaged generator accepts an assignment manifest and consumes exact per-episode mission/scenario/replay identities while retaining automatic mode |
| Implement controller, worker wrapper, and ledger | complete on Windows | immutable plan/recipe/assignment/claim/attempt/result records; inventory reconstruction; semantic failure resolution; reserve activation; duplicate suppression; whole-shard retry; graceful stop |
| Run the Windows controller toy pilot | complete | original 887-cell pass plus a 300,177-frame budget run: 887/887 cells, 25 validated assignments, only 177 clean-boundary overshoot, zero semantic failures, zero technical failures, and a real graceful-stop/resume migration |
| Freeze numeric allocation policy | complete on Windows | user supplies X; calibrated 55/20/15/5/5 mission shares and frozen nested shares; current complete-coverage/share feasibility floor is 344,534 credited frames |
| Package and validate Linux x86-64 | complete | UE 5.8 v26 Clang cross-build and Development BuildCookRun succeed; the corrected package initializes Vulkan SM6 on an RTX A6000; the unchanged 300k plan produced 300,015 observations across 27 independently revalidated WebP/Parquet shards, 887/887 cells, and zero failures |
| Add complete distribution reports | partial | controller-native planned and credited mission/target/mode/gaze/direction/path/facing reports are complete; broader action, spatial, contact-duration, duplicate, and storage reports remain |
| Scale collection | Linux parity gate complete; reporting/trainer gates remain | Windows and Linux both completed the immutable 300k controller plan; larger RunPod rollout remains gated on the remaining reports, trainer smoke test, storage/backup choice, and explicit budget authorization |

The current packaged build accepts the existing automatic arguments plus
`-RecipeManifest=<assignment.json>`. A prescribed manifest supplies plan,
assignment, attempt, split, logical-worker, physical-executor, recipe,
episode-index, exact discrete-scenario, repetition, refinement, continuous
sample, and capture-configuration fields. The generator rejects duplicate recipe
IDs/episode indices within an assignment. `Scripts/dataset_controller.py` owns
global recipe uniqueness, planning, assignment, inventory, and reserve
activation; `Scripts/dataset_worker.py` owns attempt claims, packaged execution,
finalization, validation, and immutable result publication. The central
controller—not each generator process—owns global assignment and coverage
accounting.

### Current same-GPU worker benchmark

This was a generator-throughput/concurrency experiment, not model training or a
custom GPU-compute implementation. The packaged synchronous PNG preflight ran
through D3D12/SM6 on this desktop:

- CPU: Intel Core i5-12400F
- GPU: NVIDIA GeForce RTX 4060 with 7,956 MB reported dedicated memory
- workload: trajectory V2, 384 × 384, 20 Hz, one 20-second/400-transition
  semi-Markov episode, seed 6111

The identical workload was measured once alone and then in three concurrent
processes on the same GPU:

- one worker: about 19.5 seconds after generator configuration and about
  22.3 seconds from process launch
- three concurrent workers: about 396.8 seconds to finish 60 aggregate simulated
  seconds
- aggregate three-worker throughput: 0.151 simulated seconds per wall second
- scaling versus one worker: 0.147×
- all four resulting shards passed validation

Three Unreal rendering/capture processes therefore showed severe negative
scaling on this specific PC. They competed for the same RTX 4060 while each
worker also blocked on synchronous GPU readback and performed lossless PNG
compression. The experiment did not demonstrate useful same-GPU parallelism.
Collection on one machine/GPU must run one generator process at a time. The
failed same-GPU experiment does not argue against separate RunPod workers with
separate GPUs. The centralized design scales across machines while retaining the
synchronous reference capture path; asynchronous readback is deferred and is not
a gate for the first production dataset.

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
- a finalized-agent success flag is not supported by its recorded visible-hold,
  distinct-bin, contact/recovery, ramp, or hoop counters
- a success lacks its primary-objective flag, final-goal arrival, or required
  post-objective tail
- a schema-v10 guided success is the final observation rather than an internal
  success frame followed by exactly its required 0.75-1.50-second rollout
- a post-success phase, counter, style, latched-success flag, frozen mission
  counter, or success-distance record is inconsistent
- a schema-v10 semi-Markov contact streak is inconsistent with recorded contact
  or its realized maximum continuous contact run exceeds one second
- a full orbit does not contain twelve distinct-bin waypoints plus its return
  waypoint
- a successful contact, ramp, or hoop mission does not record the selected
  locomotion-facing profile, valid counters, and the required realized match
- a successful hoop mission lacks an interpolated crossing point inside the
  opening
- a review suite does not contain all 60 unique expected mission slugs, both
  orbit directions for every target/orbit-mode pair, and all five facing
  profiles in each ramp and hoop direction
- a sufficiently large run is one-sided in orbit direction, misses too many
  complete contact/ramp/hoop scenario cells, omits a path/facing profile, or
  shows a repeated maximum-radius clamp spike
- a sampled mission start, goal, or recovery goal lies on the old +/-1450 cm
  repair boundary

The centralized prescribed pipeline additionally fails validation if:

- a result references an unknown plan, recipe, assignment, or attempt
- plan creation accepts a budget below mandatory complete discrete coverage
- the selected global recipe set changes only because worker count changes
- a mandatory discrete cell is omitted before an avoidable repetition is added
- progressive continuous sample ordinals repeat or regress without an explicit
  prescribed repetition
- a physical executor changes a recipe's stable logical replay identity
- two accepted results claim the same recipe ID
- a shard contains an episode not listed in its assignment
- a prescribed categorical cell or continuous stratum differs from realized
  metadata without an explicit recorded configuration error
- a technical or interrupted attempt is credited toward coverage
- a semantic failure receives intended guided-mission coverage credit
- a semantic-failure reserve exceeds its plan-defined bound
- train and evaluation recipes share a replay identity
- controller progress cannot be reconstructed from immutable records
- a published result lacks full validation and a matching shard checksum

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

1. Run the coverage-complete prescribed-controller toy plan on the working
   Windows baseline; it uses every discrete cell with minimal repetition and
   coarsest progressive continuous refinement.
2. Reproduce that toy plan with the packaged Linux build on RunPod.
3. Generate approximately 10,000 frames for trainer, schema, controller,
   interruption, and report smoke tests.
4. Implement coverage, pitch, action, collision, duplicate-frame, controller,
   attempt, and storage reports.
5. Freeze the serious Movement V1 accepted-frame target in the immutable
   production collection plan.
6. Reserve the chosen held-out share using disjoint prescribed recipe identities.
7. Train and inspect multi-step rollouts before extending Movement V1 toward one
   million frames.
8. Add V2 trajectory data and then V3 throw data only after the preceding
   curriculum gate passes.

At 20 Hz, 500,000 frames would represent approximately 6.94 simulated hours, but
that value is now a planning reference rather than a frozen production target.
The measured WebP/Parquet benchmark below, not the older PNG estimate, must be
used when sizing the eventual RunPod plan and persistent storage.

The working training budget is approximately USD 100 using RunPod for storage
and training. This rules out retaining thousands of hours or multi-terabyte
lossless datasets for the first iteration. Collection must be coverage-driven,
centrally accounted, interruption-safe, and evaluated incrementally. Each GPU
uses one synchronous generator process; scale comes from separate centrally
controlled RunPod workers.

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

### Active implementation checkpoint

The authoritative storage conversion is complete for one-shard production-v1
runs: native lossless WebP, typed Parquet, manifest/checksum updates, validation,
review, benchmarking, and exact cross-format comparison are implemented on
Windows. The automatic schema-v10 mission behavior is also implemented and
validated. It uses named stateless parameters, enumerated categorical cells,
stratified valid geometry, measurable outcomes, canonical action bits only, no
mid-episode state correction, generic no-progress/time-limit failures,
accepted-frame balancing, and natural endpoint-tail completion.

The Windows baseline now implements the frozen centralized production
architecture while retaining the synchronous one-shard generator as the trusted
capture baseline. The first complete 20 Hz prescribed toy validated all 887
discrete cells in one shard with 54,868 authoritative observations and zero
semantic failures. The budget-controller run then generated 300,177 credited
64x64 observations across 25 validated assignments with 177 frames of overshoot,
887/887 credited cells, and zero semantic or technical failures. A graceful stop
after assignment 23 and immutable resume under corrected near-target assignment
boundaries preserved all validated work. That run supplied per-scenario duration
calibration; a feasibility-aware 350,000-frame plan now predicts 350,041 frames
at 55.010/19.992/14.990/4.998/5.010 mission shares. Interruption/retry,
deliberate coarse-rate semantic failure, reserve activation, graceful stop,
duplicate suppression, canonical-prefix stability, and inventory reconstruction
are all covered.

Immediate implementation order:

1. Back up or migrate the verified Linux collection from pod-local persistent
   storage to portable network/object storage and retain the immutable plan,
   manifests, checksums, inventory, and verification report independently.
2. Complete distribution reports and a small trainer/model smoke test using the
   independently validated Windows and Linux collections.
3. Compare platform-level credited distributions and selected same-recipe
   replays; require semantic parity, not byte-identical cross-platform rendering.
4. Choose production X at or above the recorded feasibility floor, then
   authorize only the staged data-volume rollout above.

Deferred work which is not a first-dataset gate includes asynchronous GPU
readback, in-process multi-shard rollover, partial-shard salvage, multiple
processes on one GPU, and human keyboard capture. Complete distribution reports
and the trainer/model smoke test remain required before serious collection.
