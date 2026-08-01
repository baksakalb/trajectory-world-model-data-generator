#!/usr/bin/env python3
"""Validate a generated shard and build review MP4s from its saved RGB frames.

The converter never renders Unreal or captures the screen. Every MP4 frame is
read directly from the authoritative tar shard in recorded frame-index order.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import tarfile
from collections import defaultdict
from pathlib import Path
from typing import Any, BinaryIO


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class DatasetValidationError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate a generated dataset and derive exact review MP4s."
    )
    parser.add_argument("dataset", type=Path, help="Directory containing dataset.json")
    parser.add_argument(
        "--output",
        type=Path,
        help="Review directory (default: <dataset>/review)",
    )
    parser.add_argument(
        "--ffmpeg",
        type=Path,
        help="Path to ffmpeg.exe; otherwise ffmpeg is resolved from PATH.",
    )
    parser.add_argument(
        "--episode",
        action="append",
        help="Only render this episode ID; may be supplied more than once.",
    )
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="Validate without requiring ffmpeg or creating MP4s.",
    )
    return parser.parse_args()


def read_json_lines(tar: tarfile.TarFile, name: str) -> list[dict[str, Any]]:
    extracted = tar.extractfile(name)
    if extracted is None:
        raise DatasetValidationError(f"Missing required metadata entry: {name}")

    records: list[dict[str, Any]] = []
    for line_number, raw_line in enumerate(extracted, start=1):
        if not raw_line.strip():
            continue
        try:
            records.append(json.loads(raw_line))
        except json.JSONDecodeError as error:
            raise DatasetValidationError(
                f"{name}:{line_number}: invalid JSON: {error}"
            ) from error
    return records


def md5_file(path: Path) -> str:
    digest = hashlib.md5(usedforsecurity=False)
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_checksum(dataset_dir: Path, shard_path: Path) -> str:
    checksum_path = dataset_dir / "checksums.md5"
    if not checksum_path.is_file():
        raise DatasetValidationError(f"Missing checksum file: {checksum_path}")

    expected_by_name: dict[str, str] = {}
    for line in checksum_path.read_text(encoding="utf-8").splitlines():
        fields = line.strip().split(maxsplit=1)
        if len(fields) == 2:
            expected_by_name[fields[1].lstrip("*")] = fields[0].lower()

    expected = expected_by_name.get(shard_path.name)
    if not expected:
        raise DatasetValidationError(
            f"No checksum entry found for shard {shard_path.name}"
        )
    actual = md5_file(shard_path)
    if actual != expected:
        raise DatasetValidationError(
            f"Checksum mismatch for {shard_path.name}: {actual} != {expected}"
        )
    return actual


def png_dimensions(stream: BinaryIO) -> tuple[int, int]:
    header = stream.read(24)
    if len(header) != 24 or header[:8] != PNG_SIGNATURE or header[12:16] != b"IHDR":
        raise DatasetValidationError("Observation is not a valid PNG stream.")
    return struct.unpack(">II", header[16:24])


def validate_dataset(
    dataset_dir: Path,
) -> tuple[
    dict[str, Any],
    Path,
    str,
    dict[str, list[dict[str, Any]]],
]:
    dataset_json_path = dataset_dir / "dataset.json"
    if not dataset_json_path.is_file():
        raise DatasetValidationError(f"Missing dataset manifest: {dataset_json_path}")

    dataset = json.loads(dataset_json_path.read_text(encoding="utf-8"))
    if not dataset.get("complete"):
        raise DatasetValidationError(
            f"Dataset is marked incomplete: {dataset.get('error', 'unknown error')}"
        )
    shards = dataset.get("shards")
    if not isinstance(shards, list) or len(shards) != 1:
        raise DatasetValidationError(
            "Preflight schema requires exactly one shard per worker run."
        )

    shard_path = dataset_dir / shards[0]
    if not shard_path.is_file():
        raise DatasetValidationError(f"Missing shard: {shard_path}")
    checksum = validate_checksum(dataset_dir, shard_path)

    expected_width = int(dataset["rgb_width"])
    expected_height = int(dataset["rgb_height"])
    records_by_episode: dict[str, list[dict[str, Any]]] = defaultdict(list)

    with tarfile.open(shard_path, mode="r:") as tar:
        names = {member.name for member in tar.getmembers() if member.isfile()}
        frames = read_json_lines(tar, "metadata/frames.jsonl")
        transitions = read_json_lines(tar, "metadata/transitions.jsonl")
        episodes = read_json_lines(tar, "metadata/episodes.jsonl")

        if len(frames) != int(dataset["observation_count"]):
            raise DatasetValidationError(
                f"Frame metadata count {len(frames)} != manifest "
                f"{dataset['observation_count']}"
            )
        if len(transitions) != int(dataset["transition_count"]):
            raise DatasetValidationError(
                f"Transition count {len(transitions)} != manifest "
                f"{dataset['transition_count']}"
            )
        if len(episodes) != int(dataset["completed_episode_count"]):
            raise DatasetValidationError(
                f"Episode count {len(episodes)} != manifest "
                f"{dataset['completed_episode_count']}"
            )

        for frame in frames:
            image_key = frame["rgb_key"]
            if image_key not in names:
                raise DatasetValidationError(f"Missing RGB observation: {image_key}")
            records_by_episode[frame["episode_id"]].append(frame)

        transition_groups: dict[str, list[dict[str, Any]]] = defaultdict(list)
        for transition in transitions:
            transition_groups[transition["episode_id"]].append(transition)

        for episode in episodes:
            episode_id = episode["episode_id"]
            episode_frames = sorted(
                records_by_episode.get(episode_id, []),
                key=lambda record: int(record["frame_index"]),
            )
            episode_transitions = sorted(
                transition_groups.get(episode_id, []),
                key=lambda record: int(record["source_frame_index"]),
            )
            expected_transitions = int(episode["actual_transitions"])
            expected_frame_indices = list(range(expected_transitions + 1))
            actual_frame_indices = [
                int(record["frame_index"]) for record in episode_frames
            ]
            actual_transition_indices = [
                int(record["source_frame_index"]) for record in episode_transitions
            ]
            if actual_frame_indices != expected_frame_indices:
                raise DatasetValidationError(
                    f"{episode_id}: discontinuous frame indices."
                )
            if actual_transition_indices != list(range(expected_transitions)):
                raise DatasetValidationError(
                    f"{episode_id}: discontinuous transition indices."
                )

            for frame in episode_frames:
                extracted = tar.extractfile(frame["rgb_key"])
                if extracted is None:
                    raise DatasetValidationError(
                        f"Could not read observation: {frame['rgb_key']}"
                    )
                dimensions = png_dimensions(extracted)
                if dimensions != (expected_width, expected_height):
                    raise DatasetValidationError(
                        f"{frame['rgb_key']}: dimensions {dimensions} != "
                        f"{(expected_width, expected_height)}"
                    )
            records_by_episode[episode_id] = episode_frames

    return dataset, shard_path, checksum, dict(records_by_episode)


def resolve_ffmpeg(explicit_path: Path | None) -> Path:
    if explicit_path:
        candidate = explicit_path.resolve()
        if candidate.is_file():
            return candidate
        raise DatasetValidationError(f"ffmpeg was not found at {candidate}")

    resolved = shutil.which("ffmpeg")
    if resolved:
        return Path(resolved)
    raise DatasetValidationError(
        "Dataset validation passed, but ffmpeg is not installed or on PATH. "
        "Install ffmpeg or pass --ffmpeg C:\\path\\to\\ffmpeg.exe."
    )


def render_episode(
    tar: tarfile.TarFile,
    ffmpeg: Path,
    output_path: Path,
    frame_records: list[dict[str, Any]],
    fps: int,
) -> None:
    command = [
        str(ffmpeg),
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-f",
        "image2pipe",
        "-framerate",
        str(fps),
        "-vcodec",
        "png",
        "-i",
        "pipe:0",
        "-an",
        "-c:v",
        "libx264",
        "-crf",
        "18",
        "-preset",
        "medium",
        "-pix_fmt",
        "yuv420p",
        str(output_path),
    ]
    process = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    assert process.stdin is not None
    try:
        for frame in frame_records:
            extracted = tar.extractfile(frame["rgb_key"])
            if extracted is None:
                raise DatasetValidationError(
                    f"Could not read observation: {frame['rgb_key']}"
                )
            shutil.copyfileobj(extracted, process.stdin)
        process.stdin.close()
        stderr = process.stderr.read().decode("utf-8", errors="replace")
        return_code = process.wait()
    except BaseException:
        process.kill()
        process.wait()
        output_path.unlink(missing_ok=True)
        raise

    if return_code != 0:
        output_path.unlink(missing_ok=True)
        raise DatasetValidationError(
            f"ffmpeg failed for {output_path.name}: {stderr.strip()}"
        )


def main() -> int:
    args = parse_args()
    dataset_dir = args.dataset.resolve()
    output_dir = (args.output or dataset_dir / "review").resolve()

    try:
        dataset, shard_path, checksum, records_by_episode = validate_dataset(dataset_dir)
        print(
            "Validation passed: "
            f"{dataset['completed_episode_count']} episode(s), "
            f"{dataset['transition_count']} transitions, "
            f"{dataset['observation_count']} observations."
        )
        if args.validate_only:
            return 0

        selected = set(args.episode or records_by_episode.keys())
        unknown = selected.difference(records_by_episode)
        if unknown:
            raise DatasetValidationError(
                f"Unknown episode ID(s): {', '.join(sorted(unknown))}"
            )
        ffmpeg = resolve_ffmpeg(args.ffmpeg)
        output_dir.mkdir(parents=True, exist_ok=True)
        rendered: list[dict[str, Any]] = []

        with tarfile.open(shard_path, mode="r:") as tar:
            for episode_id in sorted(selected):
                output_path = output_dir / f"{episode_id}.mp4"
                render_episode(
                    tar,
                    ffmpeg,
                    output_path,
                    records_by_episode[episode_id],
                    int(dataset["observation_rate_hz"]),
                )
                rendered.append(
                    {
                        "episode_id": episode_id,
                        "frame_count": len(records_by_episode[episode_id]),
                        "source_rgb_keys": [
                            frame["rgb_key"]
                            for frame in records_by_episode[episode_id]
                        ],
                        "video": output_path.name,
                    }
                )
                print(f"Created {output_path}")

        review_manifest = {
            "source_dataset": str(dataset_dir),
            "source_shard": shard_path.name,
            "source_shard_md5": checksum,
            "fps": int(dataset["observation_rate_hz"]),
            "videos": rendered,
        }
        (output_dir / "review_manifest.json").write_text(
            json.dumps(review_manifest, indent=2) + "\n",
            encoding="utf-8",
        )
        return 0
    except (DatasetValidationError, OSError, KeyError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
