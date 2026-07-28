# Multiplayer Architecture Guideline

Status: approved design for the first multiplayer implementation.

This document is the source of truth for multiplayer work. Implementation may
refine names and low-level details, but it must not change the authority model,
session model, arena protocol, or match lifecycle without updating this
document first.

## 1. Product Contract

The first multiplayer release has the following fixed scope:

- Two players.
- Cross-store PC play.
- One player hosts a listen server and also plays locally.
- Epic Online Services (EOS) provides authentication, public session discovery,
  connection establishment, NAT traversal, and relay fallback.
- The host's listen server is authoritative for all gameplay.
- The main menu contains a public server browser.
- Players may join only while the session is in the lobby/pre-match phase.
- A match does not migrate to a new host.
- A guest may reconnect to their reserved slot for 45 seconds.
- If the host disconnects, the match ends.
- Voice chat, ranked play, skill matchmaking, spectators, and dedicated servers
  are outside the first implementation.

This is a casual competitive architecture. A listen host has an unavoidable
latency and trust advantage. Ranked or high-integrity competition would require
dedicated authoritative servers later.

## 2. Online-Service Decision

### 2.1 Canonical service

EOS is the canonical multiplayer service for every storefront build. EOS owns:

- Product User IDs used by gameplay.
- Public sessions.
- Session search and join.
- P2P connection addressing.
- Relay fallback.
- Cross-store compatibility.

Steam Online Subsystem is not the canonical session directory because it does
not provide a general identity and session system for players outside Steam.

### 2.2 Unreal integration

Use Unreal's established Online Subsystem interfaces for the first shipping
implementation, not the newer Online Services API:

- `OnlineSubsystem`
- `OnlineSubsystemUtils`
- `OnlineSubsystemEOS`
- `SocketSubsystemEOS`
- `OnlineSubsystemEOSPlus` where native-store integration is required
- `OnlineSubsystemSteam` in the Steam distribution build

The Steam build uses EOS Plus with Steam as its native platform service. The
EOS session remains the cross-store session of record. Non-Steam builds use EOS
directly. Both build variants must use the same EOS product, compatible
sandbox/deployment, multiplayer protocol version, and gameplay content version.

EOS Plus is used only as the Unreal bridge to the native Steam identity and
features. The game must not depend on Steam session search for cross-store play.

### 2.3 Identity

EOS Product User ID is the canonical in-game player identifier.

- A Steam player authenticates using their Steam platform credentials and is
  represented in EOS by a Product User ID.
- An Epic Games Store player authenticates through the Epic/EOS path and is
  represented by a Product User ID.
- Gameplay data, session membership, reconnect reservations, and player slots
  use the EOS Product User ID, not a Steam ID or display name.
- Display names are presentation data and are never identity keys.

The EOS crossplay/social overlay and unified cross-store friend list are
deferred. The public browser does not depend on them. Friend invitations may be
added after the browser and core session flow are stable.

### 2.4 Environments and credentials

Use distinct EOS deployments for development/staging and production. Do not
mix test sessions with live sessions. Store-specific build configuration must be
generated or selected as part of packaging; developers must not manually edit
the same checked-in configuration back and forth.

No private backend or organization secret may be committed to the repository or
embedded in a client build. Only credentials intended by EOS for distribution
in a game client may be packaged.

## 3. Session Contract

### 3.1 Public browser

The first version provides:

- Host Public Game.
- Refresh Public Games.
- Join selected game.
- Leave game.

Private sessions and invitation codes are a follow-up. Automatic skill
matchmaking is not part of the first version.

Each advertised session contains at least:

- Multiplayer protocol version.
- Content/build compatibility ID.
- Session phase (`Lobby` or `InProgress`).
- Current player count.
- Maximum player count, fixed at two.
- Host display name.
- Map width and height.
- Optional region label.
- Joinable flag.

The browser:

- Requests at most 50 results.
- Shows only compatible, public, joinable sessions with an open slot.
- Displays ping when EOS provides a meaningful value.
- Never displays or requires a raw IP address.
- Does not refresh more often than once every five seconds.
- Treats search, join, travel, and login as asynchronous operations.

### 3.2 Hosting

Host flow:

1. Authenticate with EOS.
2. Create an EOS public session with two public connections.
3. Travel to the gameplay map as a listen server.
4. Enter the `Lobby` match phase.
5. Generate the authoritative arena layout.
6. Wait for the guest to join, receive the arena, and report ready.
7. Start the countdown only after both players are ready.

