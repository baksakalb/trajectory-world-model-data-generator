#!/usr/bin/env python3
"""Validate and render the planned 180-video V2 human-review set.

This tool never launches Unreal. Generate the immutable review assignments with
``v2_dataset_controller.py review-plan``, run them through ``dataset_worker.py``,
then use this script to validate or render their exact recorded frames.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


def read_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def result_output(collection: Path, assignment_id: str) -> Path:
    matches = []
    for path in sorted((collection / "results").glob(f"{assignment_id}--attempt-*.json")):
        value = read_json(path)
        if value.get("technical_result") == "validated":
            matches.append(Path(value["output_directory"]))
    if len(matches) != 1:
        raise ValueError(
            f"{assignment_id} must have exactly one validated immutable result; "
            f"found {len(matches)}"
        )
    return matches[0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("collection", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--ffmpeg", type=Path)
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()
    collection = args.collection.resolve()
    manifest = read_json(collection / "v2-review-plan.json")
    entries = manifest.get("entries", [])
    if (
        len(entries) != 180
        or len({entry["mission_type"] for entry in entries}) != 60
        or any(
            sum(item["mission_type"] == entry["mission_type"] for item in entries) != 3
            for entry in entries
        )
    ):
        raise ValueError("review manifest is not exactly three examples for all 60 types")
    output = (args.output or collection / "videos").resolve()
    if not args.validate_only:
        output.mkdir(parents=True, exist_ok=True)
    review_script = Path(__file__).with_name("review_dataset.py")
    rendered: list[str] = []
    for entry in entries:
        dataset = result_output(collection, entry["assignment_id"])
        command = [
            sys.executable, str(review_script), str(dataset),
            "--episode", entry["episode_id"],
        ]
        if args.validate_only:
            command.append("--validate-only")
        else:
            command.extend(["--output", str(output)])
            if args.ffmpeg:
                command.extend(["--ffmpeg", str(args.ffmpeg.resolve())])
        completed = subprocess.run(command, check=False)
        if completed.returncode != 0:
            return completed.returncode
        if not args.validate_only:
            source = output / f"{entry['episode_id']}.mp4"
            destination = output / entry["output_video"]
            if destination.exists():
                raise ValueError(f"refusing to overwrite review video: {destination}")
            source.replace(destination)
            rendered.append(destination.name)
    summary = {
        "validated_video_count": 180,
        "rendered_video_count": 0 if args.validate_only else len(rendered),
        "mission_type_count": 60,
        "examples_per_type": 3,
        "width": 384,
        "height": 384,
    }
    if not args.validate_only:
        (output / "review-set.json").write_text(
            json.dumps({**summary, "videos": rendered}, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
