#!/usr/bin/env python3
"""Immutable central planner and inventory for Movement V1 dataset generation."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


PLAN_VERSION = "movement-v1-prescribed-1"
CATALOG_VERSION = "fixed-arena-r4-realized-facing"
MISSION_COUNTS = {
    "semi_markov": 32,  # 8 initial behavior families x 4 hold-duration bands.
    "object_view": 120,
    "contact_recovery": 675,
    "ramp_traverse": 30,
    "hoop_pass": 30,
}
GUIDED_MISSIONS = set(MISSION_COUNTS) - {"semi_markov"}


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def canonical_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def stable_id(prefix: str, value: Any, length: int = 16) -> str:
    digest = hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()
    return f"{prefix}-{digest[:length]}"


def write_new_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as handle:
        json.dump(value, handle, indent=2, sort_keys=True)
        handle.write("\n")


def write_new_jsonl(path: Path, values: Iterable[Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as handle:
        for value in values:
            handle.write(canonical_json(value))
            handle.write("\n")


def read_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def read_jsonl(path: Path) -> list[Any]:
    with path.open("r", encoding="utf-8") as handle:
        return [json.loads(line) for line in handle if line.strip()]


def spread_indices(count: int) -> list[int]:
    """Return a deterministic farthest-gap ordering over [0, count)."""
    if count <= 0:
        return []
    selected: list[int] = []
    remaining = set(range(count))
    while remaining:
        if not selected:
            candidate = 0
        else:
            candidate = max(
                remaining,
                key=lambda value: (
                    min(abs(value - chosen) for chosen in selected),
                    -value,
                ),
            )
        selected.append(candidate)
        remaining.remove(candidate)
    return selected


def mandatory_cells() -> list[tuple[str, int]]:
    """Interleave mission families while spreading within every finite catalog."""
    orders = {mission: spread_indices(count) for mission, count in MISSION_COUNTS.items()}
    cells: list[tuple[str, int]] = []
    for ordinal in range(max(MISSION_COUNTS.values())):
        for mission in MISSION_COUNTS:
            if ordinal < len(orders[mission]):
                cells.append((mission, orders[mission][ordinal]))
    return cells


def cell_details(mission: str, scenario: int) -> dict[str, Any]:
    if mission == "semi_markov":
        action_names = [
            "idle", "forward", "strafe", "camera_yaw", "camera_pitch",
            "movement_camera", "opposing_inputs", "deliberate_contact",
        ]
        hold_names = ["short", "medium", "long", "very_long"]
        return {
            "initial_behavior_family": action_names[scenario // 4],
            "initial_hold_band": hold_names[scenario % 4],
        }
    if mission == "object_view":
        gaze = ["target_center", "target_offset", "travel_direction", "roam_reacquire"]
        targets = ["rectangle", "pyramid", "sphere", "hoop", "ramp"]
        if scenario < 20:
            return {"target": targets[scenario // 4], "mode": "approach_observe", "gaze": gaze[scenario % 4], "direction": None}
        if scenario < 40:
            local = scenario - 20
            return {"target": targets[local // 4], "mode": "pass_by", "gaze": gaze[local % 4], "direction": None}
        full = scenario >= 80
        local = scenario - (80 if full else 40)
        return {
            "target": targets[local // 8],
            "mode": "full_orbit" if full else "partial_orbit",
            "gaze": gaze[(local % 8) // 2],
            "direction": "clockwise" if local % 2 == 0 else "counter_clockwise",
        }
    if mission == "contact_recovery":
        targets = ["rectangle", "pyramid", "sphere", "hoop", "ramp", "north_wall", "south_wall", "east_wall", "west_wall"]
        recoveries = ["backward", "strafe_left", "strafe_right", "diagonal_left", "diagonal_right"]
        approaches = ["direct", "glance_left", "glance_right"]
        facings = ["forward", "backward", "strafe_left", "strafe_right", "free_attention"]
        base, facing = divmod(scenario, 5)
        target, local = divmod(base, 15)
        recovery, approach = divmod(local, 3)
        return {"target": targets[target], "recovery": recoveries[recovery], "approach": approaches[approach], "facing": facings[facing]}
    direction, local = divmod(scenario, 15)
    path, facing = divmod(local, 5)
    facings = ["forward", "backward", "strafe_left", "strafe_right", "free_attention"]
    if mission == "ramp_traverse":
        return {"direction": ["uphill", "downhill"][direction], "path": ["center", "diagonal_left_to_right", "diagonal_right_to_left"][path], "facing": facings[facing]}
    return {"direction": ["positive_x_to_negative_x", "negative_x_to_positive_x"][direction], "path": ["center", "oblique_left_to_right", "oblique_right_to_left"][path], "facing": facings[facing]}


def build_recipe(plan_id: str, recipe_index: int, mission: str, scenario: int, repetition: int, reserve_for: str | None = None) -> dict[str, Any]:
    identity = {
        "plan_id": plan_id,
        "recipe_index": recipe_index,
        "mission": mission,
        "scenario_index": scenario,
        "repetition_index": repetition,
        "reserve_for": reserve_for,
    }
    return {
        "recipe_id": stable_id("recipe", identity),
        "recipe_index": recipe_index,
        "episode_index": recipe_index,
        "mission": mission,
        "scenario_index": scenario,
        "cell": cell_details(mission, scenario),
        "continuous_sample_ordinal": repetition,
        "refinement_level": 0 if repetition == 0 else int(math.floor(math.log2(repetition))) + 1,
        "repetition_index": repetition,
        "reserve_for": reserve_for,
        "active": reserve_for is None,
    }


def create_plan(args: argparse.Namespace) -> None:
    root = args.collection.resolve()
    if root.exists() and any(root.iterdir()):
        raise ValueError(f"collection directory is not empty: {root}")
    mandatory = mandatory_cells()
    nominal_observations = args.episode_seconds * args.observation_rate + 1
    minimum_guided_observations = math.ceil(0.75 * args.observation_rate) + 2
    minimum_frames = (
        MISSION_COUNTS["semi_markov"] * nominal_observations
        + sum(MISSION_COUNTS[mission] for mission in GUIDED_MISSIONS)
        * minimum_guided_observations
    )
    if args.frame_budget < minimum_frames:
        raise ValueError(
            f"frame budget {args.frame_budget} is infeasible: complete discrete coverage "
            f"requires at least {minimum_frames} accepted observations"
        )
    active_count = max(len(mandatory), math.ceil(args.frame_budget / nominal_observations))
    plan_identity = {
        "version": PLAN_VERSION,
        "catalog": CATALOG_VERSION,
        "frame_budget": args.frame_budget,
        "split": args.split,
        "stage": "movement_v1",
        "episode_seconds": args.episode_seconds,
        "observation_rate_hz": args.observation_rate,
        "width": args.width,
        "height": args.height,
        "storage_format": args.storage_format,
        "active_recipe_count": active_count,
    }
    plan_id = args.plan_id or stable_id("plan", plan_identity)

    active: list[dict[str, Any]] = []
    for recipe_index in range(active_count):
        mission, scenario = mandatory[recipe_index % len(mandatory)]
        repetition = recipe_index // len(mandatory)
        active.append(build_recipe(plan_id, recipe_index, mission, scenario, repetition))

    reserves: list[dict[str, Any]] = []
    next_index = len(active)
    for primary in active[: len(mandatory)]:
        if primary["mission"] not in GUIDED_MISSIONS:
            continue
        reserves.append(build_recipe(
            plan_id,
            next_index,
            primary["mission"],
            primary["scenario_index"],
            primary["repetition_index"] + 1,
            primary["recipe_id"],
        ))
        next_index += 1

    assignments: list[dict[str, Any]] = []
    for block_index in range(0, len(active), args.recipes_per_assignment):
        block = active[block_index : block_index + args.recipes_per_assignment]
        assignment_number = len(assignments)
        logical_worker_id = assignment_number % args.workers
        assignment_id = f"assignment-{assignment_number:06d}"
        assignments.append({
            "schema_version": 1,
            "plan_id": plan_id,
            "plan_version": PLAN_VERSION,
            "assignment_id": assignment_id,
            "logical_worker_id": logical_worker_id,
            "split": args.split,
            "generator": {
                "stage": "movement_v1",
                "episode_seconds": args.episode_seconds,
                "observation_rate_hz": args.observation_rate,
                "rgb_width": args.width,
                "rgb_height": args.height,
                "storage_format": args.storage_format,
                "webp_lossless_effort": args.webp_effort,
                "seed_start": args.seed_start,
            },
            "recipes": block,
        })

    plan = {
        "schema_version": 1,
        "plan_version": PLAN_VERSION,
        "plan_id": plan_id,
        "created_utc": utc_now(),
        "catalog_version": CATALOG_VERSION,
        "catalog_counts": MISSION_COUNTS,
        "mandatory_recipe_count": len(mandatory),
        "active_recipe_count": len(active),
        "reserve_recipe_count": len(reserves),
        "target_accepted_frames": args.frame_budget,
        "minimum_feasible_accepted_frames": minimum_frames,
        "budget_completion_policy": "mandatory coverage first; then stop after the first fully validated recipe at or above target",
        "worker_count": args.workers,
        "recipes_per_assignment": args.recipes_per_assignment,
        "assignment_count": len(assignments),
        "split": args.split,
        "generator": assignments[0]["generator"],
    }
    write_new_json(root / "plan" / "collection-plan.json", plan)
    write_new_jsonl(root / "plan" / "recipes.jsonl", [*active, *reserves])
    for assignment in assignments:
        write_new_json(root / "assignments" / f"{assignment['assignment_id']}.json", assignment)
    print(canonical_json({
        "plan_id": plan_id,
        "mandatory_recipes": len(mandatory),
        "active_recipes": len(active),
        "reserve_recipes": len(reserves),
        "assignments": len(assignments),
        "collection": str(root),
    }))


def result_files(root: Path) -> list[Path]:
    results = root / "results"
    return sorted(results.glob("*.json")) if results.exists() else []


def build_inventory(root: Path) -> dict[str, Any]:
    plan = read_json(root / "plan" / "collection-plan.json")
    recipes = read_jsonl(root / "plan" / "recipes.jsonl")
    active_ids = {recipe["recipe_id"] for recipe in recipes if recipe["active"]}
    successful_assignments: set[str] = set()
    technical_failures: list[dict[str, Any]] = []
    accepted_frames = 0
    accepted_recipes: set[str] = set()
    credited_cells: set[tuple[str, int]] = set()
    semantic_failures: set[str] = set()
    duplicate_results: list[str] = []
    result_attempts: set[tuple[str, str]] = set()
    for path in result_files(root):
        result = read_json(path)
        result_attempts.add((str(result.get("assignment_id")), str(result.get("attempt_id"))))
        if result.get("technical_result") != "validated":
            technical_failures.append(result)
            continue
        assignment_id = result["assignment_id"]
        if assignment_id in successful_assignments:
            duplicate_results.append(path.name)
            continue
        successful_assignments.add(assignment_id)
        accepted_frames += int(result.get("accepted_observation_frames", 0))
        accepted_recipes.update(result.get("resolved_recipe_ids", []))
        semantic_failures.update(result.get("semantic_failure_recipe_ids", []))
        for cell in result.get("credited_cells", []):
            credited_cells.add((cell["mission"], int(cell["scenario_index"])))
    expected_cells = {(mission, scenario) for mission, count in MISSION_COUNTS.items() for scenario in range(count)}
    missing_cells = sorted(expected_cells - credited_cells)
    assignments = sorted((root / "assignments").glob("*.json"))
    orphaned_claims: list[dict[str, Any]] = []
    claims_dir = root / "claims"
    if claims_dir.exists():
        for path in sorted(claims_dir.glob("*.json")):
            claim = read_json(path)
            attempt_id = f"attempt-{int(claim['attempt_number']):03d}"
            if (str(claim["assignment_id"]), attempt_id) not in result_attempts:
                orphaned_claims.append({
                    "assignment_id": claim["assignment_id"],
                    "attempt_id": attempt_id,
                    "executor_id": claim.get("executor_id"),
                    "claimed_utc": claim.get("claimed_utc"),
                })
    inventory = {
        "schema_version": 1,
        "plan_id": plan["plan_id"],
        "reconstructed_utc": utc_now(),
        "target_accepted_frames": plan["target_accepted_frames"],
        "accepted_observation_frames": accepted_frames,
        "budget_reached": accepted_frames >= plan["target_accepted_frames"],
        "active_recipe_count": len(active_ids),
        "resolved_active_recipe_count": len(active_ids & accepted_recipes),
        "successful_assignment_count": len(successful_assignments),
        "assignment_count": len(assignments),
        "technical_failure_attempt_count": len(technical_failures),
        "interrupted_claims_without_result": orphaned_claims,
        "semantic_failure_recipe_ids": sorted(semantic_failures),
        "credited_discrete_cell_count": len(credited_cells),
        "expected_discrete_cell_count": len(expected_cells),
        "missing_credited_cells": [{"mission": mission, "scenario_index": scenario} for mission, scenario in missing_cells],
        "coverage_complete": not missing_cells,
        "duplicate_validated_results_ignored": duplicate_results,
        "complete": accepted_frames >= plan["target_accepted_frames"] and not missing_cells,
    }
    return inventory


def inventory_command(args: argparse.Namespace) -> None:
    inventory = build_inventory(args.collection.resolve())
    if args.write_snapshot:
        snapshot = args.collection.resolve() / "snapshots" / f"inventory-{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')}.json"
        write_new_json(snapshot, inventory)
        inventory["snapshot"] = str(snapshot)
    print(json.dumps(inventory, indent=2, sort_keys=True))


def activate_reserves(args: argparse.Namespace) -> None:
    root = args.collection.resolve()
    inventory = build_inventory(root)
    failed = set(inventory["semantic_failure_recipe_ids"])
    recipes = read_jsonl(root / "plan" / "recipes.jsonl")
    reserves = [recipe for recipe in recipes if recipe.get("reserve_for") in failed]
    if not reserves:
        print(canonical_json({"activated": 0, "reason": "no unresolved semantic failures with reserves"}))
        return
    existing = {path.stem for path in (root / "assignments").glob("*.json")}
    activation_id = stable_id("reserve", sorted(failed))
    assignment_id = f"assignment-{activation_id}"
    if assignment_id in existing:
        print(canonical_json({"activated": 0, "reason": "already activated", "assignment_id": assignment_id}))
        return
    plan = read_json(root / "plan" / "collection-plan.json")
    assignment = {
        "schema_version": 1,
        "plan_id": plan["plan_id"],
        "plan_version": plan["plan_version"],
        "assignment_id": assignment_id,
        "logical_worker_id": args.worker_id,
        "split": plan["split"],
        "reserve_activation_for": sorted(failed),
        "generator": plan["generator"],
        "recipes": [{**recipe, "active": True} for recipe in reserves],
    }
    write_new_json(root / "assignments" / f"{assignment_id}.json", assignment)
    write_new_json(root / "reserve-activations" / f"{activation_id}.json", {
        "activation_id": activation_id,
        "created_utc": utc_now(),
        "assignment_id": assignment_id,
        "failed_recipe_ids": sorted(failed),
        "reserve_recipe_ids": [recipe["recipe_id"] for recipe in reserves],
    })
    print(canonical_json({"activated": len(reserves), "assignment_id": assignment_id}))


def verify_plan(args: argparse.Namespace) -> None:
    root = args.collection.resolve()
    plan = read_json(root / "plan" / "collection-plan.json")
    recipes = read_jsonl(root / "plan" / "recipes.jsonl")
    active = [recipe for recipe in recipes if recipe["active"]]
    ids = [recipe["recipe_id"] for recipe in recipes]
    indices = [recipe["episode_index"] for recipe in recipes]
    if len(ids) != len(set(ids)) or len(indices) != len(set(indices)):
        raise ValueError("recipe IDs and episode indices must be globally unique")
    cells = Counter((recipe["mission"], recipe["scenario_index"]) for recipe in active)
    missing = [
        (mission, scenario)
        for mission, count in MISSION_COUNTS.items()
        for scenario in range(count)
        if cells[(mission, scenario)] == 0
    ]
    assignments = [read_json(path) for path in sorted((root / "assignments").glob("*.json"))]
    assigned = [recipe["recipe_id"] for assignment in assignments for recipe in assignment["recipes"]]
    if Counter(assigned) != Counter(recipe["recipe_id"] for recipe in active):
        raise ValueError("active recipes are not assigned exactly once")
    report = {
        "valid": not missing,
        "plan_id": plan["plan_id"],
        "active_recipes": len(active),
        "assignments": len(assignments),
        "discrete_cells": len(cells),
        "expected_discrete_cells": sum(MISSION_COUNTS.values()),
        "missing_cells": missing,
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    if missing:
        raise ValueError("plan omits mandatory discrete cells")


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description=__doc__)
    commands = root.add_subparsers(dest="command", required=True)
    plan = commands.add_parser("plan", help="create an immutable collection plan")
    plan.add_argument("collection", type=Path)
    plan.add_argument("--frame-budget", type=int, required=True)
    plan.add_argument("--workers", type=int, default=1)
    plan.add_argument("--recipes-per-assignment", type=int, default=32)
    plan.add_argument("--episode-seconds", type=int, default=10)
    plan.add_argument("--observation-rate", type=int, default=20)
    plan.add_argument("--width", type=int, default=256)
    plan.add_argument("--height", type=int, default=256)
    plan.add_argument("--storage-format", choices=("webp_parquet", "png_jsonl"), default="webp_parquet")
    plan.add_argument("--webp-effort", type=int, default=0)
    plan.add_argument("--seed-start", type=int, default=1000)
    plan.add_argument("--split", default="train")
    plan.add_argument("--plan-id")
    plan.set_defaults(func=create_plan)
    inventory = commands.add_parser("inventory", help="reconstruct collection state from immutable results")
    inventory.add_argument("collection", type=Path)
    inventory.add_argument("--write-snapshot", action="store_true")
    inventory.set_defaults(func=inventory_command)
    reserves = commands.add_parser("activate-reserves", help="assign predefined reserves for semantic failures")
    reserves.add_argument("collection", type=Path)
    reserves.add_argument("--worker-id", type=int, default=0)
    reserves.set_defaults(func=activate_reserves)
    verify = commands.add_parser("verify-plan", help="validate complete catalog coverage and non-overlap")
    verify.add_argument("collection", type=Path)
    verify.set_defaults(func=verify_plan)
    return root


def main() -> int:
    args = parser().parse_args()
    if getattr(args, "workers", 1) < 1 or getattr(args, "recipes_per_assignment", 1) < 1:
        raise ValueError("workers and recipes-per-assignment must be positive")
    args.func(args)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
