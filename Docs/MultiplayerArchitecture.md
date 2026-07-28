# HE: FALL Multiplayer Gameplay Architecture

Status: implemented and verified against UE 5.8.

Baseline: `ba9fe45` on `main`.

## Scope and invariant

HE: FALL is a two-player listen-server game. EOS authenticates users, advertises
sessions, and supplies transport. EOS never grants gameplay authority. The
listen server is the only gameplay authority, including for its local host
player.

The server exclusively decides arena layout, throws, projectile motion,
collision, bounce, fuse, explosions, arena destruction, collapse timing,
damage, death, falling validity, respawn, and match phase. Clients submit
intent and render mirrors. A client-local prediction object is always cosmetic.

The project stays on UE's Generic Replication System. Replication Graph, Iris,
Mover, and Network Prediction add no justified value for two players and a few
short-lived grenades.

## Baseline audit

The implementation at `ba9fe45` has one server grenade simulation, but still
has conflicting representation and synchronization paths:

- `AGrenadeActor` advances a custom server-only fixed-step simulation and
  exposes raw `FRepMovement` at 30 Hz. Clients have no projectile interpolation,
  local throw prediction, throw identity, or reconciliation.
- `UGrenadeThrowerComponent` accepts a client-supplied final spawn position,
  velocity, and fuse. It has no throw sequence, synchronized timestamp, or
  duplicate/stale-request rejection.
- `AGrenadeGameState` polls every server tile at 10 Hz into two whole byte
  arrays. Clients reapply those arrays every tick.
- Server tiles and client tiles are distinct local actors. Their array position
  and renamed actor/component paths serve as implicit identity.
- `ABreakableTile::BreakTile` scans every character on every machine, clears its
  movement base, and forces `MOVE_Falling`.
- Arena readiness is reported by an owned `PlayerController`, but the server
  accepts any revision/checksum and never gates play on complete mutable state.
- Death and respawn are server-side, but replicated alive/death/respawn state is
  absent from `PlayerState`.

These paths are replaced rather than retained behind mode checks.

## Ownership and class responsibilities

| Class | Responsibility |
| --- | --- |
| `Ahe_grenade_gameGameMode` | Server-only generation decisions, match rules, side assignment, readiness validation, collapse schedule, elimination, and respawn. |
| `AGrenadeGameState` | Replicated immutable arena snapshot, Fast Array mutable arena state, collapse warning/deadline, match phase/deadline, runtime arena realization, and stable-ID lookup. |
| `AGrenadePlayerState` | Replicated side, arena readiness revision, alive/dead state, death count, and respawn deadline. |
| `Ahe_grenade_gamePlayerController` | Client-owned RPC gateway for the arena-ready handshake. |
| `Ahe_grenade_gameCharacter` + `UGGMovementComponent` | UE Character Movement prediction, saved moves, corrections, falling, and smoothing. Ordinary transforms are never manually replicated or repeatedly set. |
| `UGrenadeThrowerComponent` | Local charge/aim/trajectory state, compact throw intent, server validation/cooldown, cosmetic predicted-grenade registry, and reconciliation. Its RPC is valid because the replicated component is owned through the possessed pawn and player controller. |
| `AGrenadeActor` | The only gameplay grenade when server-spawned; client-local instances are explicitly cosmetic and cannot damage, destroy, or publish outcomes. |

Actor role is not treated as gameplay permission for cosmetic local actors:
non-replicated client actors can have `ROLE_Authority`. Gameplay mutation also
requires the server net mode and the explicit authoritative gameplay mode.

## Arena protocol

### Immutable snapshot

The server generates the arena once using the preserved generation rules, then
captures every realized placement into a versioned snapshot. Clients never run
the random generator.

The snapshot contains:

- schema and layout revision;
- dimensions, cell measurements, origin, and material identifiers;
- a deduplicated mesh/material/collision asset table;
- every object's stable positive integer ID, type, explicit transform,
  grid coordinate/collapse ring, asset or component range, orientation, and
  gameplay flags;
- every composite mesh piece and relative transform;
- object/component/destructible counts and checksum.

Floor objects are explicit records, not implied by a seed. Stable IDs, rather
than actor names, array positions, pointers, or world-location searches, are
used for all gameplay state.

Generation actors are temporary server implementation details. After capture,
the authoritative server and every client build the same runtime
representation from the snapshot.

### Runtime representation and movement bases

Runtime arena mesh/collision components are owned by the replicated
`AGrenadeGameState` actor on every machine. Components use deterministic names
derived only from stable arena ID and component index and are marked network
addressable. Gameplay lookup still uses the integer ID.

This gives Character Movement the same replicated owning actor and stable
component path on server and client, instead of basing the two worlds on
unrelated tile actors. Static mesh/material/component data is reconstructed
locally because those visuals do not replicate automatically.

`AGameStateBase` derives from `AInfo`, whose UE 5.8 constructor hides the actor
by default. `AGrenadeGameState` explicitly unhides itself so its runtime arena
components render while remaining ordinary local mirrors; the snapshot and
Fast Array are still the replicated source of truth.

### Mutable state