The gameplay map also serves as the pre-match lobby in v1. A separate 3D lobby
map is not required.

### 3.3 Joining

Guest flow:

1. Authenticate with EOS.
2. Search for public sessions.
3. Select a compatible joinable session.
4. Join through the EOS session interface.
5. Resolve the EOS connection string.
6. Client-travel to the listen server.
7. Receive and build the authoritative arena snapshot.
8. Verify its checksum and report ready.
9. Receive the assigned player side and wait for the countdown.

### 3.4 Session phase and joining in progress

The session is joinable only in `Lobby`.

When the match enters `Countdown` or `InProgress`, the server updates the EOS
session to reject new players. A reconnecting guest with a valid reserved EOS
Product User ID is the only exception.

### 3.5 Cleanup

All failure and exit paths must clean up their EOS state:

- Failed host travel destroys the created session.
- Failed join leaves the joined session before returning to the browser.
- Normal host exit destroys the session.
- Normal guest exit leaves the session.
- Network failure returns the guest to the main menu with a useful reason.
- Stale session results are removed from the browser after a failed join.

## 4. Unreal Authority and Ownership

### 4.1 Server-only responsibilities

The listen server decides:

- Arena dimensions and layout.
- Player side and spawn point.
- Match phase and countdown.
- Grenade acceptance, spawn, simulation, collision, and detonation.
- Damage, death, scoring, and round outcome.
- Breakable-object destruction.
- Tile-collapse warning phase and destruction.
- Cooldowns and whether a throw is legal.

A client may request an action, but it never sends an authoritative result.

### 4.2 Class responsibilities

The implementation introduces or formalizes these roles:

- `UGGSessionSubsystem` (`UGameInstanceSubsystem`)
  - EOS login, create, search, join, leave, and destroy.
  - Connection/travel orchestration.
  - Browser-facing asynchronous state and error reporting.
- `Ahe_grenade_gameGameMode`
  - Remains server-only.
  - Generates the arena description.
  - Assigns player sides and spawn slots.
  - Owns server match rules and validates readiness.
- `AGrenadeGameState`
  - Replicated match phase.
  - Phase end time expressed in server world time.
  - Current round and shared score if applicable.
  - Reference to the arena replication state.
- `AGrenadePlayerState`
  - EOS Product User ID association.
  - Assigned side.
  - Ready/loading state.
  - Score and reconnect reservation state.
- `AArenaReplicationState`
  - Always relevant replicated actor.
  - Owns the immutable arena snapshot and layout revision.
  - Owns compact changing arena state.
  - Becomes dormant after its initial snapshot when possible.
- `Ahe_grenade_gameCharacter`
  - Uses Unreal's server-authoritative character movement.
  - Sends owned action requests to the server.
- `UGrenadeThrowerComponent`
  - Builds immediate local trajectory inputs for the owning player.
  - Requests throws from the server.
  - Does not directly spawn an authoritative grenade on a client.
- `AGrenadeActor`
  - Spawned only by the server.
  - Replicates authoritative movement/state.
  - Clients interpolate visual movement and do not independently decide hits.

The exact class filenames may differ, but these boundaries are mandatory.

### 4.3 GameMode versus GameState

`GameMode` does not exist on remote clients. It may generate and validate the
arena on the server, but it must place client-visible shared state in replicated
actors such as `GameState`, `PlayerState`, or `AArenaReplicationState`.

HUD code reads replicated state. It must not infer authoritative match state from
local timers or a local `GameMode`.

## 5. Arena Snapshot Protocol

### 5.1 Chosen strategy

The host sends the complete arena description once during loading. Every
machine constructs the same arena from that description.

This is preferred over independently rerunning the random generator because:

- It cannot drift because of platform or future algorithm changes.
- Dynamic map dimensions are explicit.
- The complete layout is small enough for a one-time transfer.
- Reconnect and debugging can reproduce the exact arena.

The server's arena remains authoritative. Client construction exists for
rendering, collision prediction, and a consistent trajectory preview.

### 5.2 Snapshot contents

The immutable snapshot contains:

- Snapshot schema version.
- Multiplayer protocol version.
- Layout revision.
- Original random seed for diagnostics and replay.
- Grid dimensions.
- Cell size, pitch, and relevant height configuration.
- Arena origin/basis information needed to reconstruct world transforms.
- Explicit tile records or a compact full tile occupancy/state representation.
- Every placed object.
- Both player spawn slots.
- Collapse configuration.
- Snapshot checksum.

Every arena object record contains:

- Stable numeric object ID.
- Object type.
- Integer grid coordinate.
- Cardinal orientation or panel orientation.
- Required size/variant fields.
- Initial flags.

Asset paths and materials are content identified by the compatible game build;
they are not transmitted as arbitrary client-provided strings.

### 5.3 Stable IDs

Tiles and objects receive stable IDs when the server finalizes the snapshot.
All later state changes refer to these IDs:

- Break object `37`.
- Break tile `218`.
- Set current collapse ring to `3`.

Actor names, pointer values, and floating-point world locations are not network
identifiers.

### 5.4 Size and serialization

The target serialized snapshot size is at most 32 KiB for normal arenas.

- Use compact enums and integer grid coordinates.
- Do not replicate a world transform when it can be derived from a grid
  coordinate and orientation.
- Log the serialized byte size and checksum in development builds.
- If future maps exceed safe single-payload limits, add reliable chunking with
  sequence numbers and a final checksum. Do not silently truncate.

The first implementation may use a replicated `UPROPERTY` with `RepNotify` if
the measured snapshot fits safely. The wire format must still be versioned.

### 5.5 Construction and readiness

Server:

1. Generate and validate a layout.
2. Assign stable IDs.
3. Serialize and checksum it.
4. Store it in `AArenaReplicationState`.
5. Build the server's authoritative collision/visual arena.

Remote client:

1. Receive the complete snapshot.
2. Validate schema and protocol versions.
3. Build local arena geometry and collision.
4. Calculate the checksum.
5. Send `ServerReportArenaReady(LayoutRevision, Checksum)`.

The server starts only when:

- Exactly two valid players are present.
- Both have the expected layout revision.
- Both reported the authoritative checksum.

A checksum mismatch is a compatibility failure. The match must not start.

The listen host must build the arena once. It must not duplicate arena objects
for its local-player view.

### 5.6 Refactoring requirement

Arena generation must be separated into two operations:

1. `GenerateLayout`: server-only random decisions producing pure data.
2. `BuildArenaFromLayout`: deterministic realization of supplied data.

The existing `Ahe_grenade_gameGameMode` generation code must not remain a
single function that both chooses random content and creates server-only actors.

## 6. Changing Arena State

The immutable layout is not resent for normal gameplay changes.

`AArenaReplicationState` maintains compact authoritative state:

- Broken-object IDs or bitset.
- Broken-tile IDs or bitset.
- Current collapse ring.
- Collapse phase.
- Next collapse server time.
- Dynamic-state revision.

Persistent state is replicated reliably. Cosmetic effects may use unreliable
multicast events, but every effect must be reconstructible from persistent
state. A missed sound or particle must not produce a different collision state.

Tile warning color and the bottom-left countdown are derived from the replicated
server deadline and synchronized server world time. The server does not
replicate a new timer number every frame.

On reconnect, the guest receives:

- The immutable arena snapshot.
- The latest complete dynamic arena state.
- The current match phase and server deadlines.

## 7. Player and Match State

### 7.1 Player sides

The server assigns the host and guest to the two symmetric spawn sides. Side is
stored in `AGrenadePlayerState` and determines the spawn slot from the arena
snapshot.

Clients never choose or override their side or spawn transform.

### 7.2 Match phases

Use an explicit replicated enum:

- `Lobby`
- `ArenaSync`
- `Countdown`
- `InProgress`
- `Reconnecting`
- `PostMatch`
- `ReturningToMenu`

Phase transitions occur only on the server. Each timed phase replicates its end
as a server-world timestamp.

### 7.3 Disconnects

Host disconnect:

- The listen server disappears.
- Guest returns to the main menu.
- Display `Host disconnected`.
- No host migration is attempted.

Guest disconnect during lobby:

- Remove the guest.
- Reopen the public slot.

Guest disconnect during a match:

- Reserve the guest slot for the same EOS Product User ID for 45 seconds.
- Enter `Reconnecting`.
- Pause match progression, grenade simulation, and tile-collapse progression.
- Show a reconnect countdown to the host.
- If the same player reconnects, send current state, wait for arena readiness,
  then resume.
