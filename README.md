# HE Grenade Game Curriculum Dataset Generator

This repository plans, certifies, records, validates, and renders deterministic
first-person world-model data in the fixed HE Grenade Game arena.

## Current authoritative state

There are two deliberately separate datasets:

- **Movement V1:** movement and camera control, persistent semi-Markov play,
  object viewing, contact recovery, ramp traversal, hoop passage, and static
  no-input controls. Grenade actions are disabled.
- **Trajectory/Throw V2:** persistent semi-Markov play with Q/E enabled and 62
  prescribed grenade mission types using canonical preview and grenade physics.

The approved campaign contains exactly **3,333,333 planned credited frames**:

| Stage | Semi-Markov | Missions | Total |
| --- | ---: | ---: | ---: |
| V1 | 777,778 | 333,333 | 1,111,111 |
| V2 | 1,555,555 | 666,667 | 2,222,222 |
| **Combined** | **2,333,333** | **1,000,000** | **3,333,333** |

The campaign was first planned, structurally verified, runtime/physics
certified, resolved, and recertified on Windows with Unreal Engine 5.8. It was
then rebuilt, independently planned, certified, and recorded on Linux. The
authoritative captured data is on the RunPod EU-RO-1 network volume:

```text
/workspace/LinuxCampaign3333333/
|-- resolution/v1/resolved-collection/
`-- v2-schema14-b49ddd0/resolution/resolved-collection/
```

The completed production inventory is:

| Stage | Validated assignments | Credited observations | Stored observations |
| --- | ---: | ---: | ---: |
| V1 | 192 | 1,115,586 | 1,152,179 |
| V2 | 171 | 2,222,222 | 2,229,369 |
| **Combined** | **363** | **3,337,808** | **3,381,548** |

V1 stops only between immutable assignments, so its realized credited count can
exceed the 1,111,111 planning target; all 902 required cells are covered and the
budget is complete. V2 hit its target exactly. All complete valid observations
remain training data.

The historical Windows planning/certification evidence is:

```text
Artifacts/WindowsCampaign3333333-20260813-112500/
```

Its combined report is
`windows-plan-verification-report.json`. Generated artifacts are local evidence,
not repository source, and may not exist in another clone.
`CAMPAIGN_3333333.md` preserves the source-controlled human-readable checkpoint.

### Historical Windows certification result

- V1 original guided recipes: 6,339 passed and 5 rejected out of 6,344.
- All five V1 rejected slots received a distinct same-mission/same-scenario
  replacement; every replacement passed on attempt 1.
- Final V1 resolved plan: 6,344/6,344 guided recipes passed.
- V2 original prescribed missions: 4,940/4,940 passed.
- Final V2 resolved plan: 5,459/5,459 total recipes passed.
- Unresolved recipes: zero.
- Final campaign report: `complete: true`.

The five original V1 failures were four hoop timeouts and one contact-recovery
no-progress result. The complete rejected recipes, engine errors, candidates,
attempts, strategies, and accepted replacements are preserved in
`resolution/v1/resolution-report.json`.

### Completed Linux production result

- Linux V1 inventory: complete, 192 validated assignments, 1,115,586 credited
  observations, 1,152,179 stored observations, 902/902 cells, and zero technical
  or semantic failures.
- Linux V2 candidate: `v2plan-19d90e5db02a2104`.
- Linux V2 resolved plan: `resolved-856bc5b5c7d32e77`.
- V2 original certification: 4,940/4,940 prescribed recipes passed with zero
  rejects or replacements.
- V2 final recertification: 5,459/5,459 recipes passed.
- V2 recording: 171/171 assignments, exactly 2,222,222 credited observations,
  2,229,369 stored observations, zero technical/semantic failures, and worker
  exit code 0.
- Four V2 recipes were retained with `v2_visibility_degraded=true`; their named
  physics/events remained valid.
- The complete campaign occupies approximately 329 GiB on the network volume.

## The three different frame counts

Do not use "frames" without saying which of these is meant:

1. **Physics simulation steps** are internal collision/trajectory calculations
   used by certification. They are not necessarily RGB observations.
2. **Credited observation frames** are the planner's exact allocation and
   balance accounting.
3. **Produced observation frames** are the RGB observations physically saved
   in finalized shards.

For a successful recipe, the worker computes:

```text
credited frames = min(produced valid observations, planned_credited_frames)
```

This cap does **not** truncate the stored episode. If a recipe has a 60-frame
credit cap and produces 75 valid observations, all 75 images and their
transitions remain in the dataset; 60 count toward allocation and 15 are
reported as produced-but-not-credited observations. Training should use the
complete valid episode, not slice it at the credit cap.

The plan projected approximately 3,343,246 complete stored observations:

- V1 base recipes: approximately 1,113,877 observations from calibrated
  durations;
- V2 recipes: 2,229,369 observations from frozen durations.

The completed Linux capture contains 3,381,548 stored observations: 1,152,179
for V1 and 2,229,369 for V2. The exact planned allocation target remains
3,333,333; produced observations and V1 assignment-granular credit overshoot are
reported separately and must not be discarded.

## Recipes, base work, tails, and replacements

A **recipe** is one immutable episode instruction: stage, mission, scenario,
seed/episode identity, action behavior, and planned credit cap.

V1 has:

- 6,026 base recipes selected to cover the catalog and frame targets;
- 6,023 base recipes with positive credit (three end-of-bucket recipes have a
  zero remaining credit cap);
- 604 deterministic tail recipes for realized V1 frame shortfalls;
- 6,630 total active recipes.

The worker executes assignments in plan order and checks inventory between
assignments. Once inventory is complete, it starts no new assignment. Tail
recipes are full episodes; they are not partial fragments. They are distinct
from certification replacements.

A **replacement** occupies the same planned coverage/quota slot as a recipe
that failed before recording. The resolver:

1. preserves the original recipe and failure evidence;
2. tries up to ten distinct same-mission/same-scenario candidates;
3. orders attempts from diverse to increasingly conservative;
4. if needed, mutates toward a certified recipe of the same mission without
   weakening the verifier;
5. records complete lineage;
6. emits a new immutable resolved collection; and
7. runs the ordinary bound certifier again on the complete resolved plan.

No replacement occurs inside the recorder. A runtime disagreement after final
certification is a regression and remains fail-closed.

## Episode boundaries and training identity

Every recipe produces one independent episode. Frames inside an episode form a
continuous temporal sequence. The simulator resets or repositions between
episodes, so the last frame of one episode must never be connected to the first
frame of another.

Finalized data preserves this explicitly:

- every frame has `episode_id` and `frame_index`;
- every transition has `episode_id` and `source_frame_index`;
- every `episodes.parquet` row contains episode, recipe, plan, stage, split,
  observation-count, transition-count, mission, success, and termination
  metadata;
- V2 rows additionally preserve source, mission type, replay identity, event,
  canonical physics, and throw evidence.

Use `(plan_id, plan_version, episode_id)` as the robust sequence key. Never
construct a transition across different episode IDs.

## V1/V2 folder separation

The final recordable plans are separate collection roots:

```text
WindowsCampaign3333333-20260813-112500/
`-- resolution/
    |-- v1/
    |   `-- resolved-collection/
    `-- v2/
        `-- resolved-collection/
