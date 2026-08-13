# HE Grenade Game Curriculum Dataset Generator

This repository generates deterministic first-person visual-control data in the
fixed HE Grenade Game arena.

## Current state

The curriculum has two stages:

- **Movement V1:** movement and camera control. Its visually approved guided
  missions are preserved. New generation uses 70% persistent semi-Markov play
  and 30% guided missions.
- **Trajectory/Throw V2:** the same persistent semi-Markov policy with trajectory
  preview and grenade throwing enabled, plus 62 prescribed mission types whose
  immutable launch/camera solutions are certified with the canonical simulator
  before capture.

V2 plans allocate whole-frame targets nearest to 70% semi-Markov and 30%
prescribed missions, while always summing exactly to the requested budget. The
62 mission types differ by at most one credited frame.

Production generation is not authorized until the current semi-Markov policy
and the implemented V2 missions pass human visual review.

### Implementation checkpoint

The V2 implementation is structurally complete against `V2_MISSION_DESIGN.md`.
The current catalog is machine-qualified at a one-million-frame budget, but
production still requires the prescribed human visual review:

- the immutable catalog contains exactly 62 mission types in seven mission
  families, including two trajectory-control demonstrations;
- the planner produces an exact whole-frame budget, nearest-integer 70/30 source
  targets, and mission-type targets that differ by at most one frame;
- every feasible plan begins with one semi-Markov recipe and one recipe for all
  62 mission types, then uses deterministic largest-frame-deficit scheduling;
- the feasibility floor is calculated from mandatory recipe durations and
  shares rather than copied from V1 or a historical V2 implementation;
- mission launch/camera solutions are deterministic, use the low ballistic
  branch, and are certified with the same canonical simulator used by preview
  and realized play;
- runtime playback uses ordinary camera/Q/E inputs, checks the immutable launch
  state, named interaction, timing, and visibility railguards, and stops on a
  physics or code regression;
- worker crediting, output schemas, finalization, and review validation support
  both persistent semi-Markov and certified mission recipes;
- the review planner prepares exactly 186 immutable 384x384 recipes: three
  separated examples for each mission type;
- Movement V1 planning and guided-mission behavior remain protected by
  byte-preservation regression tests.

The recorder deliberately contains no qualification search, alternate seed, or
runtime replacement path. Before recording, a separate resolver may replace a
candidate-plan recipe that fails no-capture certification. It preserves the
failed recipe and evidence, tries at most ten distinct same-slot candidates in
increasingly conservative order, records complete lineage, and emits a new
immutable plan. That resolved plan is certified again and bound to the exact
executable before it can be recorded. A disagreement while recording remains a
regression and is never replaced on the fly.

The no-capture verifier certifies the complete immutable production plan before
recording. The current one-million-frame plan certified all 2,468 recipes
(234 semi-Markov plan entries and 2,234 construction-tested mission entries)
with zero rejects. Recording remains blocked until the new review set passes
human inspection.

## Canonical action schema

V1 and V2 use the same ten-bit action and transition schema:

| Bit | Input | Meaning |
| ---: | --- | --- |
| 0 | W | forward |
| 1 | A | strafe left |
| 2 | S | backward |
| 3 | D | strafe right |
| 4 | ArrowUp | camera pitch up |
| 5 | ArrowDown | camera pitch down |
| 6 | ArrowLeft | camera yaw left |
| 7 | ArrowRight | camera yaw right |
| 8 | Q | trajectory preview / aim lock |
| 9 | E | throw request |

Opposing inputs are legal and recorded. The runtime resolves their realized
axes consistently.

Q is level-triggered. While Q is held, planar movement is suppressed and camera
input remains active. E is accepted only on a rising edge, after Q was visible
in the preceding observation, and when the canonical two-second cooldown is
clear.

## Shared persistent semi-Markov play

V1 and V2 call one C++ selector:

```cpp
SelectPersistentSemiMarkovAction(
    CurriculumStage == ECurriculumStage::TrajectoryThrowV2);
```

The argument is a capability flag:

- V1 passes `false`, so the result is limited to movement and camera bits.
- V2 passes `true`, enabling Q/E without changing the movement, camera, hold,
  transition, collision-escape, or arena-attention policy.

Episodes start from deterministically varied positions and camera angles. The
policy favors readable, human-like play while retaining rare controls and edge
cases. Actions persist for meaningful durations; camera pitch favors useful
middle views; movement tends toward useful arena space without forbidding walls
or corners; and post-throw attention may follow, briefly watch, revisit, or
ignore a grenade.

V2 grenade-control bias is probabilistic, not a quota:

- Q is added to 30% of ordinary holds.
- Q is added to 50% of scripted transition segments.
- After the first opportunity, throw opportunities are spaced by a random
  2.5-6.0 seconds.