- If the grace period expires, transition to `PostMatch` with the guest treated
  as having left.

## 8. Grenades and Trajectory

### 8.1 Trajectory preview

The owning client draws the trajectory immediately and locally. It uses:

- The current local view/control rotation.
- Current charge/throw speed.
- Current character movement state.
- The exact received arena snapshot and synchronized break state.
- The shared `FGrenadeSim` algorithm and configuration.

Trajectory points are not streamed across the network every frame. Sending the
whole curve would add latency and bandwidth without improving the local
preview.

The smooth trajectory renderer remains a local visual system. It must not modify
authoritative collision or destruction state.

### 8.2 Throw request

On release, the owning client sends a reliable server RPC containing compact
intent:

- Throw sequence number.
- Aim/control rotation.
- Charge value or client input timestamps needed to derive it.
- Grenade type when multiple types exist.
- Arena/layout revision.

The client does not authoritatively send:

- A final grenade world position.
- A trusted initial velocity.
- Hit results.
- Bounce results.
- Destroyed object IDs.

The server validates:

- RPC ownership.
- Match phase.
- Cooldown and throw state.
- Sequence number freshness.
- Aim and charge limits.
- Layout revision.
- Plausible control rotation and character state.

The server then computes the authoritative spawn location and velocity from the
server character state, using the same launch and simulation configuration.

### 8.3 Actual grenade

The server spawns and simulates `AGrenadeActor`.

- Grenade state and movement replicate from server to clients.
- Clients interpolate corrections.
- Clients do not run an independent authoritative physics grenade.
- Only the server applies collision consequences, destruction, damage, and
  detonation.

For a static synchronized arena, the preview should match the authoritative path
closely. Exact bit-for-bit agreement is not guaranteed across latency and
independent physics worlds. The authoritative replicated grenade always wins.

Initial implementation predicts the trajectory line only. Physical
client-spawned grenade prediction, throw animation prediction, and rollback are
deferred until measured latency shows they are needed.

### 8.4 Trajectory destruction indication

The local trajectory may highlight tiles it predicts will be affected. This is
advisory. A highlight never breaks a tile. The server's actual grenade impact
decides destruction and replicates the result.

## 9. Replication Rules

- Never use a reliable RPC every tick.
- Replicate state needed to recover from packet loss or reconnect.
- Use RPCs for owned requests and short-lived cosmetic notifications.
- Use `RepNotify` for state that triggers local reconstruction or presentation.
- Server deadlines replace per-frame timer replication.
- Dynamic arena components created at runtime must not be assumed to replicate
  automatically.
- Server-spawned authoritative actors must explicitly enable replication when
  clients need them.
- Client-local arena realization actors are not separately replicated actors;
  their state comes from `AArenaReplicationState`.
- The arena replication actor is always relevant.
- Static snapshot data should become dormant after delivery when practical.
- Grenades use a higher update rate than static arena state and are
  interpolated.
- Begin with a 30 Hz server/network update target and profile before increasing
  it.

## 10. Compatibility and Versioning

Maintain separate integers for:

- Multiplayer protocol version.
- Arena snapshot schema version.
- Content/build compatibility ID.

The host advertises compatibility values in the EOS session. The browser filters
known mismatches. The server validates again during connection and arena
readiness; browser filtering alone is not security or correctness.

Any incompatible enum ordering or arena serialization change increments the
arena schema. Any incompatible RPC/session contract change increments the
multiplayer protocol.

## 11. Security and Validation

- Treat all client RPC parameters as untrusted.
- Validate ownership and match phase for every gameplay RPC.
- Rate-limit throw and ready requests.
- Reject stale or duplicate throw sequence numbers.
- Clamp values before use.
- Never accept client-reported hits, damage, object destruction, scores, or
  timer completion.
- Do not expose raw host IP addresses in UI or logs intended for players.
- Do not promise protection against a malicious listen host.

Anti-cheat integration is not required for the initial casual release.

## 12. Error Handling and User Experience

Every asynchronous online action exposes a visible state:

- Signing in.
- Creating game.
- Searching.
- Joining.
- Connecting.
- Loading arena.
- Waiting for opponent.
- Reconnecting.

Failures must produce an actionable message rather than silently returning:

- EOS unavailable.
- Authentication failed.
- No compatible games found.
- Session full.
- Session already started.
- Version mismatch.
- Join failed.
- Connection timed out.
- Arena checksum mismatch.
- Host disconnected.

Buttons are disabled while their operation is in flight to prevent duplicate
session calls.

## 13. Implementation Order

Implementation proceeds in this order:

1. Add replicated `GameState`, `PlayerState`, match phases, and server timestamps.
2. Refactor arena generation into pure layout generation and layout realization.
3. Add versioned arena snapshot, stable IDs, checksum, and ready handshake.
4. Make breakage and collapse authoritative and replicated.
5. Convert throw input to a validated server RPC.
6. Make grenades server-spawned and replicate authoritative movement/results.
7. Validate two-player replication locally without EOS.
8. Add `UGGSessionSubsystem` and EOS authentication.
9. Add EOS host, public search, join, travel, leave, and destroy.
10. Add Steam distribution integration through EOS Plus.
11. Add reconnect reservation and resynchronization.
12. Test packaged cross-store builds on separate internet connections.

EOS integration is intentionally not step one. Correct server authority and
replication must work under local network testing before session discovery is
added.

## 14. Required Test Matrix

### 14.1 Local correctness

- One listen server plus one client in multi-process PIE.
- Two packaged development clients on LAN.
- Verify only the server decides throws, hits, breaks, timers, and scores.
- Verify host and guest receive opposite symmetric spawn slots.
- Verify arena checksums are identical.
- Verify no client constructs the arena twice.

### 14.2 Network conditions

Test at minimum:

- 50 ms round-trip latency.
- 100 ms round-trip latency.
- 150 ms round-trip latency.
- 1%, 3%, and 5% packet loss.
- Temporary connection loss.

Trajectory rendering must remain immediate. Actual grenades may correct smoothly,
but may never create different destruction results on the two machines.

### 14.3 EOS and storefront

- Two EOS development accounts on different machines.
- Public host appears in browser.
- Compatible guest can join without raw IP or manual port forwarding.
- Full session cannot be joined.
- In-progress session is not joinable.
- Host cleanup removes the session.
- Steam build can join a non-Steam build through the same EOS deployment.
- Relay-path testing is performed from networks where direct P2P is unavailable.

### 14.4 Arena and reconnect

- Minimum and maximum supported map dimensions.
- Snapshot below the target size or explicit chunking exercised.
- Deliberate checksum mismatch rejects readiness.
- Guest reconnect receives current broken objects, collapsed rings, timers, and
  match phase.
- Reconnect timeout produces the defined post-match result.
- Host disconnect returns the guest safely to menu.

## 15. Acceptance Criteria

Multiplayer v1 is complete only when:

- A Steam-distributed build and another EOS-compatible PC build can authenticate
  into the same EOS environment.
- A host can advertise a public two-player session.
- A guest can find and join it through the browser.
- No manual IP entry or router configuration is required in the tested relay
  path.
- Both machines build the same arena and pass the checksum handshake.
- The match cannot start before both players are ready.
- The host is authoritative for all gameplay outcomes.
- The client's trajectory is immediate and uses the same arena and simulation
  configuration as the host.
- Grenade corrections do not cause divergent damage or destruction.
- Tile collapse and HUD timing agree through server timestamps.
- Session exit, network failure, and host loss all return users to a valid UI
  state.
- The required latency, loss, reconnect, and cross-store tests pass in packaged
  builds.

## 16. Deferred Decisions

These are deliberately postponed and must not block v1:

- Throw animations and animation prediction.
- Physical client-side grenade prediction and rollback.
- Crossplay social overlay and unified friends.
- Private join codes and invitations.
- Voice chat.
- Ranked matchmaking.
- Persistent progression.
- Host migration.
- Dedicated servers.
- Spectators or more than two players.

## 17. Primary References

- Unreal Engine networking overview:
  <https://dev.epicgames.com/documentation/unreal-engine/networking-overview-for-unreal-engine>
- Unreal Engine Online Subsystem:
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/online-subsystem-in-unreal-engine>
- Unreal Engine session interface:
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/online-subsystem-session-interface-in-unreal-engine>
- Unreal Engine Online Subsystem EOS:
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/online-subsystem-eos-plugin-in-unreal-engine>
- Epic Online Services FAQ:
  <https://onlineservices.epicgames.com/faq>