```

Keep that separation during capture and training export:

```text
training-data/
|-- v1/
`-- v2/
```

The internal fallback identity is `plan_version`:

- V1 starts with `movement-v1`;
- V2 starts with `trajectory-throw-v2`.

V2 also sets `v2_source` to `semi_markov` or `mission`. Folder identity is
convenient; internal plan/episode identity is authoritative if files are moved.

## Movement V1

V1 uses 70% semi-Markov and 30% guided missions. Its exact campaign allocation
is:

| V1 source | Frames |
| --- | ---: |
| Semi-Markov | 777,778 |
| Object view/navigation | 146,666 |
| Contact and recovery | 110,000 |
| Ramp traversal | 36,667 |
| Hoop passage | 36,667 |
| Static no-input | 3,333 |

The catalog has 855 guided cells and 32 semi-Markov opening cells. Existing
object, contact, ramp, and hoop mission logic remains protected by canonical
source regression checks.

### Static no-input mission

Static no-input is 1% of V1 mission frames (0.3% of V1 overall). Each episode:

- runs for exactly 200 observations at 20 Hz (ten seconds);
- uses action mask zero;
- samples a deterministic collision-safe random position within one of 15
  spatial strata;
- samples a random fixed yaw and pitch;
- rejects movement greater than 1 cm or yaw/pitch drift greater than 0.01 degrees.

The isolated runtime qualification passed 19/19 static recipes.

## Trajectory/Throw V2

V2 uses 70% semi-Markov and 30% prescribed missions. The campaign allocation is:

| V2 source/family | Frames |
| --- | ---: |
| Semi-Markov | 1,555,555 |
| Broad object surfaces | 139,789 |
| Object edges and apexes | 139,789 |
| Wall/corner rebounds | 129,036 |
| Hoop interactions | 107,525 |
| Ramp interactions | 86,016 |
| Out-of-bounds | 43,008 |
| Trajectory control | 21,504 |

There are 62 prescribed mission types. At this budget, each receives either
10,752 or 10,753 credited frames. The exact per-type allocation and variation
cell counts are in the combined and V2 resolution reports.

Every mission uses the canonical launch speed and one immutable
`FGrenadeSimConfig`. Preview, construction certification, and realized throws
share the same launch state and physics. The V2 verifier performs canonical
simulation/contact checks without RGB capture. See `V2_MISSION_DESIGN.md` for
the complete mission catalog and geometry contract.

