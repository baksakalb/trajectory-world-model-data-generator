#!/usr/bin/env python3
"""Immutable local planner, inventory, and reports for combined V2."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from fractions import Fraction
from functools import lru_cache
from pathlib import Path
from typing import Any, Iterable

from v2_catalog import (
    AIM_ACQUISITION_PROFILES,
    ARC_BANDS,
    BASE_FAMILY_COUNTS,
    CATALOG_VERSION,
    DISTANCE_BANDS,
    POST_THROW_CAMERA_PROFILES,
    POST_THROW_MOVEMENT_PROFILES,
    Q_RETENTION_PROFILES,
    SEQUENCE_TEMPLATES,
    SEQUENCE_VERSION,
    audit_slots,
    base_cells,
    catalog_fingerprint,
    sequence_fingerprint,
)


PLAN_VERSION = "trajectory-throw-v2-persistent-semi-markov-local-2"
CONTRACT_VERSION = "v2-canonical-physics-1+human-actions-2+persistent-semi-markov-1"
DEFAULT_CALIBRATION = Path(__file__).with_name("movement_v2_duration_calibration.json")
FAMILY_FRAME_SHARES = {
    "semi_markov": Fraction(70, 100),
    "trajectory_view": Fraction(7, 100),
    "solid_object": Fraction(6, 100),
    "wall_corner": Fraction(4, 100),
    "floor_observe": Fraction(3, 100),
    "ramp": Fraction(3, 100),
    "hoop": Fraction(3, 100),
    "temporal": Fraction(3, 100),
    "out_of_bounds": Fraction(1, 100),
}
SOURCE_FRAME_SHARES = {
    "semi_markov": Fraction(70, 100),
    "mission": Fraction(30, 100),
}
PRODUCTION_BUDGET_QUANTUM = 100
RESERVES_PER_RECIPE = 2
GENERATOR_PIPELINE_FILES = (
    "Scripts/dataset_worker.py",
    "Scripts/finalize_production_dataset.py",
    "Scripts/review_dataset.py",
    "Scripts/v2_audit.py",
    "Scripts/v2_calibration.py",
    "Scripts/v2_catalog.py",
    "Scripts/v2_dataset_controller.py",
)


def canonical_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def stable_id(prefix: str, value: Any, length: int = 16) -> str:
    digest = hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()
    return f"{prefix}-{digest[:length]}"


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


@lru_cache(maxsize=None)
def spread_indices(count: int) -> tuple[int, ...]:
    if count <= 0:
        return ()
    selected: list[int] = []
    remaining = set(range(count))
    distances = [math.inf] * count
    while remaining:
        candidate = max(remaining, key=lambda value: (distances[value], -value))
        selected.append(candidate)
        remaining.remove(candidate)
        for value in remaining:
            distances[value] = min(distances[value], abs(value - candidate))
    return tuple(selected)


def load_calibration(path: Path = DEFAULT_CALIBRATION) -> dict[str, Any]:
    value = read_json(path.resolve())
    durations = value.get("expected_credited_frames_by_family", {})
    if set(durations) != set(FAMILY_FRAME_SHARES):
        raise ValueError("V2 duration calibration has the wrong family set")
    if any(int(frames) <= 0 for frames in durations.values()):
        raise ValueError("V2 duration calibration contains a non-positive duration")
    sequences = value.get("expected_credited_frames_by_sequence_grenade_count", {})
    required_counts = {template.grenade_count for template in SEQUENCE_TEMPLATES}
    if any(str(count) not in sequences for count in required_counts):
        raise ValueError("V2 duration calibration omits sequence grenade counts")
    return value


def mandatory_order() -> list[dict[str, Any]]:
    cells = base_cells()
    grouped = {
        family: [cell for cell in cells if cell["family"] == family]
        for family in BASE_FAMILY_COUNTS
    }
    result: list[dict[str, Any]] = []
    for ordinal in range(max(BASE_FAMILY_COUNTS.values())):
        for family in FAMILY_FRAME_SHARES:
            order = spread_indices(len(grouped[family]))
            if ordinal < len(order):
                result.append(grouped[family][order[ordinal]])
    return result


def sequence_connections(template_index: int) -> list[str]:
    template = SEQUENCE_TEMPLATES[template_index]
    cells = base_cells()
    grouped = defaultdict(list)
    for cell in cells:
        grouped[cell["family"]].append(cell)

    connected: list[str] = []
    for step, family in enumerate(template.base_family_pattern):
        choices = grouped[family]
        connected.append(choices[(template_index * 37 + step * 101) % len(choices)]["cell_id"])
    return connected


def sequence_steps(template_index: int) -> list[dict[str, Any]]:
    """Materialize every temporal step; IDs alone are not executable geometry."""
    template = SEQUENCE_TEMPLATES[template_index]
    by_id = {cell["cell_id"]: cell for cell in base_cells()}
    return [
        {
            "step_index": step,
            "cell_id": cell_id,
            "cell": by_id[cell_id],
            "aim_acquisition_profile": template.aim_profiles[step % len(template.aim_profiles)],
            "q_retention_profile": template.q_profiles[step % len(template.q_profiles)],
            "post_throw_movement_profile": template.movement_profiles[step % len(template.movement_profiles)],
            "post_throw_camera_profile": template.camera_profiles[step % len(template.camera_profiles)],
        }
        for step, cell_id in enumerate(sequence_connections(template_index))
    ]


def temporal_profiles(repetition: int, scenario_index: int) -> dict[str, str]:
    return {
        "aim_acquisition_profile": AIM_ACQUISITION_PROFILES[(scenario_index + repetition) % len(AIM_ACQUISITION_PROFILES)],
        "post_throw_camera_profile": POST_THROW_CAMERA_PROFILES[(2 * scenario_index + repetition) % len(POST_THROW_CAMERA_PROFILES)],
        "q_retention_profile": Q_RETENTION_PROFILES[(scenario_index + 2 * repetition) % len(Q_RETENTION_PROFILES)],
        "post_throw_movement_profile": POST_THROW_MOVEMENT_PROFILES[(3 * scenario_index + repetition) % len(POST_THROW_MOVEMENT_PROFILES)],
    }


def split_for(recipe_index: int, seed_start: int, evaluation_percent: int) -> str:
    if evaluation_percent <= 0:
        return "train"
    rank = int(hashlib.sha256(f"{seed_start}:split:{recipe_index}".encode()).hexdigest()[:8], 16) % 100
    return "evaluation" if rank < evaluation_percent else "train"


def expected_frames(
    calibration: dict[str, Any], family: str,
    sequence_index: int | None = None, cell: dict[str, Any] | None = None,
) -> int:
    if sequence_index is None:
        if family == "semi_markov" and cell is not None:
            seconds = {
                "short": 120, "medium": 140, "long": 160, "very_long": 180,
            }[str(cell["hold_band"])]
            return seconds * int(calibration["observation_rate_hz"]) + 1
        return int(calibration["expected_credited_frames_by_family"][family])
    count = min(5, SEQUENCE_TEMPLATES[sequence_index].grenade_count)
    return int(calibration["expected_credited_frames_by_sequence_grenade_count"][str(count)])


def build_recipe(
    *, recipe_index: int, seed_start: int, cell: dict[str, Any], repetition: int,
    calibration: dict[str, Any], evaluation_percent: int, schedule_phase: str,
    sequence_index: int | None = None, reserve_for: str | None = None,
) -> dict[str, Any]:
    split = split_for(recipe_index, seed_start, evaluation_percent)
    identity = {
        "catalog_version": CATALOG_VERSION, "recipe_index": recipe_index,
        "cell_id": cell["cell_id"], "repetition_index": repetition,
        "sequence_template_id": None if sequence_index is None else SEQUENCE_TEMPLATES[sequence_index].template_id,
        "split": split, "seed_start": seed_start, "reserve_for": reserve_for,
    }
    replay_identity = stable_id("v2replay", identity, 24)
    recipe = {
        "recipe_id": stable_id("v2recipe", identity),
        "recipe_index": recipe_index,
        "episode_index": recipe_index,
        "seed": seed_start + recipe_index,
        "split": split,
        "replay_identity": replay_identity,
        "source": cell["source"],
        "mission": cell["family"],
        "family": cell["family"],
        "scenario_index": int(cell["scenario_index"]),
        "cell_id": cell["cell_id"],
        "cell": cell,
        "continuous_sample_ordinal": repetition,
        "refinement_level": 0 if repetition == 0 else int(math.log2(repetition)) + 1,
        "repetition_index": repetition,
        "continuous_strata": {
            "distance": DISTANCE_BANDS[(recipe_index + repetition) % 3],
            "arc": ARC_BANDS[(2 * recipe_index + repetition) % 3],
            "jitter_key": stable_id("jitter", identity, 24),
        },
        **temporal_profiles(repetition, int(cell["scenario_index"])),
        "sequence_template_id": None,
        "sequence": None,
        "sequence_base_cell_ids": [],
        "expected_credited_frames": expected_frames(calibration, cell["family"], sequence_index, cell),
        "planned_credited_frames": 0,
        "schedule_phase": schedule_phase,
        "reserve_for": reserve_for,
        "active": reserve_for is None,
    }
    # Trajectory-view cells deliberately enumerate the visible acquisition
    # gesture.  Do not let the generic temporal stratum silently replace that
    # mission identity (this previously turned named pitch/yaw demonstrations
    # into unrelated profiles).
    if cell["family"] == "trajectory_view":
        recipe["aim_acquisition_profile"] = cell["interaction_mode"]
    if sequence_index is not None:
        template = SEQUENCE_TEMPLATES[sequence_index]
        recipe["sequence_template_id"] = template.template_id
        recipe["sequence"] = template.as_dict()
        recipe["sequence_base_cell_ids"] = sequence_connections(sequence_index)
        recipe["sequence"]["steps"] = sequence_steps(sequence_index)
        recipe["aim_acquisition_profile"] = template.aim_profiles[0]
        recipe["q_retention_profile"] = template.q_profiles[0]
        recipe["post_throw_movement_profile"] = template.movement_profiles[0]
        recipe["post_throw_camera_profile"] = template.camera_profiles[0]
    return recipe


def target_family_frames(frame_budget: int) -> dict[str, int]:
    if frame_budget % PRODUCTION_BUDGET_QUANTUM:
        raise ValueError(
            f"production V2 frame target must be divisible by {PRODUCTION_BUDGET_QUANTUM} "
            "for exact 70/7/6/4/3/3/3/3/1 integer-frame allocation"
        )
    result = {family: int(frame_budget * share) for family, share in FAMILY_FRAME_SHARES.items()}
    if sum(result.values()) != frame_budget:
        raise AssertionError("exact V2 family allocation did not sum to X")
    return result


def minimum_feasible_frame_budget(calibration: dict[str, Any] | None = None) -> int:
    calibration = calibration or load_calibration()
    mandatory = Counter()
    for cell in mandatory_order():
        mandatory[cell["family"]] += expected_frames(calibration, cell["family"], cell=cell)
    for sequence_index, template in enumerate(SEQUENCE_TEMPLATES):
        family = template.base_family_pattern[0]
        mandatory[family] += expected_frames(calibration, family, sequence_index)
    floor = max(math.ceil(Fraction(mandatory[family], 1) / share) for family, share in FAMILY_FRAME_SHARES.items())
    return int(math.ceil(floor / PRODUCTION_BUDGET_QUANTUM) * PRODUCTION_BUDGET_QUANTUM)


def build_schedule(
    frame_budget: int, calibration: dict[str, Any], seed_start: int,
    evaluation_percent: int, diagnostic_only: bool,
) -> list[dict[str, Any]]:
    targets = (
        {family: 2**62 for family in FAMILY_FRAME_SHARES}
        if diagnostic_only else target_family_frames(frame_budget)
    )
    cells = mandatory_order()
    grouped = defaultdict(list)
    for cell in base_cells():
        grouped[cell["family"]].append(cell)
    recipes: list[dict[str, Any]] = []
    repetitions: Counter[str] = Counter()
    expected_by_family: Counter[str] = Counter()

    def append(cell: dict[str, Any], phase: str, sequence_index: int | None = None) -> None:
        index = len(recipes)
        repetition = repetitions[cell["cell_id"]]
        recipe = build_recipe(
            recipe_index=index, seed_start=seed_start, cell=cell,
            repetition=repetition, calibration=calibration,
            evaluation_percent=evaluation_percent, schedule_phase=phase,
            sequence_index=sequence_index,
        )
        recipes.append(recipe)
        repetitions[cell["cell_id"]] += 1
        expected_by_family[cell["family"]] += int(recipe["expected_credited_frames"])

    if diagnostic_only:
        # Diagnostic plans are explicitly budget-bounded and make no coverage claim.
        cursor = 0
        produced = 0
        while produced < frame_budget:
            cell = cells[cursor % len(cells)]
            append(cell, "diagnostic")
            produced += int(recipes[-1]["expected_credited_frames"])
            cursor += 1
    else:
        for cell in cells:
            append(cell, "mandatory")
        for sequence_index, template in enumerate(SEQUENCE_TEMPLATES):
            family = template.base_family_pattern[0]
            first_cell_id = sequence_connections(sequence_index)[0]
            cell = next(item for item in grouped[family] if item["cell_id"] == first_cell_id)
            append(cell, "mandatory_sequence", sequence_index)

        cursors = Counter()
        while any(expected_by_family[family] < targets[family] for family in FAMILY_FRAME_SHARES):
            family = min(
                (name for name in FAMILY_FRAME_SHARES if expected_by_family[name] < targets[name]),
                key=lambda name: (
                    Fraction(expected_by_family[name], 1) / FAMILY_FRAME_SHARES[name],
                    list(FAMILY_FRAME_SHARES).index(name),
                ),
            )
            order = spread_indices(len(grouped[family]))
            cell = grouped[family][order[cursors[family] % len(order)]]
            append(cell, "weighted")
            cursors[family] += 1

    # Exact planned credit caps produce the frozen integer allocation; any extra
    # generated event tail remains authoritative but does not receive credit.
    remaining = dict(target_family_frames(frame_budget)) if not diagnostic_only else None
    for recipe in recipes:
        family = recipe["family"]
        if remaining is None:
            credit = min(int(recipe["expected_credited_frames"]), max(0, frame_budget - sum(int(item["planned_credited_frames"]) for item in recipes)))
        else:
            credit = min(int(recipe["expected_credited_frames"]), max(0, remaining[family]))
            remaining[family] -= credit
        recipe["planned_credited_frames"] = credit
    return recipes


def planned_distribution(recipes: list[dict[str, Any]]) -> dict[str, Any]:
    family = Counter()
    source = Counter()
    split = Counter()
    temporal: dict[str, Counter[str]] = {
        "aim_acquisition_profile": Counter(),
        "post_throw_camera_profile": Counter(),
        "q_retention_profile": Counter(),
        "post_throw_movement_profile": Counter(),
        "sequence_template_id": Counter(),
    }
    for recipe in recipes:
        frames = int(recipe.get("planned_credited_frames", 0))
        if frames <= 0:
            continue
        family[recipe["family"]] += frames
        source[recipe["source"]] += frames
        split[recipe["split"]] += frames
        for dimension in temporal:
            value = recipe.get(dimension)
            if value:
                temporal[dimension][str(value)] += frames
    total = sum(family.values())
    return {
        "total_credited_frames": total,
        "source_frames": dict(source),
        "source_shares": {name: value / total if total else 0.0 for name, value in sorted(source.items())},
        "family_frames": {name: family[name] for name in FAMILY_FRAME_SHARES},
        "family_shares": {name: family[name] / total if total else 0.0 for name in FAMILY_FRAME_SHARES},
        "split_frames": dict(split),
        "temporal": {dimension: dict(sorted(values.items())) for dimension, values in temporal.items()},
    }


def create_plan(args: argparse.Namespace) -> None:
    root = args.collection.resolve()
    if root.exists() and any(root.iterdir()):
        raise ValueError(f"collection directory is not empty: {root}")
    calibration = load_calibration(args.duration_calibration)
    minimum = minimum_feasible_frame_budget(calibration)
    diagnostic = args.frame_budget < minimum
    if diagnostic and not args.allow_infeasible_diagnostic:
        raise ValueError(f"requested X={args.frame_budget} is below calibrated V2 floor {minimum}")
    calibration_matches_contract = (
        str(calibration.get("contract_version", "")) == CONTRACT_VERSION
    )
    if (
        not diagnostic
        and not args.allow_unqualified_calibration
        and (not bool(calibration.get("qualified")) or not calibration_matches_contract)
    ):
        raise ValueError(
            "production V2 plan requires a qualified local duration calibration "
            f"for {CONTRACT_VERSION}"
        )
    if not diagnostic:
        target_family_frames(args.frame_budget)
    recipes = build_schedule(
        args.frame_budget, calibration, args.seed_start,
        args.evaluation_percent, diagnostic,
    )
    active = [recipe for recipe in recipes if int(recipe["planned_credited_frames"]) > 0]
    identity = {
        "plan_version": PLAN_VERSION, "contract_version": CONTRACT_VERSION,
        "catalog_sha256": catalog_fingerprint(), "sequence_sha256": sequence_fingerprint(),
        "generator_source_sha256": generator_source_fingerprint(),
        "frame_budget": args.frame_budget, "seed_start": args.seed_start,
        "evaluation_percent": args.evaluation_percent,
        "calibration_version": calibration["calibration_version"],
        "generator": {
            "stage": "trajectory_throw_v2", "observation_rate": args.observation_rate,
            "width": args.width, "height": args.height,
            "storage_format": args.storage_format, "webp_effort": args.webp_effort,
        },
        "recipe_replay_prefix": [recipe["replay_identity"] for recipe in active],
    }
    plan_id = args.plan_id or stable_id("v2plan", identity)
    generator = {**identity["generator"], "episode_seconds": args.episode_seconds, "seed_start": args.seed_start}
    assignments: list[dict[str, Any]] = []
    for assignment_number, start in enumerate(range(0, len(active), args.recipes_per_assignment)):
        block = active[start : start + args.recipes_per_assignment]
        assignment_id = f"assignment-{assignment_number:06d}"
        logical_worker = assignment_number % args.workers
        assignments.append({
            "schema_version": 2, "plan_id": plan_id,
            "plan_version": PLAN_VERSION, "assignment_id": assignment_id,
            "assignment_number": assignment_number,
            "dispatch_wave": assignment_number // args.workers,
            "logical_worker_id": logical_worker, "split": "mixed",
            "generator": generator, "recipes": block,
        })
    distribution = planned_distribution(active)
    plan = {
        "schema_version": 3, "plan_version": PLAN_VERSION, "plan_id": plan_id,
        "created_utc": utc_now(), "contract_version": CONTRACT_VERSION,
        "catalog_version": CATALOG_VERSION, "catalog_sha256": catalog_fingerprint(),
        "sequence_version": SEQUENCE_VERSION, "sequence_sha256": sequence_fingerprint(),
        "generator_source_sha256": generator_source_fingerprint(),
        "catalog_counts": BASE_FAMILY_COUNTS, "mandatory_base_cell_count": len(base_cells()),
        "mandatory_sequence_template_count": len(SEQUENCE_TEMPLATES),
        "audit_slot_count": len(audit_slots()),
        "duration_calibration_version": calibration["calibration_version"],
        "duration_calibration_qualified": bool(calibration.get("qualified")),
        "target_accepted_frames": args.frame_budget,
        "target_family_frames": None if diagnostic else target_family_frames(args.frame_budget),
        "minimum_feasible_accepted_frames": minimum,
        "production_budget_quantum": PRODUCTION_BUDGET_QUANTUM,
        "diagnostic_only": diagnostic, "distribution_feasible": not diagnostic,
        "source_frame_shares": {name: float(value) for name, value in SOURCE_FRAME_SHARES.items()},
        "family_frame_shares": {name: float(value) for name, value in FAMILY_FRAME_SHARES.items()},
        "evaluation_percent": args.evaluation_percent,
        "active_recipe_count": len(active), "reserve_recipe_count": len(active) * RESERVES_PER_RECIPE,
        "assignment_count": len(assignments), "worker_count": args.workers,
        "recipes_per_assignment": args.recipes_per_assignment,
        "generator": generator, "planned_distribution": distribution,
    }
    reserves: list[dict[str, Any]] = []
    reserve_index = len(recipes)
    for primary in active:
        for reserve_ordinal in range(RESERVES_PER_RECIPE):
            reserve = build_recipe(
                recipe_index=reserve_index, seed_start=args.seed_start,
                cell=primary["cell"], repetition=int(primary["repetition_index"]) + reserve_ordinal + 1,
                calibration=calibration, evaluation_percent=args.evaluation_percent,
                schedule_phase="reserve", reserve_for=primary["recipe_id"],
            )
            reserve["planned_credited_frames"] = int(primary["planned_credited_frames"])
            reserves.append(reserve)
            reserve_index += 1
    write_new_json(root / "plan" / "collection-plan.json", plan)
    write_new_jsonl(root / "plan" / "recipes.jsonl", [*active, *reserves])
    write_new_json(root / "plan" / "audit-slots.json", {"slots": audit_slots()})
    for assignment in assignments:
        write_new_json(root / "assignments" / f"{assignment['assignment_id']}.json", assignment)
    print(canonical_json({
        "plan_id": plan_id, "diagnostic_only": diagnostic,
        "minimum_feasible_frames": minimum, "active_recipes": len(active),
        "reserve_recipes": len(reserves), "assignments": len(assignments),
        "collection": str(root),
    }))


def create_qualification_plan(args: argparse.Namespace) -> None:
    """Create a local-only evidence plan without making production-budget claims."""
    root = args.collection.resolve()
    if root.exists() and any(root.iterdir()):
        raise ValueError(f"collection directory is not empty: {root}")
    calibration = load_calibration(args.duration_calibration)
    cells = base_cells()
    by_cell_id = {cell["cell_id"]: cell for cell in cells}
    by_template_id = {
        template.template_id: index for index, template in enumerate(SEQUENCE_TEMPLATES)
    }
    requested_cells = list(by_cell_id) if args.all_base_cells else list(args.cell_id)
    if args.audit_cells:
        requested_cells.extend(slot["cell_id"] for slot in audit_slots())
    requested_sequences = (
        list(by_template_id) if args.all_sequences else list(args.sequence_template_id)
    )
    unknown_cells = sorted(set(requested_cells) - set(by_cell_id))
    unknown_sequences = sorted(set(requested_sequences) - set(by_template_id))
    if unknown_cells:
        raise ValueError(f"unknown V2 qualification cell IDs: {unknown_cells}")
    if unknown_sequences:
        raise ValueError(f"unknown V2 sequence template IDs: {unknown_sequences}")
    if not requested_cells and not requested_sequences:
        raise ValueError("qualification-plan requires an explicit cell/template selection")

    recipes: list[dict[str, Any]] = []
    repetitions: Counter[str] = Counter()

    def append_recipe(cell: dict[str, Any], sequence_index: int | None = None) -> None:
        index = len(recipes)
        repetition = repetitions[cell["cell_id"]]
        recipe = build_recipe(
            recipe_index=index, seed_start=args.seed_start, cell=cell,
            repetition=repetition, calibration=calibration,
            evaluation_percent=0, schedule_phase="local_qualification",
            sequence_index=sequence_index,
        )
        recipe["planned_credited_frames"] = int(recipe["expected_credited_frames"])
        recipe["qualification_only"] = True
        recipes.append(recipe)
        repetitions[cell["cell_id"]] += 1

    for cell_id in dict.fromkeys(requested_cells):
        append_recipe(by_cell_id[cell_id])
    for template_id in dict.fromkeys(requested_sequences):
        sequence_index = by_template_id[template_id]
        append_recipe(by_cell_id[sequence_connections(sequence_index)[0]], sequence_index)

    generator = {
        "stage": "trajectory_throw_v2", "observation_rate": args.observation_rate,
        "width": args.width, "height": args.height,
        "storage_format": args.storage_format, "webp_effort": args.webp_effort,
        "episode_seconds": args.episode_seconds, "seed_start": args.seed_start,
    }
    identity = {
        "plan_version": PLAN_VERSION, "contract_version": CONTRACT_VERSION,
        "catalog_sha256": catalog_fingerprint(), "sequence_sha256": sequence_fingerprint(),
        "generator_source_sha256": generator_source_fingerprint(),
        "qualification_recipe_ids": [recipe["replay_identity"] for recipe in recipes],
        "generator": generator,
    }
    plan_id = args.plan_id or stable_id("v2qualification", identity)
    assignments: list[dict[str, Any]] = []
    for assignment_number, start in enumerate(range(0, len(recipes), args.recipes_per_assignment)):
        block = recipes[start : start + args.recipes_per_assignment]
        assignments.append({
            "schema_version": 2, "plan_id": plan_id, "plan_version": PLAN_VERSION,
            "assignment_id": f"assignment-{assignment_number:06d}",
            "assignment_number": assignment_number,
            "dispatch_wave": assignment_number // args.workers,
            "logical_worker_id": assignment_number % args.workers,
            "split": "qualification", "qualification_only": True,
            "generator": generator, "recipes": block,
        })
    target = sum(int(recipe["planned_credited_frames"]) for recipe in recipes)
    plan = {
        "schema_version": 3, "plan_version": PLAN_VERSION, "plan_id": plan_id,
        "created_utc": utc_now(), "contract_version": CONTRACT_VERSION,
        "catalog_version": CATALOG_VERSION, "catalog_sha256": catalog_fingerprint(),
        "sequence_version": SEQUENCE_VERSION, "sequence_sha256": sequence_fingerprint(),
        "generator_source_sha256": generator_source_fingerprint(),
        "qualification_only": True, "diagnostic_only": True,
        "distribution_feasible": False, "target_accepted_frames": target,
        "selected_base_cell_count": len(dict.fromkeys(requested_cells)),
        "selected_sequence_template_count": len(dict.fromkeys(requested_sequences)),
        "active_recipe_count": len(recipes), "reserve_recipe_count": 0,
        "assignment_count": len(assignments), "worker_count": args.workers,
        "recipes_per_assignment": args.recipes_per_assignment,
        "generator": generator, "planned_distribution": planned_distribution(recipes),
    }
    write_new_json(root / "plan" / "collection-plan.json", plan)
    write_new_jsonl(root / "plan" / "recipes.jsonl", recipes)
    write_new_json(root / "plan" / "audit-slots.json", {"slots": audit_slots()})
    for assignment in assignments:
        write_new_json(root / "assignments" / f"{assignment['assignment_id']}.json", assignment)
    print(canonical_json({
        "plan_id": plan_id, "qualification_only": True,
        "recipes": len(recipes), "assignments": len(assignments),
        "collection": str(root),
    }))


def verify_plan(root: Path) -> dict[str, Any]:
    root = root.resolve()
    plan = read_json(root / "plan" / "collection-plan.json")
    recipes = read_jsonl(root / "plan" / "recipes.jsonl")
    active = [recipe for recipe in recipes if recipe.get("active")]
    ids = [recipe["recipe_id"] for recipe in recipes]
    replay = [recipe["replay_identity"] for recipe in recipes]
    if len(ids) != len(set(ids)) or len(replay) != len(set(replay)):
        raise ValueError("V2 recipe and replay identities must be unique")
    assigned = [recipe["recipe_id"] for path in sorted((root / "assignments").glob("*.json")) for recipe in read_json(path)["recipes"]]
    if Counter(assigned) != Counter(recipe["recipe_id"] for recipe in active):
        raise ValueError("active V2 recipes are not assigned exactly once")
    cells = {recipe["cell_id"] for recipe in active}
    expected_cells = {cell["cell_id"] for cell in base_cells()}
    sequences = {recipe.get("sequence_template_id") for recipe in active if recipe.get("sequence_template_id")}
    expected_sequences = {template.template_id for template in SEQUENCE_TEMPLATES}
    missing_cells = sorted(expected_cells - cells)
    missing_sequences = sorted(expected_sequences - sequences)
    distribution = planned_distribution(active)
    valid_distribution = bool(plan.get("diagnostic_only")) or (
        distribution["total_credited_frames"] == int(plan["target_accepted_frames"])
        and all(
            distribution["source_frames"].get(source, 0)
            == int(plan["target_accepted_frames"]) * share.numerator // share.denominator
            for source, share in SOURCE_FRAME_SHARES.items()
        )
    )
    valid = (bool(plan.get("diagnostic_only")) or (not missing_cells and not missing_sequences)) and valid_distribution
    return {
        "valid": valid, "plan_id": plan["plan_id"],
        "active_recipes": len(active), "base_cells": len(cells & expected_cells),
        "expected_base_cells": len(expected_cells), "sequence_templates": len(sequences),
        "expected_sequence_templates": len(expected_sequences),
        "missing_cells": missing_cells, "missing_sequences": missing_sequences,
        "planned_distribution": distribution,
    }


def result_files(root: Path) -> list[Path]:
    return sorted((root / "results").glob("*.json")) if (root / "results").exists() else []


def build_inventory(root: Path) -> dict[str, Any]:
    root = root.resolve()
    plan = read_json(root / "plan" / "collection-plan.json")
    recipes = {recipe["recipe_id"]: recipe for recipe in read_jsonl(root / "plan" / "recipes.jsonl")}
    validated_assignments: set[str] = set()
    resolved: set[str] = set()
    failures: set[str] = set()
    credited = Counter()
    produced = 0
    credited_cells: set[str] = set()
    credited_sequences: set[str] = set()
    technical_failures = 0
    for path in result_files(root):
        result = read_json(path)
        if result.get("technical_result") != "validated":
            technical_failures += 1
            continue
        assignment = str(result["assignment_id"])
        if assignment in validated_assignments:
            continue
        validated_assignments.add(assignment)
        produced += int(result.get("produced_observation_frames", 0))
        failures.update(result.get("semantic_failure_recipe_ids", []))
        for item in result.get("credited_cells", []):
            recipe = recipes.get(str(item.get("recipe_id")))
            if recipe is None:
                continue
            frames = int(item.get("credited_observation_frames", 0))
            credited[recipe["family"]] += frames
            credited_cells.add(recipe["cell_id"])
            if recipe.get("sequence_template_id"):
                credited_sequences.add(recipe["sequence_template_id"])
        resolved.update(result.get("resolved_recipe_ids", []))
    accepted = sum(credited.values())
    expected_cells = {cell["cell_id"] for cell in base_cells()}
    expected_sequences = {template.template_id for template in SEQUENCE_TEMPLATES}
    missing_cells = sorted(expected_cells - credited_cells)
    missing_sequences = sorted(expected_sequences - credited_sequences)
    diagnostic = bool(plan.get("diagnostic_only"))
    return {
        "schema_version": 2, "plan_id": plan["plan_id"], "reconstructed_utc": utc_now(),
        "target_accepted_frames": int(plan["target_accepted_frames"]),
        "accepted_observation_frames": accepted, "produced_observation_frames": produced,
        "credited_frames_by_family": {name: credited[name] for name in FAMILY_FRAME_SHARES},
        "budget_reached": accepted >= int(plan["target_accepted_frames"]),
        "credited_base_cell_count": len(credited_cells), "missing_credited_cells": missing_cells,
        "credited_sequence_template_count": len(credited_sequences), "missing_sequence_templates": missing_sequences,
        "semantic_failure_recipe_ids": sorted(failures),
        "technical_failure_attempt_count": technical_failures,
        "validated_assignment_count": len(validated_assignments),
        "resolved_recipe_count": len(resolved),
        "coverage_complete": not missing_cells and not missing_sequences,
        "complete": accepted >= int(plan["target_accepted_frames"]) and (diagnostic or (not missing_cells and not missing_sequences)),
    }


def activate_reserves(root: Path, worker_id: int) -> dict[str, Any]:
    root = root.resolve()
    inventory = build_inventory(root)
    failed = set(inventory["semantic_failure_recipe_ids"])
    recipes = read_jsonl(root / "plan" / "recipes.jsonl")
    already = {recipe["recipe_id"] for path in (root / "assignments").glob("*.json") for recipe in read_json(path)["recipes"]}
    chosen: list[dict[str, Any]] = []
    by_primary = defaultdict(list)
    for recipe in recipes:
        if recipe.get("reserve_for") in failed and recipe["recipe_id"] not in already:
            by_primary[recipe["reserve_for"]].append(recipe)
    for primary in sorted(failed):
        if by_primary[primary]:
            chosen.append({**by_primary[primary][0], "active": True})
    if not chosen:
        return {"activated": 0, "reason": "no unresolved semantic failure has an unused reserve"}
    activation = stable_id("v2reserve", [recipe["recipe_id"] for recipe in chosen])
    assignment_id = f"assignment-{activation}"
    plan = read_json(root / "plan" / "collection-plan.json")
    assignment = {
        "schema_version": 2, "plan_id": plan["plan_id"], "plan_version": plan["plan_version"],
        "assignment_id": assignment_id, "assignment_number": len(list((root / "assignments").glob("*.json"))),
        "logical_worker_id": worker_id, "split": "mixed", "reserve_activation_for": sorted(failed),
        "generator": plan["generator"], "recipes": chosen,
    }
    write_new_json(root / "assignments" / f"{assignment_id}.json", assignment)
    return {"activated": len(chosen), "assignment_id": assignment_id}


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    commands = result.add_subparsers(dest="command", required=True)
    plan = commands.add_parser("plan")
    plan.add_argument("collection", type=Path)
    plan.add_argument("--frame-budget", type=int, required=True)
    plan.add_argument("--workers", type=int, default=1)
    plan.add_argument("--recipes-per-assignment", type=int, default=16)
    plan.add_argument("--episode-seconds", type=int, default=20)
    plan.add_argument("--observation-rate", type=int, default=20)
    plan.add_argument("--width", type=int, default=384)
    plan.add_argument("--height", type=int, default=384)
    plan.add_argument("--storage-format", choices=("webp_parquet", "png_jsonl"), default="webp_parquet")
    plan.add_argument("--webp-effort", type=int, default=0)
    plan.add_argument("--seed-start", type=int, default=200000)
    plan.add_argument("--evaluation-percent", type=int, default=10)
    plan.add_argument("--duration-calibration", type=Path, default=DEFAULT_CALIBRATION)
    plan.add_argument("--allow-infeasible-diagnostic", action="store_true")
    plan.add_argument("--allow-unqualified-calibration", action="store_true")
    plan.add_argument("--plan-id")
    plan.set_defaults(func=lambda args: create_plan(args))
    qualification = commands.add_parser("qualification-plan")
    qualification.add_argument("collection", type=Path)
    qualification.add_argument("--cell-id", action="append", default=[])
    qualification.add_argument("--sequence-template-id", action="append", default=[])
    qualification.add_argument("--all-base-cells", action="store_true")
    qualification.add_argument("--all-sequences", action="store_true")
    qualification.add_argument("--audit-cells", action="store_true")
    qualification.add_argument("--workers", type=int, default=1)
    qualification.add_argument("--recipes-per-assignment", type=int, default=1)
    qualification.add_argument("--episode-seconds", type=int, default=20)
    qualification.add_argument("--observation-rate", type=int, default=20)
    qualification.add_argument("--width", type=int, default=64)
    qualification.add_argument("--height", type=int, default=64)
    qualification.add_argument("--storage-format", choices=("webp_parquet", "png_jsonl"), default="webp_parquet")
    qualification.add_argument("--webp-effort", type=int, default=0)
    qualification.add_argument("--seed-start", type=int, default=910000)
    qualification.add_argument("--duration-calibration", type=Path, default=DEFAULT_CALIBRATION)
    qualification.add_argument("--plan-id")
    qualification.set_defaults(func=create_qualification_plan)
    verify = commands.add_parser("verify-plan")
    verify.add_argument("collection", type=Path)
    verify.set_defaults(func=lambda args: print(json.dumps(verify_plan(args.collection), indent=2, sort_keys=True)))
    distribution = commands.add_parser("plan-distribution")
    distribution.add_argument("collection", type=Path)
    distribution.set_defaults(func=lambda args: print(json.dumps(verify_plan(args.collection)["planned_distribution"], indent=2, sort_keys=True)))
    inventory = commands.add_parser("inventory")
    inventory.add_argument("collection", type=Path)
    inventory.add_argument("--write-snapshot", action="store_true")
    def inventory_command(args: argparse.Namespace) -> None:
        value = build_inventory(args.collection)
        if args.write_snapshot:
            path = args.collection.resolve() / "snapshots" / f"inventory-{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')}.json"
            write_new_json(path, value)
            value["snapshot"] = str(path)
        print(json.dumps(value, indent=2, sort_keys=True))
    inventory.set_defaults(func=inventory_command)
    reserves = commands.add_parser("activate-reserves")
    reserves.add_argument("collection", type=Path)
    reserves.add_argument("--worker-id", type=int, default=0)
    reserves.set_defaults(func=lambda args: print(json.dumps(activate_reserves(args.collection, args.worker_id), indent=2, sort_keys=True)))
    return result


def validate_common_args(args: argparse.Namespace) -> None:
    if getattr(args, "workers", 1) < 1 or getattr(args, "recipes_per_assignment", 1) < 1:
        raise ValueError("workers and recipes-per-assignment must be positive")
    width = getattr(args, "width", 64)
    height = getattr(args, "height", 64)
    if not 64 <= width <= 4096 or not 64 <= height <= 4096:
        raise ValueError("capture width and height must be between 64 and 4096")
    if not 0 <= getattr(args, "evaluation_percent", 0) <= 50:
        raise ValueError("evaluation-percent must be between 0 and 50")


def main() -> int:
    args = parser().parse_args()
    validate_common_args(args)
    args.func(args)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
