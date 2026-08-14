# Linux RunPod generation guide

This is the operational procedure for planning, certifying, resolving, and
recording the HE Grenade Game V1/V2 dataset on a single Linux RunPod GPU. It is
written for the current one-worker invariant and a persistent network volume
mounted at `/workspace`.

## Initial qualified configuration

The bounded qualification completed on 2026-08-13 with:

- RunPod EU-RO-1;
- one NVIDIA RTX 5090 with 32 GB VRAM;
- `runpod/kasm-docker:cuda11`;
- Ubuntu 20.04 userland, NVIDIA driver 580.173.02, CUDA 13.0 runtime;
- a 600 GB network volume mounted at `/workspace`;
- Unreal Engine 5.8 Linux Development package;
- 384x384 lossless WebP observations at 20 Hz plus typed Parquet metadata.

The qualified executable is:

```text
SHA-256: 0bc31b9b2f9056af5454f4b24505d9572b906e8af5f3db55859ea605151b9740
```

The package runtime fingerprint computed on Linux is:

```text
SHA-256: 5bc0f818d4623a6055cc3a172a1782ea1b86f6832b6b6d3378e363ce55e186229
Files:   37
Bytes:   899577973
```

Those hashes identify the 2026-08-13 bounded RTX 5090 qualification package.
They are retained for provenance and must not be substituted for the later
schema-14 production binding.

## Completed schema-14 production checkpoint

Full V2 production completed on 2026-08-14 with:

- RunPod EU-RO-1 and the same 600 GB network volume;
- one NVIDIA RTX 4090;
- source commit `b49ddd038e688c01567b4899984b4c6d7a3b3a64`;
- package release tag `linux-v14-b49ddd0`;
- source/package root `/workspace/he_grenade_v2_b49ddd0`;
- campaign root `/workspace/LinuxCampaign3333333/v2-schema14-b49ddd0`;
- binary SHA-256
  `1b1ebcfa67c79c760d9515ec2750cb3481dc705e24b943991e3786707bd8a94a`;
- launcher SHA-256
  `194c3995d341cbc6d7e1b5547d7782621b67be4db8405e1492f177ea4c6c099b`;
- package-runtime SHA-256
  `2ca9be2e3858dcd6f8ce28f3b841e12d3b315c9bd9ef6c24f7802069455ebece`;
- candidate plan `v2plan-19d90e5db02a2104`;
- resolved plan `resolved-856bc5b5c7d32e77`.

All 74 Python tests passed on Linux. The exact recipe that had previously
stopped recording for lost trajectory presentation was replayed with the normal
production command: it succeeded, and 20 affected frames were annotated
`v2_visibility_degraded=true`. Physics, action, identity, and named-event
validation remained fail-closed.

Compute the package fingerprint on Linux. Filesystem ordering can make an
aggregate fingerprint calculated on Windows unsuitable as the Linux binding,
even when every individual runtime file is byte-identical.

V2 plan identity uses a separate generator-source fingerprint. The planner
canonicalizes CRLF, LF, and CR text line endings to LF before hashing source,
so the same committed generator produces the same plan ID and assignment
digests on Windows and Linux. This normalization changes identity metadata
only; recipe construction, ordering, seeds, splits, and frame targets do not
depend on source-file line endings.

The RunPod template must expose graphics as well as compute. Set:

```text
NVIDIA_DRIVER_CAPABILITIES=graphics,compute,utility,compat32
```

Keep the generation pod and its network volume in the same RunPod data center.
The production worker count is deliberately one; do not start two recorders
against the same collection.

## Build the Linux package on Windows

UE 5.8 can cross-compile the Linux package from Windows when Epic's matching
Linux toolchain is installed. From PowerShell:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" `
  BuildCookRun `
  -project="$PWD\he_grenade_game.uproject" `
  -noP4 -platform=Linux -clientconfig=Development `
  -build -cook -stage -pak -archive `
  -archivedirectory="$PWD\Artifacts\LinuxPackage" `
  -utf8output
```

The known `M_ArenaWallGrid` Vulkan sampler warning is from a legacy material
that the C++ curriculum arena does not load. It is not a successful-build
substitute: the cook must finish with zero errors, and the RunPod smoke capture
below must show the intended arena materials.

Transfer a Git checkout and the packaged directory to the network volume. The
examples below assume this layout:

```text
/workspace/he_grenade_project/
|-- Scripts/
|-- Source/
`-- Linux/
    |-- he_grenade_game.sh
    |-- Engine/
    `-- he_grenade_game/
```

The package is a build artifact and must not be committed to Git.

## Connect and install prerequisites

Use the SSH command shown by RunPod, with the private key that belongs to its
registered public key:

```bash
ssh -tt <runpod-ssh-user>@ssh.runpod.io -i ~/.ssh/<private-key>
```

RunPod's SSH gateway requires a TTY. After connecting:

```bash
sudo apt-get update
sudo apt-get install -y python3-pip python3-venv vulkan-tools xvfb

python3 -m venv /home/kasm-user/he_grenade_venv
/home/kasm-user/he_grenade_venv/bin/python -m pip install --upgrade pip
/home/kasm-user/he_grenade_venv/bin/python -m pip install \
  -r /workspace/he_grenade_project/Scripts/requirements-production.txt
```

The qualification used Pillow 12.1.1, PyArrow 25.0.0, and
imageio-ffmpeg 0.6.0 as pinned by the requirements file.

## Verify the package and Vulkan

```bash
cd /workspace/he_grenade_project
chmod +x Linux/he_grenade_game.sh
chmod +x Linux/he_grenade_game/Binaries/Linux/he_grenade_game

sha256sum Linux/he_grenade_game/Binaries/Linux/he_grenade_game
file Linux/he_grenade_game/Binaries/Linux/he_grenade_game
ldd Linux/he_grenade_game/Binaries/Linux/he_grenade_game
```

Stop if the executable hash differs from the intended build or `ldd` reports a
missing dependency.

Kasm normally supplies a display that can confuse headless Vulkan discovery.
Use the NVIDIA ICD explicitly for every Unreal process:

```bash
unset DISPLAY
export XDG_RUNTIME_DIR=/tmp/runtime-kasm
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
export VK_ICD_FILENAMES=/etc/vulkan/icd.d/nvidia_icd.json

vulkaninfo --summary
nvidia-smi
```

The Ubuntu 20.04 `vulkaninfo` utility may terminate badly after printing valid
information against a much newer NVIDIA driver. The decisive graphics test is
the Unreal smoke capture, not the utility's shutdown behavior.

Run the source tests on the pod:

```bash
/home/kasm-user/he_grenade_venv/bin/python \
  -m unittest discover -s Scripts -p 'test_*.py'
```

## Offscreen RGB smoke capture

```bash
rm -rf /workspace/he_grenade_smoke
mkdir -p /workspace/he_grenade_smoke

./Linux/he_grenade_game.sh \
  -GenerateDataset -Stage=movement -Episodes=1 -EpisodeSeconds=1 \
  -SeedStart=990001 -WorkerId=990 -ObservationRate=20 \
  -Width=384 -Height=384 -WebPEffort=0 -Mission=semi_markov \
  -BuildRevision=linux-smoke -StorageFormat=webp_parquet \
  -Output=/workspace/he_grenade_smoke/capture \
  -RenderOffscreen -unattended -nosound -NoSplash -NoVSync \
  > /workspace/he_grenade_smoke/unreal.log 2>&1

/home/kasm-user/he_grenade_venv/bin/python \
  Scripts/finalize_production_dataset.py \
  /workspace/he_grenade_smoke/capture

/home/kasm-user/he_grenade_venv/bin/python \
  Scripts/review_dataset.py \
  /workspace/he_grenade_smoke/capture --validate-only
```

The qualified smoke produced 21 observations and 20 transitions. Inspect at
least one WebP from the tar shard at its original 384x384 resolution before
continuing.

## Diagnostic planner parity

Generate fresh Linux plans; never write into a Windows reference collection:

```bash
rm -rf /workspace/linux-parity

/home/kasm-user/he_grenade_venv/bin/python Scripts/dataset_controller.py plan \
  /workspace/linux-parity/v1-10000-diagnostic \
  --frame-budget 10000 --allow-infeasible-diagnostic \
  --workers 1 --recipes-per-assignment 6 --tail-single-recipes 6 \
  --episode-seconds 10 --observation-rate 20 \
  --width 384 --height 384 --storage-format webp_parquet \
  --webp-effort 0 --seed-start 710000 --split train

/home/kasm-user/he_grenade_venv/bin/python Scripts/v2_dataset_controller.py plan \
  /workspace/linux-parity/v2-33274-minimum \
  --frame-budget 33274 --workers 1 --recipes-per-assignment 8 \
  --episode-seconds 150 --observation-rate 20 \
  --width 384 --height 384 --storage-format webp_parquet \
  --webp-effort 0 --seed-start 720000 --evaluation-percent 10
```

Expected portable identities, after removing only `created_utc` from each
canonical plan JSON, are:

| Plan | Plan ID | Normalized plan SHA-256 | Recipes SHA-256 |
| --- | --- | --- | --- |
| V1 10k | `plan-b494b1c07ec20d12` | `4a4c465c4fbfb7f074182beff40dcc1ab491e345f7b367eef6c65c385bc895f3` | `40d6f52b44bd471a5778c5f6cdd56cc78f165b41f9bda5cae3d00ded29a9670c` |
| V2 minimum | `v2plan-3347fbb7b2a98fee` | `c850432cb06b22d4909c9681b07d8954f9b80e74b2bc31d015bba73b8a3b148a` | `6b6d67e9877ed0ad672c3905414d82bed4888340703e1eca8d7d6bcb1f45769b` |

Only `created_utc` may differ. Plan IDs, recipes, assignments, allocations,
catalog identity, replay identity, and generator-source identity must match.

## Full Linux production planning

Create the production candidate plans independently on Linux:

```bash
export CAMPAIGN=/workspace/LinuxCampaign3333333
export V1_CANDIDATE="$CAMPAIGN/candidate-plans/v1-1111111"
export V2_CANDIDATE="$CAMPAIGN/v2-schema14-b49ddd0/candidate"
export GAME=/workspace/he_grenade_v2_b49ddd0/Linux/he_grenade_game.sh

mkdir -p "$CAMPAIGN/candidate-plans" "$CAMPAIGN/resolution" \
  "$CAMPAIGN/v2-schema14-b49ddd0"

/home/kasm-user/he_grenade_venv/bin/python Scripts/dataset_controller.py plan \
  "$V1_CANDIDATE" \
  --frame-budget 1111111 --workers 1 \
  --recipes-per-assignment 32 --tail-single-recipes 64 \
  --episode-seconds 150 --observation-rate 20 \
  --width 384 --height 384 --storage-format webp_parquet \
  --webp-effort 0 --seed-start 410000 --split train

/home/kasm-user/he_grenade_venv/bin/python Scripts/dataset_controller.py \
  verify-plan "$V1_CANDIDATE"

/home/kasm-user/he_grenade_venv/bin/python Scripts/v2_dataset_controller.py plan \
  "$V2_CANDIDATE" \
  --frame-budget 2222222 --workers 1 --recipes-per-assignment 32 \
  --episode-seconds 150 --observation-rate 20 \
  --width 384 --height 384 --storage-format webp_parquet \
  --webp-effort 0 --seed-start 510000 --evaluation-percent 10

/home/kasm-user/he_grenade_venv/bin/python Scripts/v2_dataset_controller.py \
  verify-plan "$V2_CANDIDATE"
```

With the current source and arguments, the candidate identities must be:

```text
V1: plan-ed06f39e9f221ef5
V2: v2plan-19d90e5db02a2104
```

Do not proceed if the IDs or exact frame allocations differ from
`CAMPAIGN_3333333.md`.

For schema 14, Windows and Linux produced the same V2 plan ID. All 171
assignment JSON files and `recipes.jsonl` were byte-identical. The only byte
difference was `created_utc` in `collection-plan.json`; after removing that
field, both canonical manifests hashed to
`1f745a642ed4039a3b2a2a9abb466c7c49b9f8608f40d84c02719468d453866d`.

## Linux certification and resolution

Certificates are bound to the exact plan, recipes, assignments, executable,
and complete packaged runtime. Never copy the Windows certificate into a Linux
collection, and never replace the package after certification.

The resolver performs the original certification, bounded same-slot
replacement attempts for any rejected recipes, emits a new immutable resolved
collection, and certifies that complete collection again:

```bash
/home/kasm-user/he_grenade_venv/bin/python \
  Scripts/resolve_plan_certification.py v1 "$V1_CANDIDATE" \
  --executable "$GAME" \
  --output "$CAMPAIGN/resolution/v1" \
  --max-attempts 10 \
  > "$CAMPAIGN/v1-resolution.log" 2>&1

/home/kasm-user/he_grenade_venv/bin/python \
  Scripts/resolve_plan_certification.py v2 "$V2_CANDIDATE" \
  --executable "$GAME" \
  --output "$CAMPAIGN/v2-schema14-b49ddd0/resolution" \
  --max-attempts 10 \
  > "$CAMPAIGN/v2-schema14-b49ddd0/v2-resolution.log" 2>&1
```

Both `resolution-report.json` files must report:

```text
unresolved_count: 0
final_certification_complete: true
```

A recipe rejection is evidence, not permission to weaken a verifier. Retain
the original report and replacement lineage.

## Production recording and resume

Only record the Linux resolved collections:

