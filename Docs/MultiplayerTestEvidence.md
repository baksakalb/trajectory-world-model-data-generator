# HE: FALL Multiplayer Verification Evidence

Date: 2026-07-29

Baseline: `ba9fe45` on `main`.

Implementation under test: the server-authoritative multiplayer replacement
described in `Docs/MultiplayerArchitecture.md`.

## Test method

`Scripts/Run-MultiplayerVerification.ps1` launches a listen host and a client
as separate OS processes. It uses the local IP net driver only for deterministic
transport testing; it does not edit or disclose EOS configuration. The same
gameplay authority and replication code runs with EOS transport.

The runner:

- fixes arena generation at `-GGArenaSeed=424242`;
- enables a compact client-intent self-test on both locally controlled pawns;
- enables an opt-in server scenario harness for the exact authority cases;
- accelerates collapse/respawn only to keep each run bounded;
- supports rendered editor processes and archived Development executables;
- optionally records Network Insights `.utrace` and Network Profiler data;
- parses stable-ID destruction logs into complete server/client maps;
- requires identical ID, state, item revision, and final global revision;
- fails on ownership, NetGUID/path resolution, movement-base resolution, or
  reliable-buffer warnings.

Example:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\Scripts\Run-MultiplayerVerification.ps1 `
  -Profile Jitter75 -DurationSeconds 56 -NetworkTrace
```

For an archived build, pass `-PackagedExecutable` with the archive-root
executable. The runner resolves and owns the inner binary so teardown does not
leave a child process behind.

## Builds

| Build | Result |
| --- | --- |
| `Build.bat he_grenade_gameEditor Win64 Development ... -WaitMutex -NoHotReload` | PASS, no compiler warnings |
| `Build.bat he_grenade_game Win64 Development ... -WaitMutex -NoHotReload` | PASS, no compiler warnings |
| `RunUAT.bat BuildCookRun ... -clientconfig=Development -build -cook -stage -package -pak -archive` | PASS |
| Final visibility-fix Development build/cook/package/archive | PASS; `Saved/PackagedVerification/Development-visibilityfix2-20260729` |

The unrelated Shooter sample's UE 5.8 `STATETREE_POD_INSTANCEDATA`
deprecation was updated to Epic's prescribed constructed-trivial macro so the
project builds without that warning.

## Profile matrix

Each row used a separate listen-host process and client process. The counts are
the complete destroyed-state maps reconstructed from the logs, not only the
last global revision.

| Run | Emulation | Authority IDs | Client IDs | Exact state parity | Result |
| --- | --- | ---: | ---: | --- | --- |
| `Zero-20260729-010318` | none, rendered + trace | 253 | 253 | yes | NETWORK PASS; visual regression found later |
| `Jitter75-20260729-000150` | 75 ms lag, 25 ms variance | 195 | 195 | yes | PASS |
| `Lag200-20260729-000427` | 200 ms lag | 285 | 285 | yes | PASS |
| `Loss1-20260729-000621` | 1% loss | 252 | 252 | yes | PASS |
| `Loss5-20260729-000737` | 5% loss | 252 | 252 | yes | PASS |
| `Loss10-20260729-000844` | 10% loss | 252 | 252 | yes | PASS |
| `Zero-20260729-002947` | disconnect/reconnect | 253 | 253 | yes | PASS |
| `Zero-20260729-010115` | archived Development executables | 253 | 253 | yes | NETWORK PASS; predates visual fix |
| `Zero-20260729-012807` | none, rendered after visibility fix | 115 | 115 | yes | PASS for startup/throw/destruction scope |

The logs confirm that the requested `PktLag`, `PktLagVariance`, and `PktLoss`
settings were applied by each corresponding process.

## Runtime visibility regression and fix

The first rendered evidence run proved the network assertions but did not prove
the pixels: its arena was black. UE 5.8 `AInfo::AInfo` calls `SetHidden(true)`,
and `AGameStateBase` inherits that setting. Because the redesigned runtime
meshes are components of `AGrenadeGameState`, they were present, correctly
transformed, and collidable but inherited a hidden owning actor.