Every destructible object has one `FFastArraySerializerItem`:

- stable arena ID;
- intact/destroyed enum;
- authoritative state revision;
- destruction cause metadata needed for diagnostics.

The server calls `MarkItemDirty` at the moment it destroys an object. Fast Array
add/change callbacks update client visibility and collision mirrors immediately.
The whole state is sent on a new actor channel, so late joiners receive current
state; subsequent changes send item deltas only. There is no polling array and
no client tick that reapplies destruction.

Collapse warning is a replicated state struct containing active/ring/revision
and a synchronized server deadline. The visual alpha is derived locally from
`AGameStateBase::GetServerWorldTimeSeconds`; no countdown is streamed.

### Destruction and falling

For an authoritative break:

1. The server changes the Fast Array item and increments the global revision.
2. Server runtime components for that stable ID disable collision and visuals.
3. Any Character Movement component based on one of those components has its
   base cleared once and its next floor check forced.
4. Character Movement's normal floor query selects falling; gameplay code does
   not call `SetMovementMode(MOVE_Falling)`.
5. Fast Array callbacks perform the collision/visual mirror change on clients.

The one-time base invalidation is required for a stationary character because
UE 5.8 `PhysWalking` is allowed to reuse a cached floor on a zero-delta move.
The subsequent walking/falling transition remains standard Character Movement.

## Readiness and late join

A client may play only after all of these are true:

- immutable snapshot is present, schema is supported, and checksum matches;
- runtime object/component counts match the snapshot;
- the Fast Array contains the full destructible set;
- the highest received mutable revision matches the replicated global revision.

The owning `PlayerController` sends one rate-limited reliable ready RPC with
layout revision, checksum, and mutable revision. `GameMode` validates all three
against `GameState` before updating `PlayerState`.

The host is marked ready from the already-built authoritative representation.
Pawns are gated once with Character Movement disabled while loading. When the
server accepts readiness and the match phase permits play, it enables normal
Character Movement once. A late joiner receives the same initial-property and
Fast Array full-state path before its pawn is enabled.

## Grenade protocol

### Local preview and request

Trajectory rendering remains immediate and client-local. It may highlight
stable IDs cosmetically, but never changes collision or mutable arena state.

Release allocates a monotonically increasing `ThrowId` and sends one reliable
server RPC containing:

- `ThrowId`;
- quantized aim direction and view rotation;
- quantized charge/held duration and remaining fuse;
- estimated synchronized server release time;
- arena layout revision.

The request contains no hit, bounce, damage, destroyed ID, trusted velocity, or
authoritative grenade transform.

The server rejects stale/duplicate IDs and validates ownership, readiness,
match phase, cooldown, finite values, timestamp window, layout revision, aim
delta, hold/charge/fuse consistency, spawn proximity, and speed bounds. It
derives the spawn and velocity from the authoritative character plus validated
intent.

Reliable RPCs are limited to this discrete release/ready flow. Character
Movement retains its built-in unreliable saved-move RPC path; no per-frame
reliable RPC exists.

### Prediction and reconciliation

The remote client immediately spawns an unreplicated cosmetic grenade keyed by
`ThrowId`. It uses the shared trajectory stepper against the arena mirror, but
has no gameplay collision component and explicit cosmetic mode prevents damage,
destruction, or authoritative event publication.

The server spawns the only replicated gameplay grenade. It replicates
`ThrowId`, owner, initial velocity, fuse deadline, movement, and an increasing
authoritative event record for bounce, arena impact/destruction, and explosion.
Arena destruction itself persists in the Fast Array.

When the authoritative actor arrives on the throwing client:

1. it finds the predicted visual through the owned thrower component and ID;
2. the authoritative visual is hidden to prevent a duplicate;
3. the prediction stops simulating and blends to the authoritative visual;
4. the authoritative visual is revealed and the prediction is destroyed.

A rejection removes the matching prediction through an owning-client RPC.

### Remote smoothing

The server keeps the preserved `FGrenadeSim` fixed-step collision behavior.
Clients never run that simulation for gameplay grenades.

`UProjectileMovementComponent` is attached to the grenade as an interpolation
helper with simulation disabled on proxies. `PostNetReceiveLocationAndRotation`
feeds each `FRepMovement` target to `MoveInterpolationTarget`; the visual child
uses built-in projectile interpolation while the collision/root remains the
latest server target. Velocity from `FRepMovement` is retained for diagnostics
and interpolation behavior.

The initial target is 20 updates/second. Interpolation, event-triggered
`ForceNetUpdate`, and measurement are used before any frequency increase.

Explosion is replicated as state before delayed authoritative actor teardown.
Prediction can display no damage, break, bounce result, or explosion outcome
that the server did not publish.

## Match, death, and reconnect state

`GameMode` alone transitions replicated phases and deadlines. Collapse starts
with `InProgress`, pauses for `Reconnecting`, and resumes from its remaining
authoritative time.

Elimination is idempotent and server-only. `PlayerState` publishes alive state,
death count, state revision, and respawn server time. The server destroys and
restarts the pawn after the delay; clients never eliminate or respawn a player.

