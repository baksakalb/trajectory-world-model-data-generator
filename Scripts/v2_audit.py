#!/usr/bin/env python3
"""Select and export the frozen 128-slot V2 visual audit from local results."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import tarfile
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable

import pyarrow.parquet as pq
from PIL import Image, ImageDraw, ImageFont

import review_dataset
from v2_catalog import audit_slots


def read_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def result_candidates(collections: Iterable[Path]) -> list[dict[str, Any]]:
    """Reconstruct accepted episode candidates from immutable validated results."""
    candidates: list[dict[str, Any]] = []
    seen_outputs: set[Path] = set()
    for collection in collections:
        collection = collection.resolve()
        recipes: dict[str, dict[str, Any]] = {}
        recipes_path = collection / "plan" / "recipes.jsonl"
        if recipes_path.exists():
            with recipes_path.open("r", encoding="utf-8") as handle:
                for line in handle:
                    if line.strip():
                        recipe = json.loads(line)
                        recipes[str(recipe["recipe_id"])] = recipe
        for result_path in sorted((collection / "results").glob("*.json")):
            result = read_json(result_path)
            if result.get("technical_result") != "validated":
                continue
            output = Path(str(result["output_directory"])).resolve()
            if output in seen_outputs:
                continue
            seen_outputs.add(output)
            dataset = read_json(output / "dataset.json")
            shard = output / str(dataset["shards"][0])
            with tarfile.open(shard, "r:") as archive:
                extracted = archive.extractfile("episodes.parquet")
                if extracted is None:
                    raise ValueError(f"{shard} omits episodes.parquet")
                episodes = pq.read_table(extracted).to_pylist()
            for episode in episodes:
                if not bool(episode.get("accepted_for_balancing")):
                    continue
                recipe = recipes.get(str(episode.get("recipe_id") or ""), {})
                candidates.append({
                    "output_directory": str(output),
                    "shard": shard.name,
                    "episode_id": str(episode["episode_id"]),
                    "cell_id": str(episode.get("v2_cell_id") or ""),
                    "recipe_id": episode.get("recipe_id"),
                    "replay_identity": episode.get("v2_replay_identity"),
                    "seed": int(episode["seed"]),
                    "split": episode.get("split"),
                    "observation_count": int(episode["observation_count"]),
                    "action_count": int(episode["actual_transitions"]),
                    "sequence_template_id": episode.get("v2_sequence_template_id"),
                    "throws": episode.get("v2_throws") or [],
                    "catalog_cell": recipe.get("cell"),
                    "continuous_sample_ordinal": recipe.get("continuous_sample_ordinal"),
                    "continuous_strata": recipe.get("continuous_strata"),
                    "temporal_profiles": {
                        "aim_acquisition": episode.get("v2_aim_acquisition_profile"),
                        "q_retention": episode.get("v2_q_retention_profile"),
                        "post_throw_movement": episode.get("v2_post_throw_movement_profile"),
                        "post_throw_camera": episode.get("v2_post_throw_camera_profile"),
                    },
                    "refinement_level": episode.get("refinement_level"),
                    "repetition_index": episode.get("repetition_index"),
                    "plan_id": episode.get("plan_id"),
                    "plan_version": episode.get("plan_version"),
                    "semantic_success": True,
                })
    return candidates


def select_examples(
    candidates: Iterable[dict[str, Any]],
    slots: Iterable[dict[str, Any]] | None = None,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    by_cell: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for candidate in candidates:
        if candidate.get("semantic_success"):
            by_cell[str(candidate["cell_id"])].append(candidate)
    for values in by_cell.values():
        values.sort(key=lambda item: (
            str(item.get("replay_identity") or ""), int(item["seed"]),
            str(item["episode_id"]), str(item["output_directory"]),
        ))
    selected: list[dict[str, Any]] = []
    missing: list[dict[str, Any]] = []
    for slot in slots or audit_slots():
        matches = by_cell.get(str(slot["cell_id"]), [])
        if not matches:
            missing.append(dict(slot))
            continue
        selected.append({**slot, **matches[0]})
    return selected, missing


def resolve_ffmpeg(explicit: Path | None) -> Path:
    if explicit:
        return review_dataset.resolve_ffmpeg(explicit)
    try:
        return review_dataset.resolve_ffmpeg(None)
    except review_dataset.DatasetValidationError:
        import imageio_ffmpeg
        return Path(imageio_ffmpeg.get_ffmpeg_exe()).resolve()


def event_indices(
    example: dict[str, Any],
    transitions: list[dict[str, Any]],
    frames: list[dict[str, Any]],
) -> dict[str, list[int]]:
    throws = list(example.get("throws") or [])
    return {
        "q_start": [int(item["source_frame_index"]) for item in transitions if item.get("q_rising_edge")],
        "q_release": [int(item["source_frame_index"]) for item in transitions if item.get("q_falling_edge")],
        "e_request": [int(item["source_frame_index"]) for item in transitions if item.get("e_request_edge")],
        "e_accept": [int(item["source_frame_index"]) for item in transitions if item.get("e_accepted")],
        "first_contact": [int(item["first_contact_frame"]) for item in throws if item.get("first_contact_frame") is not None],
        "rest": [int(item["rest_frame"]) for item in throws if item.get("rest_frame") is not None],
        "tail_end": [int(frames[-1]["frame_index"])] if frames else [],
    }


def render_selected(selected: list[dict[str, Any]], output: Path, ffmpeg: Path) -> None:
    cache: dict[str, tuple[
        dict[str, Any], Path, dict[str, list[dict[str, Any]]],
        dict[str, list[dict[str, Any]]],
    ]] = {}
    thumbnails: dict[str, list[tuple[str, Image.Image]]] = defaultdict(list)
    for example in selected:
        dataset_dir = Path(str(example["output_directory"]))
        key = str(dataset_dir)
        if key not in cache:
            dataset, shard, _, records = review_dataset.validate_dataset(dataset_dir)
            with tarfile.open(shard, "r:") as archive:
                extracted = archive.extractfile("transitions.parquet")
                if extracted is None:
                    raise ValueError(f"{shard} omits transitions.parquet")
                transition_rows = pq.read_table(extracted).to_pylist()
            transitions: dict[str, list[dict[str, Any]]] = defaultdict(list)
            for transition in transition_rows:
                transitions[str(transition["episode_id"])].append(transition)
            cache[key] = (dataset, shard, records, transitions)
        dataset, shard_path, records, transitions = cache[key]
        episode_id = str(example["episode_id"])
        frames = records[episode_id]
        example["event_indices"] = event_indices(
            example, transitions.get(episode_id, []), frames,
        )
        family_dir = output / str(example["family"])
        family_dir.mkdir(parents=True, exist_ok=True)
        video = family_dir / f"{example['slot_id']}.mp4"
        with tarfile.open(shard_path, "r:") as archive:
            review_dataset.render_episode(
                archive, ffmpeg, video, frames,
                int(dataset["observation_rate_hz"]),
                "webp" if dataset.get("rgb_format") == "lossless_webp" else "png",
            )
            preferred = (
                example["event_indices"]["first_contact"]
                or example["event_indices"]["e_accept"]
                or [int(frames[len(frames) // 2]["frame_index"])]
            )[0]
            thumbnail_frame = min(
                frames, key=lambda frame: abs(int(frame["frame_index"]) - preferred),
            )
            thumbnail_key = thumbnail_frame["rgb_key"]
            extracted = archive.extractfile(thumbnail_key)
            if extracted is None:
                raise ValueError(f"could not extract thumbnail {thumbnail_key}")
            thumbnails[str(example["family"])].append((
                str(example["slot_id"]), Image.open(io.BytesIO(extracted.read())).convert("RGB")
            ))
        example["video"] = str(video.relative_to(output)).replace("\\", "/")
        example["source_image_keys"] = [frame["rgb_key"] for frame in frames]

    font = ImageFont.load_default()
    for family, items in thumbnails.items():
        thumb_width, thumb_height = 192, 192
        columns = 4
        rows = (len(items) + columns - 1) // columns
        sheet = Image.new("RGB", (columns * thumb_width, rows * (thumb_height + 20)), "#181818")
        draw = ImageDraw.Draw(sheet)
        for index, (label, source) in enumerate(items):
            x = (index % columns) * thumb_width
            y = (index // columns) * (thumb_height + 20)
            image = source.copy()
            image.thumbnail((thumb_width, thumb_height), Image.Resampling.LANCZOS)
            sheet.paste(image, (x + (thumb_width - image.width) // 2, y))
            draw.text((x + 4, y + thumb_height + 3), label, fill="white", font=font)
        sheet.save(output / f"contact-sheet-{family}.jpg", quality=90)


def coverage(selected: list[dict[str, Any]], missing: list[dict[str, Any]]) -> dict[str, Any]:
    counts: dict[str, int] = defaultdict(int)
    for item in selected:
        counts[str(item["family"])] += 1
    return {
        "required_slot_count": 128,
        "selected_slot_count": len(selected),
        "missing_slot_count": len(missing),
        "selected_by_family": dict(sorted(counts.items())),
        "complete": not missing and len(selected) == 128,
        "missing_slots": missing,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("collection", type=Path, nargs="+")
    parser.add_argument("--ffmpeg", type=Path)
    parser.add_argument("--allow-incomplete", action="store_true")
    parser.add_argument("--select-only", action="store_true")
    args = parser.parse_args()
    selected, missing = select_examples(result_candidates(args.collection))
    if missing and not args.allow_incomplete:
        print(json.dumps(coverage(selected, missing), indent=2, sort_keys=True))
        return 2
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if not args.select_only:
        render_selected(selected, output, resolve_ffmpeg(args.ffmpeg))
    report = coverage(selected, missing)
    manifest = {
        "schema_version": 1,
        "selection_contract": "v2-visual-audit-128-1",
        "selection_sha256": hashlib.sha256(json.dumps(
            [(item["slot_id"], item["replay_identity"]) for item in selected],
            separators=(",", ":"), sort_keys=True,
        ).encode()).hexdigest(),
        "coverage": report,
        "examples": selected,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (output / "coverage-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    lines = ["# V2 visual audit", "", f"Selected {len(selected)} of 128 frozen slots.", ""]
    for item in selected:
        lines.append(f"- `{item['slot_id']}` - {item['title']} - `{item['cell_id']}` - `{item.get('video', 'selection only')}`")
    if missing:
        lines.extend(["", "## Missing slots", ""])
        lines.extend(f"- `{item['slot_id']}` - `{item['cell_id']}`" for item in missing)
    (output / "README.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["complete"] or args.allow_incomplete else 2


if __name__ == "__main__":
    raise SystemExit(main())