## Shared action and semi-Markov behavior

V1 and V2 store the same ten action bits:

| Bit | Input | Meaning |
| ---: | --- | --- |
| 0-3 | W/A/S/D | Planar movement |
| 4-7 | Arrow keys | Camera pitch/yaw |
| 8 | Q | Trajectory preview / aim lock |
| 9 | E | Throw request |

V1 masks Q/E. V2 enables them. Q suppresses planar movement while preserving
camera input; E is edge-triggered and accepted only after visible Q and a clear
cooldown.

Both stages use the same persistent semi-Markov selector with a capability flag.
Technically valid semi-Markov episodes are not rejected for behavioral or
statistical distributions. Technical corruption, schema disagreement, invalid
action state, or preview/realized physics divergence remains fail-closed.

## Historical Windows planning, certification, and resolution

The campaign is created without recording RGB by:

```powershell
python Scripts/plan_verify_resolve_windows.py Artifacts/WindowsCampaign3333333 `
  --executable "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
```

This command:

1. creates V1 and V2 candidate plans;
2. runs both structural verifiers;
3. runs V1 gameplay and V2 construction certification;
4. resolves rejected slots without modifying either verifier;
5. builds final resolved collections;
6. performs standard plan/build-bound recertification; and
7. writes `windows-plan-verification-report.json`.

It does not record RGB observations or render videos.

### Output and reports

```text
campaign/
|-- candidate-plans/
|   |-- v1-1111111/
|   `-- v2-2222222/
|-- resolution/
|   |-- v1/
|   |   |-- round-00-original/
|   |   |-- slots/                    # replacement attempts
|   |   |-- resolution-report.json
|   |   `-- resolved-collection/
|   |       `-- certification/
|   `-- v2/
|       |-- round-00-original/
|       |-- resolution-report.json
|       `-- resolved-collection/
`-- windows-plan-verification-report.json
```

The combined report records exact frame and recipe distributions, executable
fingerprint, plan IDs, resolution summaries, and completion. Each resolution
report records original failures, engine reasons, every attempted candidate,
strategy, accepted replacement, unresolved work, and final certification.

## Recording the training data

Recording must use a resolved collection whose bound certificate matches the
exact executable/package runtime. Do not record a candidate plan.

The following commands document the already completed Windows-bound workflow;
they do not authorize Linux capture:

```powershell
python Scripts/dataset_worker.py `
  Artifacts/WindowsCampaign3333333-20260813-112500/resolution/v1/resolved-collection `
  --executable "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  --worker-id 0

python Scripts/dataset_worker.py `
  Artifacts/WindowsCampaign3333333-20260813-112500/resolution/v2/resolved-collection `
  --executable "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  --worker-id 0