Semi-Markov episodes last 120-180 seconds, with 150 seconds as the default.

### Semi-Markov acceptance

Every technically valid semi-Markov episode is credited. Episodes are never
rejected for camera distribution, eye-level share, wall time, map coverage,
action frequency, trajectory frequency, throw count, or grenade outcome.

Technical validation may reject corrupt or inconsistent output, such as missing
frames, invalid action-state recording, broken images, incorrect duration, or
preview-versus-realized physics divergence. An individual invalid E press may
be recorded as rejected gameplay without rejecting its episode.

## Movement V1

V1 contains five guided families:

- object view/navigation;
- contact and recovery;
- ramp traversal;
- hoop passage;
- static no-input controls, with a collision-safe random spawn and fixed random
  camera orientation for ten seconds.

The immutable V1 planner contains 855 guided catalog cells plus 32 semi-Markov
opening cells. The large count comes from crossing meaningful tasks with gaze,
facing, recovery, and path variants. Approximately 59 combinations represent
the distinct task geometry a human reviewer needs to understand.

V1 planning schedules every catalog cell once, then balances additional work in
credited frame units. Its current calculated minimum feasible budget is 1,000,000
frames. That floor belongs only to V1 and must not be reused for V2.

Existing V1 datasets and previously approved videos are unchanged.

## Trajectory/Throw V2

The active V2 planner first emits one semi-Markov opening recipe and one recipe
for every mission type. It then repeatedly schedules the type with the largest
credited-frame deficit. Repetitions progress through deterministic coverage
cells (surface/rim position, distance, arc, approach side, and two-wall contact
order) before refining those cells with seeded continuous offsets. The
implementation has no candidate qualification, reserve recipes, replacement
seeds, or post-generation behavioral rejection inside the planner or recorder.
Qualification and any pre-recording substitution are handled by the separate
bounded resolver, leaving the canonical planner and verifier unchanged.

The implemented V2 mixture is:

| Source | Types | Frame share |
| --- | ---: | ---: |
| Persistent semi-Markov | — | 70% |
| Broad object-surface impacts | 13 | 6.5% |
| Object edge and apex impacts | 13 | 6.5% |
| Wall and corner rebounds | 12 | 6% |
| Hoop interactions | 10 | 5% |
| Ramp interactions | 8 | 4% |
| Deliberate out-of-bounds | 4 | 2% |
| Trajectory-control demonstrations | 2 | 0.9677% |

Each of the 62 mission types therefore receives the nearest whole-frame
allocation to `3/620` of total frames. Ordinary
misses, floor throws, settling, bounce-count variants, generic temporal actions,
and multi-throw play remain the responsibility of semi-Markov episodes.

V2 missions must be constructed from canonical-physics solutions before video
generation. Production records only the frozen recipes in a zero-rejection
resolved plan.

Every mission must keep its intended object region or arena boundary visible
from the opening through the required interaction and final frame. During Q
holds, the useful trajectory preview and interaction region must be visible
together. A mission may not open on an empty-sky or floor-only view.

The V2 minimum feasible frame budget is calculated from the frozen mission
durations, mandatory semi-Markov opening, and exact family shares. At the
certified 20 Hz observation rate and default 150-second semi-Markov duration it
is 33,274 frames. The duration-dependent floor is recomputed; non-20-Hz plans
are rejected because their physics/camera timing has not been certified.

The complete agreed implementation contract is in `V2_MISSION_DESIGN.md`.

## Canonical grenade physics

Every accepted throw uses the fixed game launch path, launch speed, cooldown,
and one immutable `FGrenadeSimConfig`. Named mission launches are anchored to
the recipe's fixed eye point rather than an animated head bone. Recipes cannot change gravity,
restitution, friction, damping, bounce limits, or stopping behavior.

Preview and realized motion start from the same launch state and physics
configuration. Natural contacts, bounces, rolls, settling, and arena exits are
recorded rather than manufactured.

Stored V2 throw metadata contains physical evidence only: grenade identity,
throw camera, contacts, bounces, rest, arena exit, visibility, and
preview-versus-realized parity. Privileged trajectory points are not learning
targets.

## Planning and validation

Create and verify a combined V2 plan:

```powershell
python Scripts/v2_dataset_controller.py plan Artifacts/V2Plan `
  --frame-budget 700000 --episode-seconds 150 `
  --observation-rate 20 --width 384 --height 384

python Scripts/v2_dataset_controller.py verify-plan Artifacts/V2Plan
```

Prepare the immutable 186-recipe human-review plan without running Unreal or
creating videos:

```powershell
python Scripts/v2_dataset_controller.py review-plan Artifacts/V2ReviewPlan `
  --observation-rate 20 --workers 1
