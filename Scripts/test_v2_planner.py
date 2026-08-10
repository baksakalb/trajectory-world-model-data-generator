#!/usr/bin/env python3
"""Contract tests for the deterministic combined-V2 planner and catalog."""

from __future__ import annotations

import argparse
import json
import tempfile
import unittest
from collections import Counter
from pathlib import Path

import v2_dataset_controller as controller
from v2_catalog import (
    BASE_FAMILY_COUNTS,
    SEQUENCE_TEMPLATES,
    audit_slots,
    base_cells,
    catalog_fingerprint,
    sequence_fingerprint,
)


def args(root: Path, budget: int, workers: int = 1) -> argparse.Namespace:
    return argparse.Namespace(
        collection=root,
        frame_budget=budget,
        workers=workers,
        recipes_per_assignment=32,
        episode_seconds=20,
        observation_rate=20,
        width=64,
        height=64,
        storage_format="webp_parquet",
        webp_effort=0,
        seed_start=200000,
        evaluation_percent=10,
        duration_calibration=controller.DEFAULT_CALIBRATION,
        allow_infeasible_diagnostic=False,
        allow_unqualified_calibration=True,
        plan_id=None,
    )


def read_jsonl(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]


def qualification_args(root: Path, **overrides: object) -> argparse.Namespace:
    values: dict[str, object] = {
        "collection": root, "cell_id": [], "sequence_template_id": [],
        "all_base_cells": False, "all_sequences": False,
        "audit_cells": False,
        "workers": 2, "recipes_per_assignment": 1,
        "episode_seconds": 20, "observation_rate": 20,
        "width": 64, "height": 64, "storage_format": "webp_parquet",
        "webp_effort": 0, "seed_start": 910000,
        "duration_calibration": controller.DEFAULT_CALIBRATION, "plan_id": None,
    }
    values.update(overrides)
    return argparse.Namespace(**values)


class V2CatalogTests(unittest.TestCase):
    def test_catalog_counts_ids_and_fingerprints_are_stable(self) -> None:
        cells = base_cells()
        self.assertEqual(Counter(cell["family"] for cell in cells), Counter(BASE_FAMILY_COUNTS))
        self.assertEqual(len(cells), 312)
        self.assertEqual(len({cell["cell_id"] for cell in cells}), 312)
        self.assertEqual(catalog_fingerprint(cells), "61fc9d8f6fb886f77a443bd8aa8cc10810d13560c2a40057bfa33996dfccf151")
        self.assertEqual(sequence_fingerprint(), "8b2201b6d9ca8aa75584e133ee3128dfbbd910cc808d10642fc71d2e162793f1")

    def test_every_sequence_and_visual_audit_slot_is_unique(self) -> None:
        self.assertEqual(len(SEQUENCE_TEMPLATES), 8)
        self.assertEqual(len({item.template_id for item in SEQUENCE_TEMPLATES}), 8)
        slots = audit_slots()
        self.assertEqual(len(slots), 99)
        self.assertEqual(len({item["slot_id"] for item in slots}), 99)
        self.assertEqual(Counter(item["family"] for item in slots), Counter({
            "semi_markov": 12, "trajectory_view": 12,
            "solid_object": 15, "wall_corner": 8, "floor_observe": 6,
            "ramp": 8, "hoop": 18, "temporal": 8,
            "out_of_bounds": 4, "sequence": 8,
        }))

    def test_solid_variations_cover_all_distance_and_arc_bands(self) -> None:
        solids = [cell for cell in base_cells() if cell["family"] == "solid_object"]
        grouped: dict[str, list[dict]] = {}
        for cell in solids:
            grouped.setdefault(cell["target"], []).append(cell)
        for values in grouped.values():
            self.assertEqual({cell["distance_band"] for cell in values}, {"near", "medium", "far"})
            self.assertEqual({cell["arc_band"] for cell in values}, {"low", "medium"})
            self.assertEqual({cell["interaction_mode"] for cell in values}, {"contact"})
            self.assertEqual({cell["target_region"] for cell in values}, {
                "center", "upper", "lower", "left_edge", "right_edge",
            })

    def test_misses_and_ambiguous_outcomes_are_not_mission_cells(self) -> None:
        payload = json.dumps(base_cells(), sort_keys=True)
        for removed in ("near_miss", "side_miss", "open_path", "corner_or_miss"):
            self.assertNotIn(removed, payload)


