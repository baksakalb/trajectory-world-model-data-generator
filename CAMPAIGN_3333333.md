# Approved 3,333,333-Frame Campaign Checkpoint

This file preserves the human-readable Windows planning/certification checkpoint
and the completed Linux production checkpoint. JSON reports and inventory
snapshots in their respective campaign roots remain authoritative.

## Identity

| Field | Value |
| --- | --- |
| Completed UTC | 2026-08-13T04:59:43.173831Z |
| Platform | Windows |
| Unreal | 5.8 |
| Executable | `UnrealEditor-Cmd.exe` |
| Executable SHA-256 | `327209aa83f741c91e32e3b61b3a6744b18cf0adaefd89586e62f2c743f2394b` |
| Candidate V1 plan | `plan-ed06f39e9f221ef5` |
| Resolved V1 plan | `resolved-ac203dfcab087bac` |
| Candidate V2 plan | `v2plan-f6963ef7654ff948` |
| Resolved V2 plan | `resolved-7262de08ffd6f518` |
| Final status | Complete |

This table intentionally retains the historical Windows-bound V2 identity to
match its resolved plan and certificates. An intermediate portable candidate
used plan ID `v2plan-9feae38daf1e9fb5`; the final schema-14 Windows/Linux
candidate is `v2plan-19d90e5db02a2104`. The historical Windows certificate did
not authorize Linux recording; the completed Linux run used its own bound
certificate described below.

## Linux production identity

| Field | Value |
| --- | --- |
| Production date | 2026-08-14 |
| Platform | Linux, RunPod EU-RO-1 |
| GPU | NVIDIA RTX 4090 |
| Source commit | `b49ddd038e688c01567b4899984b4c6d7a3b3a64` |
| V2 schema | `trajectory_throw_v2-production-14` |
| Candidate V2 plan | `v2plan-19d90e5db02a2104` |
| Resolved V2 plan | `resolved-856bc5b5c7d32e77` |
| V2 resolution | `resolution-cc783632407bb011` |
| Linux binary SHA-256 | `1b1ebcfa67c79c760d9515ec2750cb3481dc705e24b943991e3786707bd8a94a` |
| Package runtime SHA-256 | `2ca9be2e3858dcd6f8ce28f3b841e12d3b315c9bd9ef6c24f7802069455ebece` |
| Network-volume root | `/workspace/LinuxCampaign3333333` |
| Final status | Complete |

The schema-14 Windows and Linux V2 candidate plans share the same plan ID. All
171 assignments and `recipes.jsonl` are byte-identical; the only manifest byte
difference is the expected `created_utc` value.

Local evidence root:

```text
Artifacts/WindowsCampaign3333333-20260813-112500/
```

## Planned allocation

| Stage | Semi-Markov | Missions | Total |
| --- | ---: | ---: | ---: |
| V1 | 777,778 | 333,333 | 1,111,111 |
| V2 | 1,555,555 | 666,667 | 2,222,222 |
| **Combined** | **2,333,333** | **1,000,000** | **3,333,333** |

V1 mission frames:

| Mission | Frames |
| --- | ---: |
| Object view | 146,666 |
| Contact recovery | 110,000 |
| Ramp traverse | 36,667 |
| Hoop pass | 36,667 |
| Static no-input | 3,333 |

V2 mission-family frames:

| Family | Frames |
| --- | ---: |
| Broad object surfaces | 139,789 |
| Object edges and apexes | 139,789 |
| Wall/corner rebounds | 129,036 |
| Hoop interactions | 107,525 |
| Ramp interactions | 86,016 |
| Out-of-bounds | 43,008 |
| Trajectory control | 21,504 |

Each of the 62 V2 mission types receives 10,752 or 10,753 credited frames.
The exact per-type ledger is in `windows-plan-verification-report.json`.

## Certification and resolution

V1 original certification produced five failures:

| Original recipe | Mission | Scenario | Credit cap | Engine reason |
| --- | --- | ---: | ---: | --- |
| `recipe-61b87943aac61af3` | Hoop pass | 14 | 53 | `mission_timeout` |
| `recipe-7f2c0ad2e84d25bf` | Hoop pass | 4 | 52 | `mission_timeout` |
| `recipe-3311196be9ce2c46` | Contact recovery | 266 | 61 | `mission_no_progress` |
| `recipe-f21d198192e6a9fb` | Hoop pass | 9 | 51 | `mission_timeout` |
| `recipe-e09095faf989930b` | Hoop pass | 9 | 51 | `mission_timeout` |

Every failed slot received a distinct progressively centered resample of the
same mission and scenario. All passed on attempt 1:

| Original | Replacement | Result |
| --- | --- | --- |
| `recipe-61b87943aac61af3` | `recipe-e0454d52082013b0` | Certified |
| `recipe-7f2c0ad2e84d25bf` | `recipe-86c0abfc989a8b75` | Certified |
| `recipe-3311196be9ce2c46` | `recipe-aadac62f5972c914` | Certified |
| `recipe-f21d198192e6a9fb` | `recipe-c401377e9188c19e` | Certified |
| `recipe-e09095faf989930b` | `recipe-d639a84a975b7207` | Certified |

Final results:

- V1 original: 6,339/6,344 passed, five rejected.
- V1 replacements: 5/5 passed on attempt 1.
- V1 final resolved plan: 6,344/6,344 guided recipes passed.
- V2 original prescribed missions: 4,940/4,940 passed.
- V2 final plan: 5,459/5,459 total recipes passed.
- Unresolved: zero.

## Capture status

Linux RGB production is complete:

| Stage | Validated assignments | Credited observations | Stored observations | Failures |
| --- | ---: | ---: | ---: | ---: |
| V1 | 192 | 1,115,586 | 1,152,179 | 0 |
| V2 | 171 | 2,222,222 | 2,229,369 | 0 |
| **Combined** | **363** | **3,337,808** | **3,381,548** | **0** |

V1 completed its target and all 902 required cells, with a 4,475-frame credit
overshoot because workers stop only between immutable assignments. V2 reached
its target exactly. V2 retained four visibility-degraded recipes as metadata;
there were no technical or semantic failures. The campaign uses approximately
329 GiB on the network volume.

All valid produced observations must be retained. `planned_credited_frames` is
for allocation auditing and must not be used to truncate an episode. Sequence
boundaries are defined by `(plan_id, plan_version, episode_id)`.

## Required next step

Generation, per-assignment finalization/validation, and final inventories are
complete. Preserve both resolved collections, their inventory snapshots,
certificates, execution-build bindings, result JSON, tar shards, and logs before
terminating compute. The next project phase is dataset loading and controlled
autoencoder/world-model experiments; do not join frames across episode IDs or
discard produced-but-not-credited observations.