```

After review generation is explicitly authorized, `Scripts/build_v2_review_set.py`
consumes that plan, validates the captures, and renders exactly three 384x384
videos per mission type. Creating the review plan itself generates no dataset
and no videos.

Before any review or production recording, batch-certify the complete immutable
plan in one Unreal session. This performs canonical physics/contact construction
checks without rendering or writing training observations and emits a report
bound to the plan, assignment set, executable, package runtime, and generator
source:

```powershell
python Scripts/certify_v2_plan.py Artifacts/V2ReviewPlan `
  --executable "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  --output Artifacts/V2ReviewPlan/certification
```

Recording is forbidden unless this command exits successfully with
`complete: true` and zero rejected recipes. Runtime repeats the same
construction check immediately before each recipe as a final fail-closed gate.

Movement V1 uses its own no-capture runtime verifier. It executes the normal V1
guided mission controller and success checks against every guided recipe in the
immutable plan, including reserves, while deliberately excluding stochastic
semi-Markov recipes. It allocates no RGB render target and writes no dataset:

```powershell
python Scripts/certify_v1_plan.py Artifacts/MovementV1Plan `
  --executable "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  --output Artifacts/MovementV1Plan/certification
```

`Scripts/dataset_worker.py` refuses V1 or V2 recording when the corresponding
certificate is absent, rejected any recipe, or no longer matches the exact plan
and executable/package runtime. The normal lifecycle is therefore: create the
V1 and V2 plans, run their structural verifiers, run each stage's no-capture
certifier, record assignments, validate package integrity, and only then render
the separately selected 85-video human-audit set. Video creation is not a
mission-physics verification pass.

For the approved campaign—1,000,000 mission frames plus 2,333,333 semi-Markov
frames, split one-third V1 and two-thirds V2—the Windows orchestration command
is:

```powershell
python Scripts/plan_verify_resolve_windows.py Artifacts/WindowsCampaign3333333 `
  --executable "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
```

This creates both candidate plans, runs both structural verifiers, performs
their existing no-capture runtime/physics certification, resolves rejected
mission slots without weakening either verifier, certifies each final resolved
plan again using the standard recording gate, and writes
`windows-plan-verification-report.json`. It does not record RGB data or render
videos. Candidate plans and every failed/replacement attempt remain immutable
under the campaign directory.

Run assignments with `Scripts/dataset_worker.py`, inspect progress with the V1
or V2 controller's `inventory` command, and validate/render review media with
`Scripts/review_dataset.py`.

Machine validation establishes technical and physics integrity. It does not
constitute human approval of pacing, framing, mission usefulness, or dataset
distribution.

## Verification completed

The implementation checkpoint was verified on Windows with Unreal Engine 5.8:

- all 55 Python unit, contract, and regression tests passed;
- every Python file under `Scripts/` compiled successfully;
- `git diff --check` passed for the committed implementation;
- the `he_grenade_gameEditor Win64 Development` target built successfully;
- Unreal native automation discovered and passed
  `HEGrenadeGame.DataGenerator.V2.ActionSemantics`;
- planner tests confirmed the exact 490,000/210,000 split at 700,000 frames,
  approximately 3,387 frames for each mission type, and the calculated
  33,280-frame default
  feasibility floor;
- review-plan tests confirmed 186 one-recipe assignments, three per type;
- canonical regression tests confirmed one launch speed and configuration for
  preview, construction certification, and realized throws, and preserved V1
  planner and mission source identities;
- targeted runtime captures passed for the former repetition-17 east-wall
  blocker, a repetition-17 clean hoop passage, and a repetition-17 ramp
  crossover, with one accepted throw and successful mission credit in each;
- the earlier pre-control-mission smoke set passed its then-current 60 natural
  repetition-zero recipes in one continuous run (60 episodes, 8,210
  observations, 8,150 transitions); this historical result does not qualify
  the current 62-type catalog;
- complete manifests that omit any requested episode are now rejected, closing
  the empty-shard fail-open condition found by the smoke run.

The one-session verifier originally completed the 186-recipe review plan in
18.9 seconds without recording observations and correctly rejected seven bad
constructions. A subsequent one-million-frame plan exposed 68 later progressive
failures: 60 ramp recipes and eight pyramid-apex recipes. The causes were catalog
geometry errors, not physics tolerances: ramp X variation used the inverse of
the authored downhill Z slope, and lateral pyramid-apex samples retained the
apex Z coordinate instead of projecting onto the triangular surface.

Catalog version 10 corrects those physical projections without widening edge or
apex tolerances, changing grenade physics, adding retries, reducing parameter
coverage, or replacing recipes. The corrected one-million-frame plan preserves
35 variants for every affected ramp type and 37 pyramid-apex variants. Unreal
5.8 certified all 2,468 recipes in 113.8 seconds with `complete: true` and zero
rejects. The bound report is retained under
`Artifacts/V2Plan1000000FramesFixed20260812/certification/` locally; generated
plans and certification artifacts are not repository source.

