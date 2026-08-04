#!/usr/bin/env python3
"""Run immutable Windows assignment manifests and publish immutable results."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tarfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import dataset_controller as controller


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def read_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_new_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as handle:
        json.dump(value, handle, indent=2, sort_keys=True)
        handle.write("\n")


def results_for(root: Path, assignment_id: str) -> list[dict[str, Any]]:
    paths = sorted((root / "results").glob(f"{assignment_id}--attempt-*.json"))
    return [read_json(path) for path in paths]


def next_attempt(root: Path, assignment_id: str) -> int:
    used: list[int] = []
    for directory in (root / "claims", root / "results"):
        if not directory.exists():
            continue
        for path in directory.glob(f"{assignment_id}--attempt-*.json"):
            try:
                used.append(int(path.stem.rsplit("-", 1)[1]))
            except (IndexError, ValueError):
                continue
    return max(used, default=-1) + 1


def claim_attempt(root: Path, assignment_id: str, attempt_number: int, executor_id: str) -> Path | None:
    claim = root / "claims" / f"{assignment_id}--attempt-{attempt_number:03d}.json"
    try:
        write_new_json(claim, {
            "assignment_id": assignment_id,
            "attempt_number": attempt_number,
            "executor_id": executor_id,
            "claimed_utc": utc_now(),
        })
    except FileExistsError:
        return None
    return claim


def read_episode_rows(dataset_dir: Path) -> list[dict[str, Any]]:
    import pyarrow.parquet as pq

    dataset = read_json(dataset_dir / "dataset.json")
    shard = dataset_dir / dataset["shards"][0]
    with tarfile.open(shard, "r") as archive:
        member = archive.getmember("episodes.parquet")
        extracted = archive.extractfile(member)
        if extracted is None:
            raise ValueError("episodes.parquet could not be read from finalized shard")
        return pq.read_table(extracted).to_pylist()


def build_validated_result(
    assignment: dict[str, Any],
    attempt_id: str,
    executor_id: str,
    output: Path,
) -> dict[str, Any]:
    episodes = read_episode_rows(output)
    rows_by_recipe = {row.get("recipe_id"): row for row in episodes}
    resolved: list[str] = []
    successful: list[str] = []
    semantic_failures: list[str] = []
    credited_cells: list[dict[str, Any]] = []
    accepted_frames = 0
    produced_frames = 0
    accepted_frames_by_mission: dict[str, int] = {}
    for recipe in assignment["recipes"]:
        recipe_id = recipe["recipe_id"]
        row = rows_by_recipe.get(recipe_id)
        if row is None:
            raise ValueError(f"validated output omitted recipe {recipe_id}")
        resolved.append(recipe_id)
        observation_count = int(row["observation_count"])
        produced_frames += observation_count
        mission = recipe["mission"]
        semantic_success = mission == "semi_markov" or bool(row["mission_success"])
        if semantic_success:
            successful.append(recipe_id)
            accepted_frames += observation_count
            accepted_frames_by_mission[mission] = accepted_frames_by_mission.get(mission, 0) + observation_count
            credited_cells.append({
                "mission": mission,
                "scenario_index": int(recipe["scenario_index"]),
                "recipe_id": recipe_id,
                "credited_observation_frames": observation_count,
            })
        else:
            semantic_failures.append(recipe_id)
    return {
        "schema_version": 1,
        "plan_id": assignment["plan_id"],
        "assignment_id": assignment["assignment_id"],
        "attempt_id": attempt_id,
        "executor_id": executor_id,
        "completed_utc": utc_now(),
        "technical_result": "validated",
        "semantic_result": "success" if not semantic_failures else "resolved_with_failures",
        "output_directory": str(output),
        "accepted_observation_frames": accepted_frames,
        "produced_observation_frames": produced_frames,
        "accepted_frames_by_mission": accepted_frames_by_mission,
        "resolved_recipe_ids": resolved,
        "successful_recipe_ids": successful,
        "semantic_failure_recipe_ids": semantic_failures,
        "credited_cells": credited_cells,
    }


def command_for(executable: Path, runtime_manifest: Path, output: Path) -> list[str]:
    return [
        str(executable),
        "-GenerateDataset",
        f"-RecipeManifest={runtime_manifest}",
        f"-Output={output}",
        "-RenderOffscreen",
        "-unattended",
        "-nosound",
        "-NoSplash",
        "-NoVSync",
    ]


def run_assignment(args: argparse.Namespace, assignment_path: Path) -> str:
    root = args.collection.resolve()
    assignment = read_json(assignment_path)
    assignment_id = assignment["assignment_id"]
    previous = results_for(root, assignment_id)
    if any(result.get("technical_result") == "validated" for result in previous):
        return "already_validated"
    attempt_number = next_attempt(root, assignment_id)
    if attempt_number >= args.max_attempts:
        return "attempt_limit"
    if claim_attempt(root, assignment_id, attempt_number, args.executor_id) is None:
        return "claimed_elsewhere"
    attempt_id = f"attempt-{attempt_number:03d}"
    attempt_dir = root / "attempts" / assignment_id / attempt_id
    output = attempt_dir / "output"
    runtime_manifest = {
        **assignment,
        "attempt_id": attempt_id,
        "executor_id": args.executor_id,
    }
    write_new_json(attempt_dir / "request.json", runtime_manifest)
    result_path = root / "results" / f"{assignment_id}--{attempt_id}.json"
    if args.simulate_interruption:
        write_new_json(result_path, {
            "schema_version": 1,
            "plan_id": assignment["plan_id"],
            "assignment_id": assignment_id,
            "attempt_id": attempt_id,
            "executor_id": args.executor_id,
            "completed_utc": utc_now(),
            "technical_result": "interrupted",
            "semantic_result": "not_evaluated",
            "resolved_recipe_ids": [],
            "accepted_observation_frames": 0,
            "error": "controlled interruption requested by worker test flag",
        })
        return "interrupted"

    output.mkdir(parents=True, exist_ok=False)
    command = command_for(args.executable.resolve(), attempt_dir / "request.json", output)
    started = utc_now()
    with (attempt_dir / "stdout.log").open("xb") as stdout_handle, (attempt_dir / "stderr.log").open("xb") as stderr_handle:
        completed = subprocess.run(command, stdout=stdout_handle, stderr=stderr_handle, check=False)
    if completed.returncode != 0:
        write_new_json(result_path, {
            "schema_version": 1,
            "plan_id": assignment["plan_id"],
            "assignment_id": assignment_id,
            "attempt_id": attempt_id,
            "executor_id": args.executor_id,
            "started_utc": started,
            "completed_utc": utc_now(),
            "technical_result": "generator_failed",
            "semantic_result": "not_evaluated",
            "return_code": completed.returncode,
            "resolved_recipe_ids": [],
            "accepted_observation_frames": 0,
        })
        return "generator_failed"

    project = Path(__file__).resolve().parent.parent
    finalize = subprocess.run([sys.executable, str(project / "Scripts" / "finalize_production_dataset.py"), str(output)], check=False)
    validate = subprocess.run([sys.executable, str(project / "Scripts" / "review_dataset.py"), str(output), "--validate-only"], check=False) if finalize.returncode == 0 else None
    if finalize.returncode != 0 or validate is None or validate.returncode != 0:
        write_new_json(result_path, {
            "schema_version": 1,
            "plan_id": assignment["plan_id"],
            "assignment_id": assignment_id,
            "attempt_id": attempt_id,
            "executor_id": args.executor_id,
            "started_utc": started,
            "completed_utc": utc_now(),
            "technical_result": "validation_failed",
            "semantic_result": "not_evaluated",
            "finalizer_return_code": finalize.returncode,
            "validator_return_code": None if validate is None else validate.returncode,
            "resolved_recipe_ids": [],
            "accepted_observation_frames": 0,
        })
        return "validation_failed"

    write_new_json(result_path, build_validated_result(assignment, attempt_id, args.executor_id, output))
    return "validated"


def prior_wave_complete(root: Path, assignment: dict[str, Any]) -> bool:
    """Do not let a fast worker run arbitrarily far beyond slower workers."""
    if "reserve_activation_for" in assignment:
        return True
    wave = int(assignment.get("dispatch_wave", 0))
    if wave == 0:
        return True
    validated = {
        result.get("assignment_id")
        for path in (root / "results").glob("*.json")
        if (result := read_json(path)).get("technical_result") == "validated"
    }
    for path in (root / "assignments").glob("*.json"):
        earlier = read_json(path)
        if "reserve_activation_for" in earlier:
            continue
        if int(earlier.get("dispatch_wave", 0)) < wave and earlier["assignment_id"] not in validated:
            return False
    return True


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("collection", type=Path)
    result.add_argument("--executable", type=Path, required=True)
    result.add_argument("--worker-id", type=int, required=True)
    result.add_argument("--executor-id", default=f"windows-{os.getpid()}")
    result.add_argument("--max-attempts", type=int, default=3)
    result.add_argument("--simulate-interruption", action="store_true")
    result.add_argument("--one", action="store_true", help="process at most one eligible assignment")
    return result


def main() -> int:
    args = parser().parse_args()
    root = args.collection.resolve()
    statuses: list[dict[str, str]] = []
    for assignment_path in sorted((root / "assignments").glob("*.json")):
        if (root / "STOP").exists():
            break
        assignment = read_json(assignment_path)
        if int(assignment["logical_worker_id"]) != args.worker_id:
            continue
        inventory = controller.build_inventory(root)
        if inventory["complete"]:
            statuses.append({"assignment_id": assignment["assignment_id"], "status": "budget_complete"})
            break
        if not prior_wave_complete(root, assignment):
            statuses.append({"assignment_id": assignment["assignment_id"], "status": "waiting_for_prior_wave"})
            break
        status = run_assignment(args, assignment_path)
        statuses.append({"assignment_id": assignment["assignment_id"], "status": status})
        if args.one or args.simulate_interruption or (root / "STOP").exists():
            break
    print(json.dumps({"worker_id": args.worker_id, "executor_id": args.executor_id, "assignments": statuses}, indent=2, sort_keys=True))
    return 0 if all(item["status"] in {"validated", "already_validated", "budget_complete", "waiting_for_prior_wave"} for item in statuses) else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