## Bandwidth and measurement

The replicated data model is:

- immutable layout: one initial payload with logged estimated bytes, counts,
  revision, and checksum;
- mutable arena: one full destructible set for a new connection, then roughly
  one small Fast Array item per break rather than whole floor/object arrays;
- throw request: one compact reliable RPC per release;
- grenade movement: approximately 20 `FRepMovement` updates/second while
  active, plus event state on bounce/impact/explosion;
- collapse and match timers: state transition plus deadline only.

The final rendered two-process run measured a 52,178-byte logical layout
snapshot for 341 objects, eight deduplicated assets, 12 composite component
records, and 336 destructibles. Once connected, `stat net` and the in-game
metrics showed roughly 2-4 KiB/s per direction during ordinary play and the
accelerated collapse scenario. The final samples were 1,981/2,233 bytes/s
server in/out and 2,076/1,863 bytes/s client in/out. The client received 51
movement targets for a remote normal throw while the interpolated visual's
largest rendered step was 49.84 cm versus a 146 cm largest target delta.
The local predicted throw reconciled from 9.57 cm in 183.3 ms. No reliable
buffer overflow occurred.

Network traces and `.nprof` captures accompany the logged counters. The 20 Hz
movement target was retained because interpolation reduced visible target
stepping without requiring a brute-force update-frequency increase.

## Verification contract

Verification uses separate server/client OS processes, not only single-process
PIE. The automated runtime scenario logs server/client layout checksum,
destructible count, global revision, each destroyed stable ID, throw ID,
authoritative grenade events, movement mode, death revision, and respawn.

Runs cover zero emulation; 50-100 ms with jitter; 200 ms; and 1%, 5%, and 10%
loss. A server-side opt-in scenario harness covers host/client throws under
both players, simultaneous throws, both generated obstacle classes, sequential
breaks, timed rings, standing-on-break falling, death/respawn, and total
collapse. Separate late-join/reconnect and rendered runs cover initial-state
reconstruction, mesh/material/checksum parity, local reconciliation, remote
interpolation, `stat net`, Network Insights traces, and Network Profiler data.
The concrete commands and results are in `Docs/MultiplayerTestEvidence.md`.

## UE 5.8 authorities

Official UE 5.8 documentation:

- [Networking Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-overview-for-unreal-engine)
- [Remote Procedure Calls](https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-procedure-calls-in-unreal-engine)
- [Actor Owner and Owning Connection](https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-owner-and-owning-connection-in-unreal-engine)
- [Replicate Actor Properties](https://dev.epicgames.com/documentation/en-us/unreal-engine/replicate-actor-properties-in-unreal-engine)
- [Actor Role and Remote Role](https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-role-and-remote-role-in-unreal-engine)
- [Networked Character Movement](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine)
- [Movement Components](https://dev.epicgames.com/documentation/en-us/unreal-engine/movement-components-in-unreal-engine)
- [UProjectileMovementComponent](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UProjectileMovementComponent)
- [FFastArraySerializer](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/NetCore/FFastArraySerializer)
- [Testing Multiplayer](https://dev.epicgames.com/documentation/en-us/unreal-engine/testing-multiplayer-in-unreal-engine)
- [Network Emulation](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-network-emulation-in-unreal-engine)

Installed UE 5.8 source inspected:

- `Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h`
- `Engine/Source/Runtime/Engine/Classes/Engine/ReplicatedState.h`
- `Engine/Source/Runtime/Engine/Private/Actor.cpp`
- `Engine/Source/Runtime/Engine/Private/ActorReplication.cpp`
- `Engine/Source/Runtime/Engine/Private/NetDriver.cpp`
- `Engine/Source/Runtime/Engine/Private/DataChannel.cpp`
- `Engine/Source/Runtime/Engine/Private/DataReplication.cpp`
- `Engine/Source/Runtime/Engine/Private/RepLayout.cpp`
- `Engine/Source/Runtime/Engine/Classes/GameFramework/ProjectileMovementComponent.h`
- `Engine/Source/Runtime/Engine/Private/Components/ProjectileMovementComponent.cpp`
- `Engine/Source/Runtime/Engine/Private/Character.cpp`
- `Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp`
- `Engine/Source/Runtime/Net/Core/Classes/Net/Serialization/FastArraySerializer.h`

Relevant source behavior:

- `AActor::GetNetConnection` walks the owner chain; server RPC execution checks
  the receiving channel's `bNetOwner`.
- `FRepMovement` combines transform and velocity; the default actor receive path
  applies each target directly, so a non-character projectile needs explicit
  smoothing.
- projectile interpolation is opt-in and requires an interpolated visual plus
  `MoveInterpolationTarget` on network updates.
- Character Movement uses autonomous saved moves/corrections and simulated-proxy
  smoothing; ordinary character transforms must not be driven beside it.
- RepNotify is queued after property receipt, while Fast Array add/change
  callbacks run around delta deserialization; readiness therefore tests the
  complete snapshot and mutable revision rather than assuming property order.
- Fast Arrays require `MarkItemDirty` for adds/changes and identify items by
  replication ID/key; array index order is not a gameplay identity.
