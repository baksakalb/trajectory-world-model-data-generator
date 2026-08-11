# HE Grenade Game Curriculum Dataset Generator

This repository generates deterministic first-person visual-control data in the
fixed HE Grenade Game arena.

## Current state

The curriculum has two stages:

- **Movement V1:** movement and camera control. Its visually approved guided
  missions are preserved. New generation uses 70% persistent semi-Markov play
  and 30% guided missions.
- **Trajectory/Throw V2:** the same persistent semi-Markov policy with trajectory
  preview and grenade throwing enabled, plus 60 prescribed mission types whose
  immutable launch/camera solutions are certified with the canonical simulator
  before capture.

V2 plans allocate exactly 70% of credited frames to semi-Markov play and 30% to
prescribed missions. Each of the 60 mission types receives exactly 0.5% of the
total frame budget.

Production generation is not authorized until the current semi-Markov policy
and the implemented V2 missions pass human visual review.

### Implementation checkpoint

The V2 implementation is code-complete against `V2_MISSION_DESIGN.md`:

- the immutable catalog contains exactly 60 mission types in the agreed six
  families;
- the planner produces an exact 70/30 credited-frame split and an exact 0.5%
  share for every mission type;
- every feasible plan begins with one semi-Markov recipe and one recipe for all
  60 mission types, then uses deterministic largest-frame-deficit scheduling;
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
- the review planner prepares exactly 180 immutable 384x384 recipes: three
  separated examples for each mission type;
- Movement V1 planning and guided-mission behavior remain protected by
  byte-preservation regression tests.

The implementation deliberately contains no qualification search, behavioral
acceptance gate, alternate production seed, reserve recipe, semantic retry, or
replacement path. A machine-interrupted recipe may be replayed unchanged; a
certified mission that fails its invariant is a regression and stops generation.

No V2 review dataset, review video, or production dataset was generated as part
of this implementation.

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

V1 retains four guided families:

- object view/navigation;
- contact and recovery;
- ramp traversal;
- hoop passage.

The immutable V1 planner contains 855 guided catalog cells plus 32 semi-Markov
opening cells. The large count comes from crossing meaningful tasks with gaze,
facing, recovery, and path variants. Approximately 59 combinations represent
the distinct task geometry a human reviewer needs to understand.

V1 planning schedules every catalog cell once, then balances additional work in
credited frame units. Its current calculated minimum feasible budget is 516,800
frames. That floor belongs only to V1 and must not be reused for V2.

Existing V1 datasets and previously approved videos are unchanged.

## Trajectory/Throw V2

The active V2 planner first emits one semi-Markov opening recipe and one recipe
for every mission type. It then repeatedly schedules the type with the largest
credited-frame deficit. The implementation has no candidate qualification,
reserve recipes, replacement seeds, or post-generation behavioral rejection.

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

Each of the 60 mission types therefore receives 0.5% of total frames. Ordinary
misses, floor throws, settling, bounce-count variants, generic temporal actions,
and multi-throw play remain the responsibility of semi-Markov episodes.

V2 missions must be constructed from canonical-physics solutions before video
generation. Production will sample only from frozen, known-working launch and
camera regions. It will not generate candidates and then accept, reject, retry,
or replace seeds.

Every mission must keep its intended object region or arena boundary visible
from the opening through the required interaction and final frame. During Q
holds, the useful trajectory preview and interaction region must be visible
together. A mission may not open on an empty-sky or floor-only view.

The V2 minimum feasible frame budget is calculated from the frozen mission
durations, mandatory semi-Markov opening, and exact family shares. At the
default 150-second/20 Hz settings it is 32,200 frames. It is recomputed if those
settings change; no historical feasibility floor is copied.

The complete agreed implementation contract is in `V2_MISSION_DESIGN.md`.

## Canonical grenade physics

Every accepted throw uses the fixed game launch path, launch speed, cooldown,
and one immutable `FGrenadeSimConfig`. Recipes cannot change gravity,
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

Prepare the immutable 180-recipe human-review plan without running Unreal or
creating videos:

```powershell
python Scripts/v2_dataset_controller.py review-plan Artifacts/V2ReviewPlan `
  --observation-rate 20 --workers 4
