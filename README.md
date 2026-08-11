# HE Grenade Game Curriculum Dataset Generator

This repository generates deterministic first-person visual-control data in the
fixed HE Grenade Game arena.

## Current state

The curriculum has two stages:

- **Movement V1:** movement and camera control. Its visually approved guided
  missions are preserved. New generation uses 70% persistent semi-Markov play
  and 30% guided missions.
- **Trajectory/Throw V2:** the same persistent semi-Markov policy with trajectory
  preview and grenade throwing enabled. V2 missions were removed and have not
  yet been rebuilt. Until that work is implemented, V2 plans contain only
  semi-Markov episodes.

The next V2 implementation will use 70% semi-Markov frames and 30% prescribed
mission frames. The planned mission catalog contains 60 rare, visually useful
interaction types. This is a design decision, not implemented behavior.

Production generation is not authorized until the current semi-Markov policy
and the future V2 missions pass human visual review.

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

The active V2 planner currently creates semi-Markov recipes only. It has no
mission cells, mission success test, qualification pass, reserve recipes,
replacement seeds, or post-generation behavioral rejection.

The agreed future V2 mixture is:

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

The V2 minimum feasible frame budget will be calculated from the finalized
mission durations and family shares. No historical feasibility floor will be
copied.

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

Create and verify the current semi-Markov-only V2 plan:

```powershell
python Scripts/v2_dataset_controller.py plan Artifacts/V2Plan `
  --frame-budget 700000 --episode-seconds 150 `
  --observation-rate 20 --width 384 --height 384

python Scripts/v2_dataset_controller.py verify-plan Artifacts/V2Plan
```

Run assignments with `Scripts/dataset_worker.py`, inspect progress with the V1
or V2 controller's `inventory` command, and validate/render review media with
`Scripts/review_dataset.py`.

Machine validation establishes technical and physics integrity. It does not
constitute human approval of pacing, framing, mission usefulness, or dataset
distribution.

## Main source files

- `Source/he_grenade_game/DataGenerator/CurriculumDataGenerator.cpp`: V1
  missions, shared semi-Markov policy, capture, Q/E integration, and grenade
  simulation.
- `Source/he_grenade_game/DataGenerator/V2ActionSemantics.*`: Q/E edge and
  cooldown semantics.
- `Scripts/dataset_controller.py`: immutable Movement V1 planner.
- `Scripts/v2_dataset_controller.py`: current semi-Markov-only V2 planner.
- `Scripts/dataset_worker.py`: assignment execution and frame crediting.
- `Scripts/finalize_production_dataset.py`: WebP/Parquet finalization.
- `Scripts/review_dataset.py`: technical validation and MP4 rendering.

Historical V2 mission code and its audit, calibration, reserve, and replacement
systems were deleted. They must not be restored as the implementation of the new
mission design.