class V2PlannerTests(unittest.TestCase):

    def test_persistent_semi_markov_duration_bands_are_two_to_three_minutes(self) -> None:
        calibration = controller.load_calibration()
        expected = {
            "short": 120 * 20 + 1,
            "medium": 140 * 20 + 1,
            "long": 160 * 20 + 1,
            "very_long": 180 * 20 + 1,
        }
        cells = [item for item in base_cells() if item["family"] == "semi_markov"]
        for cell in cells:
            self.assertEqual(
                controller.expected_frames(calibration, "semi_markov", cell=cell),
                expected[cell["hold_band"]],
            )

    def test_trajectory_view_keeps_its_named_acquisition_gesture(self) -> None:
        cell = next(
            item for item in base_cells()
            if item["family"] == "trajectory_view"
            and item["interaction_mode"] == "pitch_adjust"
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "qualification"
            controller.create_qualification_plan(qualification_args(
                root, cell_id=[cell["cell_id"]], workers=1,
            ))
            recipe = read_jsonl(root / "plan" / "recipes.jsonl")[0]
            self.assertEqual(recipe["aim_acquisition_profile"], "pitch_adjust")

    def test_controller_rejects_capture_sizes_the_runtime_would_clamp(self) -> None:
        invalid = qualification_args(Path("unused"), width=32, height=64)
        with self.assertRaisesRegex(ValueError, "between 64 and 4096"):
            controller.validate_common_args(invalid)

    def test_qualification_plan_selects_exact_cells_and_temporal_templates(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "qualification"
            selected_cell = base_cells()[0]["cell_id"]
            controller.create_qualification_plan(qualification_args(
                root, cell_id=[selected_cell], sequence_template_id=["SQ08"],
            ))
            recipes = read_jsonl(root / "plan" / "recipes.jsonl")
            self.assertEqual(len(recipes), 2)
            self.assertTrue(all(recipe["qualification_only"] for recipe in recipes))
            self.assertEqual(recipes[0]["cell_id"], selected_cell)
            self.assertEqual(recipes[1]["sequence_template_id"], "SQ08")
            self.assertEqual(len(recipes[1]["sequence"]["steps"]), 3)
            plan = json.loads((root / "plan" / "collection-plan.json").read_text(encoding="utf-8"))
            self.assertTrue(plan["qualification_only"])
            self.assertEqual(plan["generator_source_sha256"], controller.generator_source_fingerprint())
            self.assertEqual(plan["selected_base_cell_count"], 1)
            self.assertEqual(plan["selected_sequence_template_count"], 1)

    def test_qualification_plan_can_cover_the_full_catalog(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "qualification"
            controller.create_qualification_plan(qualification_args(
                root, all_base_cells=True, all_sequences=True,
                recipes_per_assignment=64,
            ))
            recipes = read_jsonl(root / "plan" / "recipes.jsonl")
            self.assertEqual(len(recipes), 312 + len(SEQUENCE_TEMPLATES))
            self.assertEqual(len(list((root / "assignments").glob("*.json"))), 5)

    def test_qualification_plan_can_select_the_frozen_audit_cells(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "qualification"
            controller.create_qualification_plan(qualification_args(
                root, audit_cells=True, recipes_per_assignment=128,
            ))
            recipes = read_jsonl(root / "plan" / "recipes.jsonl")
            self.assertEqual(len(recipes), 99)
            self.assertEqual(
                len([item for item in recipes if item.get("sequence_template_id")]),
                8,
            )
            self.assertEqual(
                len([item for item in recipes if not item.get("sequence_template_id")]),
                91,
            )

    def test_sequence_recipes_materialize_every_executable_step(self) -> None:
        calibration = controller.load_calibration()
        recipes = controller.build_schedule(1_000_000, calibration, 200_000, 10, False)
        sequence_recipes = [recipe for recipe in recipes if recipe.get("sequence_template_id")]
        self.assertEqual(len(sequence_recipes), len(SEQUENCE_TEMPLATES))
        for recipe in sequence_recipes:
            steps = recipe["sequence"]["steps"]
            self.assertEqual(len(steps), recipe["sequence"]["grenade_count"])
            self.assertEqual(
                [step["cell_id"] for step in steps],
                recipe["sequence_base_cell_ids"],
            )
            self.assertEqual(list(range(len(steps))), [step["step_index"] for step in steps])
            self.assertTrue(all(step["cell"]["family"] for step in steps))
    def test_exact_allocation_complete_coverage_and_worker_independence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = root / "one"
            second = root / "four"
            controller.create_plan(args(first, 1_000_000, 1))
            controller.create_plan(args(second, 1_000_000, 4))
            report = controller.verify_plan(first)
            self.assertTrue(report["valid"])
            distribution = report["planned_distribution"]
            self.assertEqual(distribution["source_frames"], {
                "semi_markov": 700000, "mission": 300000,
            })
            self.assertEqual(distribution["family_frames"], {
                "semi_markov": 700000,
                "trajectory_view": 60000, "solid_object": 60000,
                "wall_corner": 40000, "floor_observe": 10000,
                "ramp": 40000, "hoop": 50000, "temporal": 30000,
                "out_of_bounds": 10000,
            })
            frozen_plan = json.loads((first / "plan" / "collection-plan.json").read_text(encoding="utf-8"))
            self.assertEqual(frozen_plan["generator_source_sha256"], controller.generator_source_fingerprint())
            a = [recipe["replay_identity"] for recipe in read_jsonl(first / "plan" / "recipes.jsonl") if recipe["active"]]
            b = [recipe["replay_identity"] for recipe in read_jsonl(second / "plan" / "recipes.jsonl") if recipe["active"]]
            self.assertEqual(a, b)

    def test_larger_budget_preserves_active_recipe_prefix(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            floor = controller.minimum_feasible_frame_budget()
            small = root / "small"
            large = root / "large"
            controller.create_plan(args(small, floor))
            controller.create_plan(args(large, 1_000_000))
            a = [recipe["replay_identity"] for recipe in read_jsonl(small / "plan" / "recipes.jsonl") if recipe["active"]]
            b = [recipe["replay_identity"] for recipe in read_jsonl(large / "plan" / "recipes.jsonl") if recipe["active"]]
            self.assertEqual(a, b[: len(a)])

    def test_production_quantum_and_qualification_gate(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stale = args(root / "stale-contract", 1_000_000)
            stale.allow_unqualified_calibration = False
            with self.assertRaisesRegex(ValueError, "qualified local duration calibration"):
                controller.create_plan(stale)
            invalid = args(root / "invalid", 1_000_001)
            with self.assertRaisesRegex(ValueError, "divisible by 100"):
                controller.create_plan(invalid)
            calibration = json.loads(controller.DEFAULT_CALIBRATION.read_text(encoding="utf-8"))
            calibration["qualified"] = False
            unqualified_path = root / "unqualified-calibration.json"
            unqualified_path.write_text(json.dumps(calibration), encoding="utf-8")
            unqualified = args(root / "unqualified", 1_000_000)
            unqualified.duration_calibration = unqualified_path
            unqualified.allow_unqualified_calibration = False
            with self.assertRaisesRegex(ValueError, "qualified local duration calibration"):
                controller.create_plan(unqualified)

    def test_train_evaluation_replay_identity_is_disjoint(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "plan"
            controller.create_plan(args(root, 1_000_000))
            recipes = [item for item in read_jsonl(root / "plan" / "recipes.jsonl") if item["active"]]
            train = {item["replay_identity"] for item in recipes if item["split"] == "train"}
            evaluation = {item["replay_identity"] for item in recipes if item["split"] == "evaluation"}
            self.assertTrue(train)
            self.assertTrue(evaluation)
            self.assertFalse(train & evaluation)

    def test_certified_missions_have_no_seed_mining_reserves(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "plan"
            controller.create_plan(args(root, controller.minimum_feasible_frame_budget()))
            recipes = read_jsonl(root / "plan" / "recipes.jsonl")
            self.assertFalse([item for item in recipes if item["reserve_for"]])

    def test_semantic_failure_cannot_activate_seed_mining_reserves(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "plan"
            controller.create_plan(args(root, controller.minimum_feasible_frame_budget()))
            primary = next(
                item for item in read_jsonl(root / "plan" / "recipes.jsonl")
                if item["active"]
            )
            assignment = next(
                json.loads(path.read_text(encoding="utf-8"))
                for path in (root / "assignments").glob("*.json")
                if any(item["recipe_id"] == primary["recipe_id"]
                       for item in json.loads(path.read_text(encoding="utf-8"))["recipes"])
            )
            controller.write_new_json(
                root / "results" / f"{assignment['assignment_id']}--attempt-000.json",
                {
                    "technical_result": "validated",
                    "assignment_id": assignment["assignment_id"],
                    "produced_observation_frames": 10,
                    "semantic_failure_recipe_ids": [primary["recipe_id"]],
                    "resolved_recipe_ids": [primary["recipe_id"]],
                    "credited_cells": [],
                },
            )
            inventory = controller.build_inventory(root)
            self.assertEqual(inventory["semantic_failure_recipe_ids"], [primary["recipe_id"]])
            first = controller.activate_reserves(root, 7)
            self.assertEqual(first["activated"], 0)
            self.assertIn("no unresolved semantic failure", first["reason"])
            reserve_assignments = [
                json.loads(path.read_text(encoding="utf-8"))
                for path in (root / "assignments").glob("assignment-v2reserve-*.json")
            ]
            self.assertFalse(reserve_assignments)


if __name__ == "__main__":
    unittest.main()