`AGrenadeGameState` now explicitly clears the actor hidden state. A native-RHI
host capture after the fix shows the arena walls and HUD, and rendered
two-process run `Zero-20260729-012807` confirms that the client applies the same
341-object snapshot, both players become ready, the match reaches
`InProgress`, both throw gateways work, and the host/client stable-ID maps match
at revision 115. That short regression run intentionally ended before full
collapse; the full-duration rows above cover the collapse behavior.

The final archived Development executable was also captured through the native
RHI after `BeginPlay` and shows the generated floor, walls, obstacles, and HUD;
it is no longer a black frame.

## Gameplay matrix

The final zero-latency editor and packaged runs both passed all of these
machine-checked conditions:

- listen host started and client applied the immutable snapshot;
- two independently owned player gateways reported validated readiness;
- host and client throw-intent RPCs were both accepted;
- the client spawned a cosmetic prediction and reconciled it;
- host throw under host;
- host throw under client;
- client throw under client;
- client throw under host;
- two server grenades spawned in the same tick for simultaneous throws;
- bounce against a breakable obstacle;
- bounce against a static obstacle;
- multiple sequential stable-ID breaks;
- outer-ring warning and timed destruction through full collapse;
- one-time movement-base invalidation;
- ordinary Character Movement transition from walking to falling;
- authoritative elimination and delayed respawn;
- exact final stable-ID state and revision parity;
- no forbidden networking warning.

The scenario logs identify the two simultaneous grenades at the same engine
timestamp. Bounce records include the authoritative stable ID and arena type.

## Late join and reconnect

Run `Zero-20260729-002947` disconnected the original client after arena
mutation, observed the authoritative reconnect phase, and started a new client
process. The replacement client:

- received and checksum-validated the immutable snapshot;
- received already-destroyed Fast Array items before readiness;
- passed the full-state revision handshake;
- caused the server to resume `InProgress`;
- ended with the same 253 stable-ID state records and revisions as authority.

## Motion and bandwidth

The final rendered run `Zero-20260729-010318` used two PCD3D_SM6 game windows,
`stat net`, Network Profiler, and Network Insights tracing.

- Snapshot: checksum `890794753`, 341 objects, eight assets, 12 composite
  component records, 336 destructibles, estimated 52,178 bytes.
- Local throw: 9.57 cm initial reconciliation error, completed in 183.3 ms.
- Remote normal throw: 51 authoritative targets, 146 cm maximum target delta,
  49.84 cm maximum interpolated visual frame step.
- A second remote normal throw: 49 targets, 135.37 cm maximum target delta,
  58.23 cm maximum visual frame step.
- Short verification throws showed 6.32-12.48 cm maximum visual frame steps.
- Final steady samples: server 1,981/2,233 bytes/s in/out; client
  2,076/1,863 bytes/s in/out.
- Trace artifacts: `Host.log.utrace` 234,791 bytes and
  `Client.log.utrace` 217,915 bytes.
- No duplicate visible authority/prediction handoff, ownership warning,
  reliable overflow, unresolved NetGUID/path, or movement-base failure was
  logged.

The target deltas remain larger than visual frame steps because the child
visual uses `UProjectileMovementComponent` interpolation while the root stays
at the newest authoritative target. The local player sees immediate cosmetic
prediction and a bounded handoff rather than waiting for replication.

## Arena/material parity

Both rendered processes logged the same layout revision, checksum, object
count, destructible count, and asset table-derived runtime construction. The
captured `stat net` frames are retained under
`Saved/NetworkVerification/Zero-20260729-010318`, alongside the raw logs and
traces. Mutable parity was then checked independently by stable ID and item
revision.

## Diagnostic review

All profile logs were searched for:

- RPCs without an owning connection or rejected due to ownership;
- unresolved/failed NetGUID or component path;
- movement-base resolution failure;
- reliable buffer/partial bunch overflow;
- client-only destruction;
- duplicate prediction/authority visibility;
- repeated manual movement-mode forcing.

None was found. Ordinary character transforms are not manually replicated.
The only `SetActorLocation` on a character is an opt-in verification-harness
teleport used to place a target over a known intact stable-ID floor tile; it is
absent from normal gameplay.

The mutable arena no longer has a polling array or local tile mutation path.
The old generator actor's break/reset/highlight state was removed; it is now
capture-only scaffolding destroyed immediately after the server publishes the
snapshot.
