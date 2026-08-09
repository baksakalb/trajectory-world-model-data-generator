#!/usr/bin/env python3
"""Reconstruct planned/realized V2 distributions from immutable local results."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import tarfile
from collections import Counter
from pathlib import Path
from typing import Any, Iterable

import pyarrow.parquet as pq
from PIL import Image


def read_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    with path.open("r", encoding="utf-8") as handle:
        return [json.loads(line) for line in handle if line.strip()]


def table_rows(archive: tarfile.TarFile, name: str) -> list[dict[str, Any]]:
    extracted = archive.extractfile(name)
    if extracted is None:
        raise ValueError(f"validated shard omits {name}")
    return pq.read_table(extracted).to_pylist()


def perceptual_hash(payload: bytes) -> str:
    image = Image.open(io.BytesIO(payload)).convert("L").resize((9, 8), Image.Resampling.BILINEAR)
    pixels = list(image.get_flattened_data())
    bits = 0
    for y in range(8):
        for x in range(8):
            bits = (bits << 1) | int(pixels[y * 9 + x] > pixels[y * 9 + x + 1])
    return f"{bits:016x}"


def bounce_band(count: int) -> str:
    if count == 0:
        return "zero"
    if count == 1:
        return "one"
    if count <= 3:
        return "two_to_three"
    return "four_plus"


def counter_json(counter: Counter[str]) -> dict[str, int]:
    return {key: counter[key] for key in sorted(counter)}


def build_report(
    collections: Iterable[Path],
    include_image_duplicates: bool = False,
) -> dict[str, Any]:
    credited_source: Counter[str] = Counter()
    credited_family: Counter[str] = Counter()
    credited_cells: Counter[str] = Counter()
    credited_sequences: Counter[str] = Counter()
    factors: Counter[str] = Counter()
    temporal: dict[str, Counter[str]] = {
        name: Counter() for name in (
            "aim_acquisition_profile", "q_retention_profile",
            "post_throw_movement_profile", "post_throw_camera_profile",
        )
    }
    observations: Counter[str] = Counter()
    transitions: Counter[str] = Counter()
    contacts: Counter[str] = Counter()
    intended_outcomes: Counter[str] = Counter()
    realized_targets: Counter[str] = Counter()
    bounce_bands: Counter[str] = Counter()
    event_durations: dict[str, list[int]] = {
        "q_to_e": [], "e_to_contact": [], "e_to_rest": [], "rest_to_tail_end": [],
    }
    split_replays: dict[str, set[str]] = {"train": set(), "evaluation": set()}
    exact_hashes: Counter[str] = Counter()
    perceptual_hashes: Counter[str] = Counter()
    shard_bytes = 0
    validated_assignments = 0
    technical_failures = 0
    semantic_failure_recipes: set[str] = set()
    seen_outputs: set[Path] = set()

    for collection in collections:
        root = collection.resolve()
        recipes = {
            str(item["recipe_id"]): item
            for item in read_jsonl(root / "plan" / "recipes.jsonl")
        }
        for result_path in sorted((root / "results").glob("*.json")):
            result = read_json(result_path)
            if result.get("technical_result") != "validated":
                technical_failures += 1
                continue
            output = Path(str(result["output_directory"])).resolve()
            if output in seen_outputs:
                continue
            seen_outputs.add(output)
            validated_assignments += 1
            semantic_failure_recipes.update(result.get("semantic_failure_recipe_ids", []))
            credits = {
                str(item["recipe_id"]): int(item["credited_observation_frames"])
                for item in result.get("credited_cells", [])
            }
            dataset = read_json(output / "dataset.json")
            shard = output / str(dataset["shards"][0])
            shard_bytes += shard.stat().st_size
            with tarfile.open(shard, "r:") as archive:
                episode_rows = table_rows(archive, "episodes.parquet")
                observation_rows = table_rows(archive, "frames.parquet")
                transition_rows = table_rows(archive, "transitions.parquet")
                episode_credit: dict[str, int] = {}
                for episode in episode_rows:
                    recipe_id = str(episode.get("recipe_id") or "")
                    credit = credits.get(recipe_id, 0)
                    if credit <= 0:
                        continue
                    recipe = recipes[recipe_id]
                    episode_id = str(episode["episode_id"])
                    episode_credit[episode_id] = credit
                    family = str(recipe["family"])
                    source = str(recipe["source"])
                    credited_source[source] += credit
                    credited_family[family] += credit
                    credited_cells[str(recipe["cell_id"])] += credit
                    if recipe.get("sequence_template_id"):
                        credited_sequences[str(recipe["sequence_template_id"])] += credit
                    split = str(recipe.get("split") or "train")
                    split_replays.setdefault(split, set()).add(str(recipe["replay_identity"]))
                    for field, counter in temporal.items():
                        counter[str(recipe.get(field) or "none")] += credit
                    for key, value in sorted((recipe.get("cell") or {}).items()):
                        if isinstance(value, (str, int, float, bool)):
                            factors[f"{key}={value}"] += credit
                    for throw in episode.get("v2_throws") or []:
                        intended_outcomes[str(throw.get("intended_outcome") or "none")] += 1
                        realized_targets[str(throw.get("realized_target") or "none")] += 1
                        bounce_bands[bounce_band(int(throw.get("bounce_count") or 0))] += 1
                        for contact in throw.get("realized_contact_order") or []:
                            contacts[str(contact)] += 1
                        q_frame = int(throw["q_rising_frame"])
                        e_frame = int(throw["e_edge_frame"])
                        event_durations["q_to_e"].append(e_frame - q_frame)
                        if throw.get("first_contact_frame") is not None:
                            event_durations["e_to_contact"].append(int(throw["first_contact_frame"]) - e_frame)
                        if throw.get("rest_frame") is not None:
                            rest = int(throw["rest_frame"])
                            event_durations["e_to_rest"].append(rest - e_frame)
                            event_durations["rest_to_tail_end"].append(
                                int(episode["observation_count"]) - 1 - rest
                            )

                credited_image_keys: list[str] = []
                for row in observation_rows:
                    episode_id = str(row["episode_id"])
                    credit = episode_credit.get(episode_id, 0)
                    if int(row["frame_index"]) >= credit:
                        continue
                    observations["total"] += 1
                    observations["q_visible" if row.get("q_visibility") else "q_hidden"] += 1
                    observations["cooldown" if int(row.get("cooldown_remaining_steps") or 0) > 0 else "ready"] += 1
                    total = int(row.get("total_grenade_count") or 0)
                    grenade_band = "zero" if total == 0 else ("one" if total == 1 else ("two_to_four" if total <= 4 else "five_plus"))
                    observations[f"grenades_{grenade_band}"] += 1
                    if int(row.get("flying_grenade_count") or 0) and int(row.get("resting_grenade_count") or 0):
                        observations["airborne_and_resting"] += 1
                    observations[f"phase_{row.get('v2_episode_phase') or 'none'}"] += 1
                    if include_image_duplicates:
                        credited_image_keys.append(str(row["rgb_key"]))

                for row in transition_rows:
                    episode_id = str(row["episode_id"])
                    if int(row["source_frame_index"]) >= max(0, episode_credit.get(episode_id, 0) - 1):
                        continue
                    transitions["total"] += 1
                    for field in ("q_rising_edge", "q_falling_edge", "e_request_edge", "e_accepted", "planar_movement_suppressed"):
                        if row.get(field):
                            transitions[field] += 1
                    reason = str(row.get("e_rejection_reason") or "none")
                    if reason != "none":
                        transitions[f"e_rejected_{reason}"] += 1
                    transitions[f"action_mask_{int(row['action_mask'])}"] += 1

                if include_image_duplicates:
                    for key in credited_image_keys:
                        extracted = archive.extractfile(key)
                        if extracted is None:
                            raise ValueError(f"validated shard omits {key}")
                        payload = extracted.read()
                        rgb = Image.open(io.BytesIO(payload)).convert("RGB")
                        exact_hashes[hashlib.sha256(rgb.tobytes()).hexdigest()] += 1
                        perceptual_hashes[perceptual_hash(payload)] += 1

    total_images = sum(exact_hashes.values())
    duplicate_report = {
        "enabled": include_image_duplicates,
        "image_count": total_images,
        "exact_duplicate_count": total_images - len(exact_hashes) if include_image_duplicates else None,
        "perceptual_duplicate_count": total_images - len(perceptual_hashes) if include_image_duplicates else None,
    }
    train_eval_overlap = split_replays.get("train", set()) & split_replays.get("evaluation", set())
    return {
        "schema_version": 1,
        "report_contract": "v2-realized-distribution-1",
        "validated_assignment_count": validated_assignments,
        "technical_failure_attempt_count": technical_failures,
        "semantic_failure_recipe_ids": sorted(semantic_failure_recipes),
        "credited_frames": {
            "total": sum(credited_family.values()),
            "by_source": counter_json(credited_source),
            "by_family": counter_json(credited_family),
            "by_cell": counter_json(credited_cells),
            "by_sequence_template": counter_json(credited_sequences),
            "by_factor": counter_json(factors),
            "by_temporal_profile": {key: counter_json(value) for key, value in temporal.items()},
        },
        "observations": counter_json(observations),
        "transitions": counter_json(transitions),
        "throws": {
            "contacts": counter_json(contacts),
            "intended_outcomes": counter_json(intended_outcomes),
            "realized_targets": counter_json(realized_targets),
            "bounce_bands": counter_json(bounce_bands),
            "event_duration_steps": {
                key: {"count": len(values), "minimum": min(values) if values else None,
                      "maximum": max(values) if values else None,
                      "mean": (sum(values) / len(values)) if values else None}
                for key, values in event_durations.items()
            },
        },
        "train_evaluation": {
            "train_replay_count": len(split_replays.get("train", set())),
            "evaluation_replay_count": len(split_replays.get("evaluation", set())),
            "overlap_count": len(train_eval_overlap),
        },
        "storage": {"validated_shard_bytes": shard_bytes},
        "duplicates": duplicate_report,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("collection", type=Path, nargs="+")
    parser.add_argument("--image-duplicates", action="store_true")
    args = parser.parse_args()
    report = build_report(args.collection, args.image_duplicates)
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, indent=2, sort_keys=True)
        handle.write("\n")
    print(json.dumps({
        "credited_frames": report["credited_frames"]["total"],
        "semantic_failures": len(report["semantic_failure_recipe_ids"]),
        "train_evaluation_overlap": report["train_evaluation"]["overlap_count"],
        "output": str(output),
    }, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