```bash
export V1_RESOLVED="$CAMPAIGN/resolution/v1/resolved-collection"
export V2_RESOLVED="$CAMPAIGN/v2-schema14-b49ddd0/resolution/resolved-collection"

/home/kasm-user/he_grenade_venv/bin/python Scripts/dataset_worker.py \
  "$V1_RESOLVED" --executable "$GAME" --worker-id 0 \
  --executor-id runpod-v1 \
  > "$CAMPAIGN/v1-worker.log" 2>&1

/home/kasm-user/he_grenade_venv/bin/python Scripts/dataset_worker.py \
  "$V2_RESOLVED" --executable "$GAME" --worker-id 0 \
  --executor-id runpod-v2-schema14-b49ddd0 \
  > "$CAMPAIGN/v2-schema14-b49ddd0/v2-production-worker.log" 2>&1
```

Use `tmux`, `screen`, or another persistent session for long runs. If SSH
disconnects or the worker stops, rerun the same command with a new
`--executor-id`. Immutable validated results are inventoried and skipped; an
assignment is not accepted unless Unreal exits successfully, Parquet
finalization succeeds, and the complete dataset validator passes.

The production run used detached session `v2production14`, started at
2026-08-14T00:57:48Z, and exited successfully at 2026-08-14T11:35:45Z. A robust
launch follows this pattern:

```bash
tmux new-session -d -s v2production14 "bash -lc '
  set -o pipefail
  unset DISPLAY
  export XDG_RUNTIME_DIR=/tmp/runtime-kasm
  export VK_ICD_FILENAMES=/etc/vulkan/icd.d/nvidia_icd.json
  cd /workspace/he_grenade_v2_b49ddd0
  /home/kasm-user/he_grenade_venv/bin/python Scripts/dataset_worker.py \
    /workspace/LinuxCampaign3333333/v2-schema14-b49ddd0/resolution/resolved-collection \
    --executable /workspace/he_grenade_v2_b49ddd0/Linux/he_grenade_game.sh \
    --worker-id 0 --executor-id runpod-v2-schema14-b49ddd0 \
    2>&1 | tee /workspace/LinuxCampaign3333333/v2-schema14-b49ddd0/v2-production-worker.log
'"
```

Attach with `tmux attach -t v2production14`; detach without stopping it using
Ctrl+B, then D.

For a bounded integration test, append `--one` to process at most one eligible
assignment. The qualified tests produced:

- V1: 6 episodes, 588 stored observations, 582 transitions, 418 credited;
- V2: 8 episodes, 3,958 observations, 3,950 transitions, all credited;
- zero technical or semantic failures.

## Inventory and completion

Inspect progress without starting new generation:

```bash
/home/kasm-user/he_grenade_venv/bin/python Scripts/dataset_controller.py \
  inventory "$V1_RESOLVED" --write-snapshot

/home/kasm-user/he_grenade_venv/bin/python Scripts/v2_dataset_controller.py \
  inventory "$V2_RESOLVED" --write-snapshot
```

Completion requires V1 `budget_reached: true` and `coverage_complete: true`,
exactly 2,222,222 credited V2 frames, no unresolved certification, and no
technical or semantic failure. V1 stops only between assignments and may exceed
its 1,111,111 target. Keep all valid produced observations, including
produced-but-not-credited context at episode ends.

The completed inventories are:

| Stage | Assignments | Credited | Produced | Technical failures | Semantic failures |
| --- | ---: | ---: | ---: | ---: | ---: |
| V1 | 192 | 1,115,586 | 1,152,179 | 0 | 0 |
| V2 | 171 | 2,222,222 | 2,229,369 | 0 | 0 |

V2 reported four visibility-degraded recipe IDs and otherwise validated every
result. The complete campaign occupies approximately 329 GiB: about 112 GiB for
the V1 resolved collection and 215 GiB for the V2 schema-14 campaign, plus plan,
certificate, log, and diagnostic evidence. RunPod's shared-filesystem `df`
output is not necessarily the quota of the individual network volume.

The V2 inventory snapshot is:

```text
/workspace/LinuxCampaign3333333/v2-schema14-b49ddd0/resolution/
  resolved-collection/inventory/inventory-snapshot-032d154bbde157f3.json
```

To render a review MP4 from authoritative stored frames without re-simulating,
use `review_dataset.py` with an episode ID:

```bash
FF=$(/home/kasm-user/he_grenade_venv/bin/python -c \
  'import imageio_ffmpeg; print(imageio_ffmpeg.get_ffmpeg_exe())')
/home/kasm-user/he_grenade_venv/bin/python Scripts/review_dataset.py \
  <assignment-output-directory> --episode <episode-id> \
  --output <review-directory> --ffmpeg "$FF"
```

After completion, preserve:

- candidate and resolved plan files;
- `execution-build.json` and bound certificates;
- all resolution reports and rejected evidence;
- immutable result JSON files and finalized tar shards;
- final V1 and V2 inventory snapshots;
- worker and certification logs;
- executable and package runtime hashes.
