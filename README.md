# HE Grenade Game Curriculum Dataset Generator

## Purpose

This repository generates deterministic visual-control data from the fixed HE
Grenade Game arena for curriculum world-model training.

The curriculum has two active stages:

1. Movement V1: movement and camera control. Existing V1 guided missions are
   preserved because their generated videos were visually audited and accepted.
2. Trajectory/Throw V2: persistent semi-Markov play with the same movement and
   camera policy plus trajectory preview and grenade throwing.

V2 has no prescribed missions. The former V2 mission catalog, mission planner,
qualification system, audit-slot exporter, mission calibration, semantic
success/failure logic, reserve recipes, and replacement path were deleted. Old
generated artifacts may remain locally for historical inspection, but they are
not evidence for the current generator and must not be used for production.

No production generation is currently authorized. A new focused visual audit of
the shared semi-Markov policy is required first.

## Canonical action schema

V1 and V2 use the same ten-bit action layout and transition schema:

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

Contradictory inputs are legal. The canonical mask can therefore contain all ten
bits. The runtime resolves opposing axes consistently and records the realized
axes and action edges.

Q is level-triggered. While Q is held, planar movement is suppressed but camera
input remains active. E is considered only on a rising edge. A throw is accepted
only when Q was already visible in the preceding stored observation and the
cooldown is clear. Simultaneous first-frame Q+E is rejected.

## One shared persistent semi-Markov policy

Both curriculum stages call the same C++ selector:

```cpp
SelectPersistentSemiMarkovAction(
    CurriculumStage == ECurriculumStage::TrajectoryThrowV2);
```

The Boolean is a capability flag, not a separate policy:

- V1 passes `false`; Q/E are not sampled and the returned mask is restricted to
  movement and camera bits.
- V2 passes `true`; the same holds, transitions, movement preferences, camera
  preferences, collision escape, and post-throw attention operate with Q/E
  enabled.

There is no V2 mission selector and no V2 mission-specific first action. Every
episode begins from a deterministically seeded varied spawn and camera angle,
then executes the persistent policy for the complete episode.

The policy is biased toward plausible human play without per-episode quotas:

- actions are held for readable durations instead of changing every frame;
- movement, camera-only, combined movement/camera, idle, and stress combinations
  remain possible;
- camera pitch is biased toward useful middle/eye-level views but may look up or
  down;
- movement and attention tend to return toward useful arena space while wall
  approaches, wall-following, contacts, and escape behavior remain possible;
- transition scripts provide occasional reversals and short edge cases;
- V2 sometimes ignores a thrown grenade, sometimes watches briefly, sometimes
  follows longer, and sometimes looks back after a delay;
- no per-episode map-sector, eye-level percentage, wall-time, action-count, or
  camera-bin threshold accepts or rejects an episode.

Semi-Markov episodes must last from 120 through 180 seconds. The default is 150
seconds at 20 observations per second.

## Movement V1

Movement V1 preserves the previously audited guided mission implementation:

- object view/navigation;
- contact and recovery;
- ramp traversal;
- hoop passage.

Its current controller schedules 70% persistent semi-Markov frames and 30% V1
guided-mission frames. This README does not claim that the newly shared V1 random
policy has been visually approved; it requires a focused review before new V1
production generation.

Existing V1 datasets and videos are unchanged by the generator rewrite.

## Trajectory/Throw V2

V2 is now 100% persistent semi-Markov play. It has no mission/frame mixture and
no rare-event guarantees. Contacts, misses, bounces, rolls, settling, hoop/ramp
interactions, and arena exits occur only when they arise naturally from the
seeded play policy.

The V2 grenade controls are moderately biased without quotas: Q is added to 30%
of ordinary sampled holds and 50% of scripted transition segments, and throw
opportunities are spaced by a random 2.5-6.0 seconds after the initial
opportunity. The canonical two-second cooldown remains unchanged.

The V2 planner creates only recipes with all three identities equal to
`semi_markov`:

```json
{
  "mission": "semi_markov",
  "source": "semi_markov",
  "family": "semi_markov"
}
```

There are no V2 cells, sequence templates, qualification plans, audit slots,
semantic failures, reserve recipes, or substitutions.

### Canonical grenade physics

Every accepted V2 throw uses the single game launch path and canonical launch
speed. A recipe cannot change throw speed, gravity, restitution, friction,
damping, bounce limits, or stopping behavior.

