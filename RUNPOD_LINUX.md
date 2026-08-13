# Linux RunPod generation guide

This is the operational procedure for planning, certifying, resolving, and
recording the HE Grenade Game V1/V2 dataset on a single Linux RunPod GPU. It is
written for the current one-worker invariant and a persistent network volume
mounted at `/workspace`.

## Qualified configuration

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

Compute the package fingerprint on Linux. Filesystem ordering can make an
aggregate fingerprint calculated on Windows unsuitable as the Linux binding,
even when every individual runtime file is byte-identical.

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
| V2 minimum | `v2plan-d3a839dff5c13469` | `d5ee6ea0dbdebc417f5154c4d5095120772adfbd4a2fed832f27e4f538d41e51` | `6b6d67e9877ed0ad672c3905414d82bed4888340703e1eca8d7d6bcb1f45769b` |

Only `created_utc` may differ. Plan IDs, recipes, assignments, allocations,
catalog identity, replay identity, and generator-source identity must match.

## Full Linux production planning

Create the production candidate plans independently on Linux:

```bash
export CAMPAIGN=/workspace/LinuxCampaign3333333
export V1_CANDIDATE="$CAMPAIGN/candidate-plans/v1-1111111"
export V2_CANDIDATE="$CAMPAIGN/candidate-plans/v2-2222222"
export GAME=/workspace/he_grenade_project/Linux/he_grenade_game.sh

mkdir -p "$CAMPAIGN/candidate-plans" "$CAMPAIGN/resolution"

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
V2: v2plan-f6963ef7654ff948
```

Do not proceed if the IDs or exact frame allocations differ from
`CAMPAIGN_3333333.md`.

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
  --output "$CAMPAIGN/resolution/v2" \
  --max-attempts 10 \
  > "$CAMPAIGN/v2-resolution.log" 2>&1
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
export V2_RESOLVED="$CAMPAIGN/resolution/v2/resolved-collection"

/home/kasm-user/he_grenade_venv/bin/python Scripts/dataset_worker.py \
  "$V1_RESOLVED" --executable "$GAME" --worker-id 0 \
  --executor-id runpod-v1 \
  > "$CAMPAIGN/v1-worker.log" 2>&1

/home/kasm-user/he_grenade_venv/bin/python Scripts/dataset_worker.py \
  "$V2_RESOLVED" --executable "$GAME" --worker-id 0 \
  --executor-id runpod-v2 \
  > "$CAMPAIGN/v2-worker.log" 2>&1
```

Use `tmux`, `screen`, or another persistent session for long runs. If SSH
disconnects or the worker stops, rerun the same command with a new
`--executor-id`. Immutable validated results are inventoried and skipped; an
assignment is not accepted unless Unreal exits successfully, Parquet
finalization succeeds, and the complete dataset validator passes.

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

Completion requires exactly 1,111,111 credited V1 frames and 2,222,222
credited V2 frames, complete required coverage, no unresolved certification,
and no technical or semantic failure. Keep all valid produced observations,
including produced-but-not-credited context at episode ends.

The qualification shards averaged approximately 98-103 KB per observation,
projecting roughly 335-345 GB for the complete dataset before extra diagnostic
and retry evidence. A 600 GB volume is a reasonable first-run allocation, but
monitor the collection with `du -sh`; RunPod's shared-filesystem `df` output is
not necessarily the quota of the individual network volume.

After completion, preserve:

- candidate and resolved plan files;
- `execution-build.json` and bound certificates;
- all resolution reports and rejected evidence;
- immutable result JSON files and finalized tar shards;
- final V1 and V2 inventory snapshots;
- worker and certification logs;
- executable and package runtime hashes.