```

Each assignment is finalized to typed WebP/Parquet output and validated before
its result is accepted. After capture, use the respective controller inventory
to obtain exact credited and produced counts:

```powershell
python Scripts/dataset_controller.py inventory <v1-resolved-collection> --write-snapshot
python Scripts/v2_dataset_controller.py inventory <v2-resolved-collection>
```

### Linux and RunPod

The complete reproducible RunPod procedure is in
[`RUNPOD_LINUX.md`](RUNPOD_LINUX.md). It covers the Linux package build and
transfer, dependencies, Vulkan environment, smoke capture, planner parity,
full production planning, certification/resolution, recording, inventory, and
safe resume behavior.

Linux qualification completed on 2026-08-13 on a RunPod EU-RO-1 RTX 5090 pod.
The UE 5.8 Linux Development executable SHA-256 is
`0bc31b9b2f9056af5454f4b24505d9572b906e8af5f3db55859ea605151b9740`.
The package's Linux-computed runtime SHA-256 is
`5bc0f818d4623a6055cc3a172a1782ea1b86f6832b6b6d3378e363ce55e186229`.

Qualification established:

- 72/72 Python tests passing on both Windows and Linux;
- exact Windows/Linux planner parity for the V1 10,000-frame diagnostic and
  V2 33,274-frame minimum plans;
- offscreen Vulkan SM6 rendering on the RTX 5090 at 384x384;
- V1 Linux resolution of one reproducible `mission_no_progress` recipe while
  preserving all 902 discrete cells and the exact mission frame allocation;
- rebuilt-package certification passing 870/870 V1 guided recipes and 127/127
  V2 recipes, with zero failures;
- one production-worker assignment passing for V1 (6 episodes, 588 stored
  observations, 418 credited frames) and V2 (8 episodes, 3,958 stored and
  credited observations), with typed Parquet finalization and validation;
- zero technical or semantic failures in both published test assignments.

The qualification found and fixed a V1 metadata defect: an empty per-recipe
split overwrote the assignment-level `train` split. `BeginEpisode()` now keeps
the manifest split unless a recipe supplies a non-empty override, preserving
V1 behavior while retaining V2 per-recipe train/evaluation splits.

Full V2 production then completed on 2026-08-14 on a RunPod EU-RO-1 RTX 4090
pod using source commit `b49ddd038e688c01567b4899984b4c6d7a3b3a64` and schema
`trajectory_throw_v2-production-14`. The package binary SHA-256 is
`1b1ebcfa67c79c760d9515ec2750cb3481dc705e24b943991e3786707bd8a94a`;
the Linux package-runtime fingerprint is
`2ca9be2e3858dcd6f8ce28f3b841e12d3b315c9bd9ef6c24f7802069455ebece`.
The Windows and Linux full V2 plans had the same plan ID and byte-identical 171
assignment files and `recipes.jsonl`; only `created_utc` differed in the plan
manifest. Certification and production evidence remains separate from the
historical Windows-bound campaign.

## Human visual review

The representative suite contains 85 videos: 19 V1 and 66 V2. These are
mission-review media, separate from the production frame budget and physics
certificate. The generated suite was visually reviewed and accepted in the
current project session.

`Scripts/build_review_85.py` validates and renders the fixed representative
selection after its captures exist. V2 also retains optional tooling for an
exhaustive 186-video review (three examples for each of 62 V2 mission types),
but that historical suite is not the current 85-video representative contract.

Video rendering validates packaging and supports human inspection; it is not a
second mission-physics verifier.

V2 schema 14 records camera presentation quality without rejecting otherwise
valid episodes. Every V2 observation has `v2_visibility_degraded`; prescribed
mission episodes also carry the aggregate `v2_visibility_degraded` value plus
the detailed region, preview, and opening-context summaries. Visibility
degradation never weakens physics, action, identity, or mission-event checks and
does not stop recording. It is retained so training can include, exclude, or
down-weight those observations explicitly. Assignment results and reconstructed
V2 inventory also report the affected recipe IDs and count.

## Verification checkpoint

The current implementation was checked with:

- 74 Python unit, contract, regression, and resolver tests passing;
- all Python files under `Scripts/` compiling;
- `git diff --check` passing;
- `he_grenade_gameEditor Win64 Development` building under Unreal Engine 5.8;
- static no-input runtime qualification passing 19/19;
- exact 3,333,333-frame V1/V2 structural planning passing;
- final Windows campaign certification completing with zero unresolved recipes;
- Linux V1/V2 diagnostic certification and production-worker checks passing.
- full Linux V2 certification passing 5,459/5,459 and production completing
  171/171 assignments with an exact 2,222,222-frame credited inventory;
- complete Linux V1 and V2 inventories written with zero technical or semantic
  failures.

Any change to runtime physics, mission geometry, camera behavior, timing,
planner identity, finalizer schemas, or certification bindings requires
proportionate regression testing and recertification.

## Main source files

- `Scripts/dataset_controller.py`: V1 planner, inventory, tail scheduling, and
  distribution accounting.
- `Scripts/certify_v1_plan.py`: no-RGB V1 guided gameplay certification.
- `Scripts/v2_mission_catalog.py`: immutable 62-type V2 catalog and solutions.
- `Scripts/v2_dataset_controller.py`: V2 planner, verifier, inventory, and
  optional exhaustive review-plan builder.
- `Scripts/certify_v2_plan.py`: no-capture V2 construction certification.
- `Scripts/resolve_plan_certification.py`: bounded replacement rounds, lineage,
  resolved-plan construction, and final recertification.
- `Scripts/plan_verify_resolve_windows.py`: approved combined campaign workflow.
- `Scripts/dataset_worker.py`: certified assignment execution and crediting.
- `Scripts/finalize_production_dataset.py`: typed WebP/Parquet finalization.
- `Scripts/review_dataset.py`: technical and semantic dataset validation.
- `Scripts/build_review_85.py`: fixed representative review renderer.
- `Source/he_grenade_game/DataGenerator/CurriculumDataGenerator.cpp`: runtime
  missions, semi-Markov actions, capture, and grenade simulation.
- `Source/he_grenade_game/DataGenerator/V2ActionSemantics.*`: Q/E action rules.

## Non-negotiable operational rules

- Record only a final resolved collection with a certificate bound to the exact
  plan, recipes, assignments, executable, package runtime, and source identity.
- Keep V1 and V2 collection roots separate.
- Preserve all complete valid episode observations for training.
- Never connect sequences across episode IDs.
- Treat credit caps as allocation metadata, not a truncation instruction.
- Never weaken a verifier to make a candidate pass.
- Never replace a recipe during recording.
- Preserve candidate plans, rejected evidence, replacement lineage, final
  reports, and inventory snapshots for reproducibility.