The same immutable `FGrenadeSimConfig` is copied into each grenade. Natural
bounces are not capped by a mission. Arena exits are recorded rather than
rejected. A preview prediction is simulated from the exact launch state and
compared with realized contact, bounce/rest, and exit evidence.

V2 throw metadata contains physical observations only:

- grenade ID and throw frame;
- preview start and throw camera;
- realized first contact and contact order;
- bounce count and rest frame;
- arena-exit frame and direction;
- visible observation count;
- preview-versus-realized parity.

It contains no intended family, intended outcome, target cell, sequence,
semantic-success flag, credit decision, or replacement identity.

## Planning and collection

Create an immutable V2 semi-Markov plan:

```powershell
python Scripts/v2_dataset_controller.py plan Artifacts/V2SemiMarkovPlan `
  --frame-budget 700000 `
  --episode-seconds 150 `
  --observation-rate 20 `
  --width 384 --height 384 `
  --workers 1
```

Verify it:

```powershell
python Scripts/v2_dataset_controller.py verify-plan Artifacts/V2SemiMarkovPlan
```

Run assignments with the ordinary immutable worker:

```powershell
python Scripts/dataset_worker.py Artifacts/V2SemiMarkovPlan `
  --executable <packaged-game-or-UnrealEditor-Cmd.exe> `
  --worker-id 0 --executor-id local-windows
```

Inspect progress:

```powershell
python Scripts/v2_dataset_controller.py inventory `
  Artifacts/V2SemiMarkovPlan --write-snapshot
```

## Dataset contract

Each episode records deterministic plan/assignment/replay identity, seed,
duration, source (`semi_markov`), action statistics, grenade physical evidence,
and technical termination state.

Each observation records image identity, player transform and velocity, camera,
ground/contact state, cooldown, Q/trajectory visibility, grenade counts and
states, and natural-play state.

Each transition records the ten-bit action mask, realized movement/camera axes,
Q/E edges, throw acceptance/rejection reason, cooldown before/after, and the
accepted grenade ID when a throw occurs.

Privileged trajectory points are never stored as a learning target.

## Validation boundary

V2 validation checks only:

- frame/transition alignment and technical shard integrity;
- exact Q/E action-gate semantics;
- Q-held movement suppression;
- absence of privileged trajectory geometry;
- unique accepted grenade IDs;
- 120-180 second episode duration;
- semi-Markov-only source/contract identity;
- agreement between accepted throw edges and physical throw rows;
- preview-versus-realized physics parity.

It does not validate mission success because V2 missions no longer exist. It does
not reject episodes for statistical preferences. Distribution quality is audited
visually across a review sample and reported across the whole dataset.

## Source layout

- `Source/he_grenade_game/DataGenerator/CurriculumDataGenerator.cpp`: shared V1/V2
  persistent policy, preserved V1 missions, V2 action gate, capture, and grenade
  physics.
- `Source/he_grenade_game/DataGenerator/V2ActionSemantics.*`: canonical Q/E edge
  and cooldown behavior.
- `Scripts/dataset_controller.py`: Movement V1 planner.
- `Scripts/v2_dataset_controller.py`: semi-Markov-only V2 planner and inventory.
- `Scripts/dataset_worker.py`: immutable assignment execution.
- `Scripts/finalize_production_dataset.py`: typed WebP/Parquet finalization.
- `Scripts/review_dataset.py`: technical/action/physics validation and MP4 review
  rendering.

The deleted V2 mission modules must not be reintroduced without a new explicit
design decision and a generation-before-recording guarantee:

- `Scripts/v2_catalog.py`
- `Scripts/v2_audit.py`
- `Scripts/v2_calibration.py`
- `Scripts/v2_report.py`
- `Scripts/build_v2_named_audit.py`

## Current verification status

The semi-Markov-only rewrite is a code change, not visual approval. Before any
new V1 or V2 production run:

1. build the Unreal Editor/package from the current source;
2. generate a small same-build V1/V2 semi-Markov review set;
3. visually review episode pacing, spawn/camera variety, map behavior, Q/E use,
   throws, post-throw attention, and pathological controls;
4. fix the policy if the human audit finds a problem;
5. create a fresh immutable production plan only after approval.

Historical audit folders and the previous 297-video V2 mission review are not
valid approval evidence for this implementation.