## Required next work

Complete the remaining work in this order:

1. Independently audit the implementation and successful million-frame
   certification report
   against this README and `V2_MISSION_DESIGN.md`.
2. Create and certify a fresh immutable 186-recipe review plan from catalog
   version 10. Require 186/186 certified and retain the bound report. Do not
   record anything if certification exits nonzero.
3. After explicit review-only authorization, execute the 186 assignments
   through `Scripts/dataset_worker.py`, then validate and render them with:

   ```powershell
   python Scripts/build_v2_review_set.py Artifacts/V2ReviewPlan `
     --output Artifacts/V2ReviewPlan/videos
   ```

4. Human-review all 186 videos for correct named interaction, target/region
   visibility throughout, preview/target co-visibility, readable opening and
   timing, meaningful separated variation, and natural post-contact behavior.
5. If any example is defective, fix the mission definition or certified region
   globally. Do not select a friendlier seed or add replacement logic. Rebuild
   and re-review the affected catalog consistently.
6. Re-run the complete Python suite, Unreal build, native automation, full-plan
   certification, and the full 186-video review after any runtime, physics,
   catalog, camera, or timing
   change.
7. Authorize production explicitly only after the independent code audit and
   human visual review both pass.
8. Create the final production plan at the approved frame budget, statically
   verify it, batch-certify that exact immutable plan/build, then dispatch its
   assignments. Inventory credited frames, finalize WebP/Parquet output, and
   perform final technical/distribution validation.

Production must remain stopped if a certified mission fails at runtime, preview
and realized physics diverge, a recipe identity changes, the exact frame shares
do not hold, or V1 behavior changes.

## Independent audit checklist

An independent verifier should confirm at least the following directly from
source and test output:

- catalog counts `13 + 13 + 12 + 10 + 8 + 4 + 2 = 62`, exact family shares, and
  unique deterministic identities;
- one mandatory pass over every type before deterministic frame-deficit
  scheduling, exact whole-frame source/type targets, calculated floor, and
  train/evaluation separation when the budget permits;
- unconditional credit for technically valid semi-Markov episodes and no
  behavioral/statistical rejection thresholds;
- absence of candidate search or substitution inside the worker/runtime, and a
  bounded, fully reported pre-recording resolver that never weakens a verifier;
- one canonical `FGrenadeSimConfig`, fixed launch speed, identical preview and
  realized launch state, construction-time canonical simulation, and runtime
  invariant failure handling;
- ordinary-input mission timing, one valid Q-then-E throw, camera/region
  railguards, named contact/passage/crossover/exit evidence, and controlled
  aftermath;
- schema/finalizer/reviewer agreement for semi-Markov and mission records;
- exactly 186 review recipes at 384x384, three endpoint-inclusive budget
  coverage variants per type, and no automatic
  execution or video generation by the review-plan command;
- preservation of the immutable Movement V1 planner, V1 guided mission
  functions, and the shared semi-Markov selector with its V1 capability mask.

## Main source files

- `Source/he_grenade_game/DataGenerator/CurriculumDataGenerator.cpp`: V1
  missions, shared semi-Markov policy, capture, Q/E integration, and grenade
  simulation.
- `Source/he_grenade_game/DataGenerator/V2ActionSemantics.*`: Q/E edge and
  cooldown semantics.
- `Scripts/dataset_controller.py`: immutable Movement V1 planner.
- `Scripts/certify_v1_plan.py`: one-session, no-RGB V1 guided-mission runtime
  verifier with immutable plan and executable/package bindings.
- `Scripts/v2_mission_catalog.py`: immutable 62-type catalog and certified
  deterministic solution regions.
- `Scripts/v2_dataset_controller.py`: combined V2 planner, verifier, inventory,
  and 186-recipe review-plan builder.
- `Scripts/certify_v2_plan.py`: one-session, no-capture construction verifier
  with plan, assignment, executable, runtime-package, and source bindings.
- `Scripts/resolve_plan_certification.py`: bounded pre-recording replacement
  rounds, immutable lineage, resolved-plan construction, and final certification.
- `Scripts/plan_verify_resolve_windows.py`: exact 3,333,333-frame Windows
  campaign planner, verifier, resolver, and combined report.
- `Scripts/build_v2_review_set.py`: authorized review-capture validator and
  renderer.
- `Scripts/dataset_worker.py`: assignment execution and frame crediting.
- `Scripts/finalize_production_dataset.py`: WebP/Parquet finalization.
- `Scripts/review_dataset.py`: technical validation and MP4 rendering.

Historical runtime replacement systems remain deleted. The implemented mission
design uses construction-time certification; only the bounded pre-recording
resolver may substitute a failed candidate-plan slot. Runtime disagreement is a
regression, never a search for a more convenient seed.
