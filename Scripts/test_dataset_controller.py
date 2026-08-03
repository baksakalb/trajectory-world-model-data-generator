#!/usr/bin/env python3
"""Fast controller contract tests; does not launch Unreal."""

from __future__ import annotations

import contextlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
from argparse import Namespace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dataset_controller as controller
import dataset_worker as worker


def plan_args(
    root: Path, *, budget: int = 25000, workers: int = 1, seed_start: int = 1000
) -> Namespace:
    return Namespace(
        collection=root,
        frame_budget=budget,
        workers=workers,
        recipes_per_assignment=32,
        episode_seconds=10,
        observation_rate=20,
        width=64,
        height=64,
        storage_format="webp_parquet",
        webp_effort=0,
        seed_start=seed_start,
        split="train",
        plan_id=None,
    )


def create(
    root: Path, *, budget: int = 25000, workers: int = 1, seed_start: int = 1000
) -> None:
    with contextlib.redirect_stdout(io.StringIO()):
        controller.create_plan(
            plan_args(root, budget=budget, workers=workers, seed_start=seed_start)
        )


class ControllerContractTests(unittest.TestCase):
    def test_complete_catalog_and_worker_independence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            one = base / "one"
            four = base / "four"
            create(one, workers=1)
            create(four, workers=4)
            recipes_one = controller.read_jsonl(one / "plan" / "recipes.jsonl")
            recipes_four = controller.read_jsonl(four / "plan" / "recipes.jsonl")
            active_one = [recipe for recipe in recipes_one if recipe["active"]]
            active_four = [recipe for recipe in recipes_four if recipe["active"]]
            self.assertEqual(active_one, active_four)
            self.assertEqual(len(active_one), sum(controller.MISSION_COUNTS.values()))
            self.assertEqual(
                len({(recipe["mission"], recipe["scenario_index"]) for recipe in active_one}),
                sum(controller.MISSION_COUNTS.values()),
            )

    def test_larger_budget_extends_same_canonical_prefix(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            small = base / "small"
            large = base / "large"
            create(small, budget=25000)
            create(large, budget=300000)
            fields = (
                "recipe_index",
                "episode_index",
                "mission",
                "scenario_index",
                "continuous_sample_ordinal",
                "refinement_level",
                "repetition_index",
            )
            small_recipes = [recipe for recipe in controller.read_jsonl(small / "plan" / "recipes.jsonl") if recipe["active"]]
            large_recipes = [recipe for recipe in controller.read_jsonl(large / "plan" / "recipes.jsonl") if recipe["active"]]
            self.assertGreater(len(large_recipes), len(small_recipes))
            self.assertEqual(
                [tuple(recipe[field] for field in fields) for recipe in small_recipes],
                [tuple(recipe[field] for field in fields) for recipe in large_recipes[: len(small_recipes)]],
            )

    def test_infeasible_budget_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            minimum = (
                controller.MISSION_COUNTS["semi_markov"] * 201
                + sum(
                    controller.MISSION_COUNTS[mission]
                    for mission in controller.GUIDED_MISSIONS
                )
                * 17
            )
            with self.assertRaisesRegex(ValueError, "infeasible"):
                create(Path(temporary) / "bad", budget=minimum - 1)

    def test_seed_changes_immutable_plan_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            first = base / "first"
            second = base / "second"
            create(first, seed_start=1000)
            create(second, seed_start=1001)
            first_plan = controller.read_json(first / "plan" / "collection-plan.json")
            second_plan = controller.read_json(second / "plan" / "collection-plan.json")
            self.assertNotEqual(first_plan["plan_id"], second_plan["plan_id"])

    def test_duplicate_validated_result_is_ignored(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "collection"
            create(root)
            assignment = controller.read_json(sorted((root / "assignments").glob("*.json"))[0])
            result = {
                "technical_result": "validated",
                "assignment_id": assignment["assignment_id"],
                "accepted_observation_frames": 10,
                "resolved_recipe_ids": [recipe["recipe_id"] for recipe in assignment["recipes"]],
                "semantic_failure_recipe_ids": [],
                "credited_cells": [
                    {"mission": recipe["mission"], "scenario_index": recipe["scenario_index"]}
                    for recipe in assignment["recipes"]
                ],
            }
            controller.write_new_json(root / "results" / "a.json", result)
            controller.write_new_json(root / "results" / "b.json", result)
            inventory = controller.build_inventory(root)
            self.assertEqual(inventory["successful_assignment_count"], 1)
            self.assertEqual(inventory["accepted_observation_frames"], 10)
            self.assertEqual(inventory["duplicate_validated_results_ignored"], ["b.json"])

    def test_stop_file_prevents_new_assignment(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "collection"
            create(root)
            (root / "STOP").write_text("stop\n", encoding="utf-8")
            worker = Path(__file__).with_name("dataset_worker.py")
            completed = subprocess.run(
                [
                    sys.executable,
                    str(worker),
                    str(root),
                    "--executable",
                    str(root / "not-used.exe"),
                    "--worker-id",
                    "0",
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertFalse((root / "claims").exists())

    def test_orphaned_claim_advances_retry_and_is_reconstructed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "collection"
            create(root)
            assignment = controller.read_json(sorted((root / "assignments").glob("*.json"))[0])
            assignment_id = assignment["assignment_id"]
            self.assertIsNotNone(worker.claim_attempt(root, assignment_id, 0, "killed-worker"))
            self.assertEqual(worker.next_attempt(root, assignment_id), 1)
            inventory = controller.build_inventory(root)
            self.assertEqual(
                inventory["interrupted_claims_without_result"],
                [
                    {
                        "assignment_id": assignment_id,
                        "attempt_id": "attempt-000",
                        "executor_id": "killed-worker",
                        "claimed_utc": inventory["interrupted_claims_without_result"][0]["claimed_utc"],
                    }
                ],
            )


if __name__ == "__main__":
    unittest.main()