```

After review generation is explicitly authorized, `Scripts/build_v2_review_set.py`
consumes that plan, validates the captures, and renders exactly three 384x384
videos per mission type. Creating the review plan itself generates no dataset
and no videos.

Run assignments with `Scripts/dataset_worker.py`, inspect progress with the V1
or V2 controller's `inventory` command, and validate/render review media with
`Scripts/review_dataset.py`.

Machine validation establishes technical and physics integrity. It does not
constitute human approval of pacing, framing, mission usefulness, or dataset
distribution.

## Verification completed

The implementation checkpoint was verified on Windows with Unreal Engine 5.8:

- all 43 Python unit, contract, and regression tests passed;
- every Python file under `Scripts/` compiled successfully;
- `git diff --check` passed for the committed implementation;
- the `he_grenade_gameEditor Win64 Development` target built successfully;
- Unreal native automation discovered and passed
  `HEGrenadeGame.DataGenerator.V2.ActionSemantics`;
- planner tests confirmed the exact 490,000/210,000 split at 700,000 frames,
  3,500 frames for each mission type, and the calculated 32,200-frame default
  feasibility floor;
- review-plan tests confirmed 180 one-recipe assignments, three per type;
- canonical regression tests confirmed one launch speed and configuration for
  preview, construction certification, and realized throws, and preserved V1
  planner and mission source identities.

These checks establish implementation and machine-contract correctness. They
do not replace rendered mission review. In particular, no claim has yet been
made that all 180 examples have acceptable composition, pacing, visible contact,
or useful natural deflection in captured video.

## Required next work

Complete the remaining work in this order:

1. Independently audit the implementation against this README and
   `V2_MISSION_DESIGN.md`. Inspect the code and tests directly; do not treat this
   status section as proof.
2. If the code audit passes, explicitly authorize review-set generation only.
   This is not authorization for a full production run.
3. Create the immutable review plan, execute its 180 assignments through
   `Scripts/dataset_worker.py`, then validate and render them with:

   ```powershell
   python Scripts/build_v2_review_set.py Artifacts/V2ReviewPlan `
     --output Artifacts/V2ReviewPlan/videos
   ```

4. Human-review all 180 videos for correct named interaction, target/region
   visibility throughout, preview/target co-visibility, readable opening and
   timing, meaningful separated variation, and natural post-contact behavior.
5. If any example is defective, fix the mission definition or certified region
   globally. Do not select a friendlier seed or add replacement logic. Rebuild
   and re-review the affected catalog consistently.
6. Re-run the complete Python suite, Unreal build, native automation, and the
   full 180-video review after any runtime, physics, catalog, camera, or timing
   change.
7. Authorize production explicitly only after the independent code audit and
   human visual review both pass.
8. Create the final production plan at the approved frame budget, verify the
   plan before dispatch, run assignments, inventory credited frames, finalize
   WebP/Parquet output, and perform final technical/distribution validation.

Production must remain stopped if a certified mission fails at runtime, preview
and realized physics diverge, a recipe identity changes, the exact frame shares
do not hold, or V1 behavior changes.

## Independent audit checklist

An independent verifier should confirm at least the following directly from
source and test output:

- catalog counts `13 + 13 + 12 + 10 + 8 + 4 = 60`, exact family shares, and
  unique deterministic identities;
- one mandatory pass over every type before deterministic frame-deficit
  scheduling, exact 70/30 allocation, exact 0.5% per type, calculated floor,
  and train/evaluation separation when the budget permits;
- unconditional credit for technically valid semi-Markov episodes and no
  behavioral/statistical rejection thresholds;
- absence of candidate, qualification, reserve, alternate-seed, substitution,
  or semantic-replacement paths across planner, worker, runtime, and validator;
- one canonical `FGrenadeSimConfig`, fixed launch speed, identical preview and
  realized launch state, construction-time canonical simulation, and runtime
  invariant failure handling;
- ordinary-input mission timing, one valid Q-then-E throw, camera/region
  railguards, named contact/passage/crossover/exit evidence, and controlled
  aftermath;
- schema/finalizer/reviewer agreement for semi-Markov and mission records;
- exactly 180 review recipes at 384x384, repetitions 0/5/17, and no automatic
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
- `Scripts/v2_mission_catalog.py`: immutable 60-type catalog and certified
  deterministic solution regions.
- `Scripts/v2_dataset_controller.py`: combined V2 planner, verifier, inventory,
  and 180-recipe review-plan builder.
- `Scripts/build_v2_review_set.py`: authorized review-capture validator and
  renderer.
- `Scripts/dataset_worker.py`: assignment execution and frame crediting.
- `Scripts/finalize_production_dataset.py`: WebP/Parquet finalization.
- `Scripts/review_dataset.py`: technical validation and MP4 rendering.

Historical V2 audit, calibration, reserve, and replacement systems remain
deleted. The implemented mission design uses construction-time certification
and treats runtime disagreement as a regression, never as a search for a more
convenient seed.
