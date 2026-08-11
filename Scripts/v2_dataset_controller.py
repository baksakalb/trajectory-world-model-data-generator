#!/usr/bin/env python3
"""Plan and inventory persistent semi-Markov-only V2 collection."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


PLAN_VERSION = "trajectory-throw-v2-semi-markov-only-1"
CONTRACT_VERSION = "shared-persistent-semi-markov-1"
MINIMUM_EPISODE_SECONDS = 120
MAXIMUM_EPISODE_SECONDS = 180
GENERATOR_PIPELINE_FILES = (
    "Scripts/dataset_worker.py",
    "Scripts/finalize_production_dataset.py",
    "Scripts/review_dataset.py",
    "Scripts/v2_dataset_controller.py",
)


def canonical_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def stable_id(prefix: str, value: Any, length: int = 16) -> str:
    digest = hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()
    return f"{prefix}-{digest[:length]}"


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def read_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    with path.open("r", encoding="utf-8") as handle:
        return [json.loads(line) for line in handle if line.strip()]


def write_new_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as handle:
        json.dump(value, handle, indent=2, sort_keys=True)
        handle.write("\n")


def write_new_jsonl(path: Path, values: Iterable[Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as handle:
        for value in values:
            handle.write(canonical_json(value) + "\n")


def generator_source_fingerprint() -> str:
    root = Path(__file__).resolve().parent.parent
    digest = hashlib.sha256()
    runtime_sources = sorted(
        path.relative_to(root).as_posix()
        for path in (root / "Source" / "he_grenade_game").rglob("*")
        if path.is_file()
        and path.suffix.lower() in {".cpp", ".h", ".cs"}
        and not path.name.endswith("Tests.cpp")
    )
    for relative in (*GENERATOR_PIPELINE_FILES, *runtime_sources):
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update((root / relative).read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def validate_episode_seconds(seconds: int) -> None:
    if not MINIMUM_EPISODE_SECONDS <= seconds <= MAXIMUM_EPISODE_SECONDS:
        raise ValueError(
            f"semi-Markov episodes must last {MINIMUM_EPISODE_SECONDS}-"
            f"{MAXIMUM_EPISODE_SECONDS} seconds"
        )


def build_recipes(
    frame_budget: int,
    episode_seconds: int,
    observation_rate: int,
    seed_start: int,
    evaluation_percent: int,
) -> list[dict[str, Any]]:
    if frame_budget <= 0:
        raise ValueError("frame budget must be positive")
    validate_episode_seconds(episode_seconds)
    if observation_rate <= 0:
        raise ValueError("observation rate must be positive")
    if not 0 <= evaluation_percent <= 100:
        raise ValueError("evaluation percent must be between 0 and 100")

    frames_per_episode = episode_seconds * observation_rate + 1
    recipes: list[dict[str, Any]] = []
    remaining = frame_budget
    episode_index = 0
    while remaining > 0:
        seed = seed_start + episode_index
        split_bucket = int(
            hashlib.sha256(f"v2-split:{seed}".encode("ascii")).hexdigest()[:8], 16
        ) % 100
        recipe_identity = {
            "contract_version": CONTRACT_VERSION,
            "episode_index": episode_index,
            "seed": seed,
            "episode_seconds": episode_seconds,
            "observation_rate": observation_rate,
        }
        recipe_id = stable_id("v2recipe", recipe_identity)
        recipes.append(
            {
                "active": True,
                "recipe_id": recipe_id,
                "recipe_index": episode_index,
                "episode_index": episode_index,
                "scenario_index": 0,
                "seed": seed,
                "mission": "semi_markov",
                "source": "semi_markov",
                "family": "semi_markov",
                "replay_identity": stable_id("v2replay", recipe_identity),
                "split": "evaluation" if split_bucket < evaluation_percent else "train",
                "planned_credited_frames": min(remaining, frames_per_episode),
                "expected_credited_frames": frames_per_episode,
            }
        )
        remaining -= frames_per_episode
        episode_index += 1
    return recipes


def planned_distribution(recipes: Iterable[dict[str, Any]]) -> dict[str, Any]:
    split_frames: Counter[str] = Counter()
    total = 0
    for recipe in recipes:
        frames = int(recipe["planned_credited_frames"])
        total += frames
        split_frames[str(recipe["split"])] += frames
    return {
        "total_credited_frames": total,
        "source_frames": {"semi_markov": total},
        "source_shares": {"semi_markov": 1.0 if total else 0.0},
        "split_frames": dict(sorted(split_frames.items())),
    }


def create_plan(args: argparse.Namespace) -> None:
    root = args.collection.resolve()
    if root.exists() and any(root.iterdir()):
        raise ValueError(f"collection directory is not empty: {root}")
    validate_episode_seconds(args.episode_seconds)
    if args.workers < 1 or args.recipes_per_assignment < 1:
        raise ValueError("workers and recipes-per-assignment must be positive")
    if not 64 <= args.width <= 4096 or not 64 <= args.height <= 4096:
        raise ValueError("width and height must be between 64 and 4096")

    recipes = build_recipes(
        args.frame_budget,
        args.episode_seconds,
        args.observation_rate,
        args.seed_start,
        args.evaluation_percent,
    )
    source_fingerprint = generator_source_fingerprint()
    generator = {
        "stage": "trajectory_throw_v2",
        "episode_seconds": args.episode_seconds,
        "observation_rate": args.observation_rate,
        "width": args.width,
        "height": args.height,
        "storage_format": args.storage_format,
        "webp_effort": args.webp_effort,
        "seed_start": args.seed_start,
    }
    identity = {
        "plan_version": PLAN_VERSION,
        "contract_version": CONTRACT_VERSION,
        "generator_source_sha256": source_fingerprint,
        "frame_budget": args.frame_budget,
        "generator": generator,
        "replays": [recipe["replay_identity"] for recipe in recipes],
    }
    plan_id = args.plan_id or stable_id("v2plan", identity)
    assignments: list[dict[str, Any]] = []
    for number, start in enumerate(range(0, len(recipes), args.recipes_per_assignment)):
        assignment_id = f"assignment-{number:06d}"
        assignments.append(
            {
                "schema_version": 2,
                "plan_id": plan_id,
                "plan_version": PLAN_VERSION,
                "assignment_id": assignment_id,
                "assignment_number": number,
                "dispatch_wave": number // args.workers,
                "logical_worker_id": number % args.workers,
                "split": "mixed",
                "generator": generator,
                "recipes": recipes[start : start + args.recipes_per_assignment],
            }
        )

    plan = {
        "schema_version": 4,
        "plan_version": PLAN_VERSION,
        "plan_id": plan_id,
        "created_utc": utc_now(),
        "contract_version": CONTRACT_VERSION,
        "generator_source_sha256": source_fingerprint,
        "target_accepted_frames": args.frame_budget,
        "active_recipe_count": len(recipes),
        "reserve_recipe_count": 0,
        "assignment_count": len(assignments),
        "worker_count": args.workers,
        "recipes_per_assignment": args.recipes_per_assignment,
        "source_frame_shares": {"semi_markov": 1.0},
        "generator": generator,
        "planned_distribution": planned_distribution(recipes),
    }
    write_new_json(root / "plan" / "collection-plan.json", plan)
    write_new_jsonl(root / "plan" / "recipes.jsonl", recipes)
    for assignment in assignments:
        write_new_json(root / "assignments" / f"{assignment['assignment_id']}.json", assignment)
    print(
        canonical_json(
            {
                "plan_id": plan_id,
                "episodes": len(recipes),
                "assignments": len(assignments),
                "collection": str(root),
            }
        )
    )


def verify_plan(root: Path) -> dict[str, Any]:
    root = root.resolve()
    plan = read_json(root / "plan" / "collection-plan.json")
    recipes = read_jsonl(root / "plan" / "recipes.jsonl")
    assignments = [
        read_json(path) for path in sorted((root / "assignments").glob("*.json"))
    ]
    assigned_ids = [
        recipe["recipe_id"] for assignment in assignments for recipe in assignment["recipes"]
    ]
    recipe_ids = [recipe["recipe_id"] for recipe in recipes]
    sources_are_free_play = all(
        recipe.get("mission") == "semi_markov"
        and recipe.get("source") == "semi_markov"
        and recipe.get("family") == "semi_markov"
        for recipe in recipes
    )
    exact_assignment = Counter(assigned_ids) == Counter(recipe_ids)
    exact_budget = (
        sum(int(recipe["planned_credited_frames"]) for recipe in recipes)
        == int(plan["target_accepted_frames"])
    )
    unique = len(recipe_ids) == len(set(recipe_ids)) and len(
        {recipe["replay_identity"] for recipe in recipes}
    ) == len(recipes)
    valid = sources_are_free_play and exact_assignment and exact_budget and unique
    return {
        "valid": valid,
        "plan_id": plan["plan_id"],
        "episodes": len(recipes),
        "semi_markov_only": sources_are_free_play,
        "assigned_exactly_once": exact_assignment,
        "exact_frame_budget": exact_budget,
        "identities_unique": unique,
        "planned_distribution": planned_distribution(recipes),
    }


def result_files(root: Path) -> list[Path]:
    directory = root.resolve() / "results"
    return sorted(directory.glob("*.json")) if directory.exists() else []


def build_inventory(root: Path) -> dict[str, Any]:
    root = root.resolve()
    plan = read_json(root / "plan" / "collection-plan.json")
    validated_assignments: set[str] = set()
    credited = 0
    produced = 0
    resolved: set[str] = set()
    technical_failures = 0
    for path in result_files(root):
        result = read_json(path)
        if result.get("technical_result") != "validated":
            technical_failures += 1
            continue
        assignment_id = str(result["assignment_id"])
        if assignment_id in validated_assignments:
            continue
        validated_assignments.add(assignment_id)
        credited += int(result.get("accepted_observation_frames", 0))
        produced += int(result.get("produced_observation_frames", 0))
        resolved.update(result.get("resolved_recipe_ids", []))
    target = int(plan["target_accepted_frames"])
    return {
        "schema_version": 3,
        "plan_id": plan["plan_id"],
        "reconstructed_utc": utc_now(),
        "target_accepted_frames": target,
        "accepted_observation_frames": credited,
        "produced_observation_frames": produced,
        "credited_frames_by_source": {"semi_markov": credited},
        "budget_reached": credited >= target,
        "technical_failure_attempt_count": technical_failures,
        "validated_assignment_count": len(validated_assignments),
        "resolved_recipe_count": len(resolved),
        "complete": credited >= target,
    }


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    commands = result.add_subparsers(dest="command", required=True)
    plan = commands.add_parser("plan")
    plan.add_argument("collection", type=Path)
    plan.add_argument("--frame-budget", type=int, required=True)
    plan.add_argument("--workers", type=int, default=1)
    plan.add_argument("--recipes-per-assignment", type=int, default=8)
    plan.add_argument("--episode-seconds", type=int, default=150)
    plan.add_argument("--observation-rate", type=int, default=20)
    plan.add_argument("--width", type=int, default=384)
    plan.add_argument("--height", type=int, default=384)
    plan.add_argument(
        "--storage-format",
        choices=("webp_parquet", "png_jsonl"),
        default="webp_parquet",
    )
    plan.add_argument("--webp-effort", type=int, default=0)
    plan.add_argument("--seed-start", type=int, default=200000)
    plan.add_argument("--evaluation-percent", type=int, default=10)
    plan.add_argument("--plan-id")
    plan.set_defaults(func=create_plan)

    verify = commands.add_parser("verify-plan")
    verify.add_argument("collection", type=Path)
    verify.set_defaults(func=lambda args: print(json.dumps(verify_plan(args.collection), indent=2, sort_keys=True)))

    inventory = commands.add_parser("inventory")
    inventory.add_argument("collection", type=Path)
    inventory.add_argument("--write-snapshot", action="store_true")

    def inventory_command(args: argparse.Namespace) -> None:
        value = build_inventory(args.collection)
        if args.write_snapshot:
            write_new_json(
                args.collection.resolve() / "inventory" / f"inventory-{stable_id('snapshot', value)}.json",
                value,
            )
        print(json.dumps(value, indent=2, sort_keys=True))

    inventory.set_defaults(func=inventory_command)

    return result


def main() -> int:
    args = parser().parse_args()
    try:
        args.func(args)
    except (OSError, ValueError, KeyError) as exc:
        print(str(exc), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
