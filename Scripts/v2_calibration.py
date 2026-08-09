#!/usr/bin/env python3
"""Derive conservative V2 credited-duration caps from local validated episodes."""

from __future__ import annotations

import argparse
import hashlib
import json
import tarfile
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable

import pyarrow.parquet as pq

from v2_catalog import SEQUENCE_TEMPLATES, base_cells
from v2_dataset_controller import FAMILY_FRAME_SHARES, canonical_json


def read_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def accepted_records(collections: Iterable[Path]) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    seen_outputs: set[Path] = set()
    for collection in collections:
        for result_path in sorted((collection.resolve() / "results").glob("*.json")):
            result = read_json(result_path)
            if result.get("technical_result") != "validated":
                continue
            output = Path(str(result["output_directory"])).resolve()
            if output in seen_outputs:
                continue
            seen_outputs.add(output)
            dataset = read_json(output / "dataset.json")
            with tarfile.open(output / str(dataset["shards"][0]), "r:") as archive:
                extracted = archive.extractfile("episodes.parquet")
                if extracted is None:
                    raise ValueError(f"{output} omits episodes.parquet")
                for episode in pq.read_table(extracted).to_pylist():
                    if episode.get("accepted_for_balancing"):
                        records.append(episode)
    return records


def derive_calibration(records: Iterable[dict[str, Any]]) -> dict[str, Any]:
    values = list(records)
    durations: dict[str, list[int]] = defaultdict(list)
    sequence_durations: dict[int, list[int]] = defaultdict(list)
    credited_cells: set[str] = set()
    credited_sequences: set[str] = set()
    for episode in values:
        family = str(episode.get("v2_source") == "random_play" and "random_play"
                     or (episode.get("v2_throws") or [{}])[0].get("intended_family")
                     or episode.get("v2_cell_id", "").split("-", 1)[0])
        template_id = episode.get("v2_sequence_template_id")
        observation_count = int(episode["observation_count"])
        if template_id:
            count = int(episode.get("v2_expected_throw_count") or len(episode.get("v2_throws") or []))
            sequence_durations[count].append(observation_count)
            credited_sequences.add(str(template_id))
        else:
            durations[family].append(observation_count)
            credited_cells.add(str(episode.get("v2_cell_id") or ""))

    expected_cells = {cell["cell_id"] for cell in base_cells()}
    expected_sequences = {template.template_id for template in SEQUENCE_TEMPLATES}
    complete = credited_cells >= expected_cells and credited_sequences >= expected_sequences
    missing_families = sorted(set(FAMILY_FRAME_SHARES) - set(durations))
    if missing_families:
        raise ValueError(f"calibration has no accepted samples for families: {missing_families}")
    missing_counts = sorted(set((2, 3, 4, 5)) - set(sequence_durations))
    if missing_counts:
        raise ValueError(f"calibration has no accepted samples for sequence counts: {missing_counts}")
    family_caps = {family: min(samples) for family, samples in durations.items()}
    sequence_caps = {str(count): min(sequence_durations[count]) for count in (2, 3, 4, 5)}
    identity = {
        "family_caps": family_caps, "sequence_caps": sequence_caps,
        "credited_cells": sorted(credited_cells & expected_cells),
        "credited_sequences": sorted(credited_sequences & expected_sequences),
    }
    return {
        "schema_version": 2,
        "calibration_version": "v2-local-qualified-" + hashlib.sha256(
            canonical_json(identity).encode()).hexdigest()[:16],
        "qualified": complete,
        "observation_rate_hz": 20,
        "expected_credited_frames_by_family": {
            family: family_caps[family] for family in FAMILY_FRAME_SHARES
        },
        "expected_credited_frames_by_sequence_grenade_count": sequence_caps,
        "qualification_evidence": {
            "accepted_episode_count": len(values),
            "accepted_base_cell_count": len(credited_cells & expected_cells),
            "required_base_cell_count": 1184,
            "accepted_sequence_template_count": len(credited_sequences & expected_sequences),
            "required_sequence_template_count": len(expected_sequences),
            "sample_count_by_family": {key: len(value) for key, value in sorted(durations.items())},
            "sample_count_by_sequence_grenade_count": {
                str(key): len(value) for key, value in sorted(sequence_durations.items())
            },
        },
        "notes": "Conservative minimum accepted observation counts; qualified only after complete base-cell and sequence-template evidence.",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("collection", type=Path, nargs="+")
    parser.add_argument("--allow-incomplete", action="store_true")
    args = parser.parse_args()
    calibration = derive_calibration(accepted_records(args.collection))
    if not calibration["qualified"] and not args.allow_incomplete:
        print(json.dumps(calibration["qualification_evidence"], indent=2, sort_keys=True))
        return 2
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8", newline="\n") as handle:
        json.dump(calibration, handle, indent=2, sort_keys=True)
        handle.write("\n")
    print(json.dumps(calibration, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
