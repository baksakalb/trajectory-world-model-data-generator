#!/usr/bin/env python3
"""Focused tests for the immutable combined V2 planner."""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import tempfile
import unittest
from collections import Counter, defaultdict
from fractions import Fraction
from pathlib import Path

import v2_dataset_controller as controller
from v2_mission_catalog import (
    FAMILY_FRAME_SHARES,
    TYPE_FRAME_SHARE,
    mission_types,
    review_recipes,
)


def plan_args(root: Path, **overrides: object) -> argparse.Namespace:
    values: dict[str, object] = {
        "collection": root, "frame_budget": 700_000, "workers": 2,
        "recipes_per_assignment": 8, "episode_seconds": 150,
        "observation_rate": 20, "width": 384, "height": 384,
        "storage_format": "webp_parquet", "webp_effort": 0,
        "seed_start": 200_000, "evaluation_percent": 10, "plan_id": None,
    }
    values.update(overrides)
    return argparse.Namespace(**values)


class V2PlannerTests(unittest.TestCase):
    def test_catalog_has_exactly_the_agreed_sixty_types_and_shares(self) -> None:
        values = mission_types()
        self.assertEqual(len(values), 60)
        self.assertEqual(len({item.slug for item in values}), 60)
        expected = {
            family: int(share / TYPE_FRAME_SHARE)
            for family, share in FAMILY_FRAME_SHARES.items()
        }
        self.assertEqual(Counter(item.family for item in values), Counter(expected))
        self.assertEqual(sum(FAMILY_FRAME_SHARES.values(), Fraction()), Fraction(3, 10))
        self.assertEqual(TYPE_FRAME_SHARE, Fraction(1, 200))

    def test_plan_is_exact_70_30_and_every_type_is_exactly_half_percent(self) -> None:
        recipes = controller.build_recipes(700_000, 150, 20, 200_000, 10)
        distribution = controller.planned_distribution(recipes)
        self.assertEqual(
            distribution["source_frames"],
            {"mission": 210_000, "semi_markov": 490_000},
        )
        self.assertEqual(len(distribution["mission_type_frames"]), 60)
        self.assertEqual(set(distribution["mission_type_frames"].values()), {3_500})

    def test_mandatory_pass_contains_every_type_before_deficit_scheduling(self) -> None:
        recipes = controller.build_recipes(700_000, 150, 20, 200_000, 10)
        mandatory = [item for item in recipes if item["schedule_phase"] == "mandatory_coverage"]
        self.assertEqual(len(mandatory), 61)  # one free-play opening plus 60 types
        mission_mandatory = [item for item in mandatory if item["source"] == "mission"]
        self.assertEqual(len({item["mission_type"] for item in mission_mandatory}), 60)
        first_deficit = next(index for index, item in enumerate(recipes) if item["schedule_phase"] == "frame_deficit")
        self.assertGreaterEqual(first_deficit, 61)
        first_families = {item["family"] for item in mission_mandatory[:6]}
        self.assertEqual(first_families, set(FAMILY_FRAME_SHARES))

    def test_largest_frame_deficit_schedule_is_deterministic(self) -> None:
        first = controller.build_recipes(700_000, 150, 20, 200_000, 10)
        second = controller.build_recipes(700_000, 150, 20, 200_000, 10)
        self.assertEqual(first, second)
        self.assertEqual(len({item["recipe_id"] for item in first}), len(first))
        self.assertEqual(len({item["replay_identity"] for item in first}), len(first))

    def test_calculated_floor_uses_mandatory_durations_and_family_shares(self) -> None:
        rate = 20
        grouped: dict[str, list] = defaultdict(list)
        for item in mission_types():
            grouped[item.family].append(item)
        candidates = [Fraction(150 * rate + 1, 1) / Fraction(7, 10)]
        for family, share in FAMILY_FRAME_SHARES.items():
            mandatory = sum(controller.mission_expected_frames(item, rate) for item in grouped[family])
            candidates.append(Fraction(mandatory, 1) / share)
        raw = max((item.numerator + item.denominator - 1) // item.denominator for item in candidates)
        expected = ((raw + 199) // 200) * 200
        self.assertEqual(controller.minimum_feasible_frame_budget(150, rate), expected)
        self.assertEqual(expected, 32_200)
        with self.assertRaisesRegex(ValueError, "calculated minimum"):
            controller.build_recipes(expected - 200, 150, rate, 1, 10)

    def test_budget_quantum_is_required_for_exact_type_shares(self) -> None:
        with self.assertRaisesRegex(ValueError, "divisible by 200"):
            controller.build_recipes(700_001, 150, 20, 1, 10)

    def test_types_reach_both_splits_when_budget_permits(self) -> None:
        recipes = controller.build_recipes(700_000, 150, 20, 200_000, 10)
        splits: dict[str, set[str]] = defaultdict(set)
        for item in recipes:
            if item["source"] == "mission":
                splits[item["mission_type"]].add(item["split"])
        self.assertEqual(set(splits), {item.slug for item in mission_types()})
        self.assertTrue(all(value == {"train", "evaluation"} for value in splits.values()))

    def test_created_plan_verifies_and_has_no_replacement_fields(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "collection"
            with contextlib.redirect_stdout(io.StringIO()):
                controller.create_plan(plan_args(root))
            result = controller.verify_plan(root)
            self.assertTrue(result["valid"])
            self.assertTrue(result["mandatory_coverage_complete"])
            self.assertTrue(result["no_replacement_fields"])
            text = (root / "plan" / "recipes.jsonl").read_text(encoding="utf-8")
            for forbidden in ("reserve_for", "replacement_for", "candidate_seed"):
                self.assertNotIn(forbidden, text)

    def test_review_builder_has_three_separated_examples_for_every_type(self) -> None:
        values = review_recipes(20)
        self.assertEqual(len(values), 180)
        grouped: dict[str, list[dict]] = defaultdict(list)
        for value in values:
            grouped[value["mission_type"]].append(value)
            self.assertEqual((value["width"], value["height"]), (384, 384))
        self.assertEqual(len(grouped), 60)
        for examples in grouped.values():
            self.assertEqual([item["repetition_index"] for item in examples], [0, 5, 17])
            self.assertEqual(len({
                tuple(item["solution"]["player_spawn"].values()) for item in examples
            }), 3)

    def test_review_plan_command_writes_exactly_180_one_recipe_assignments(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "review"
            args = argparse.Namespace(
                collection=root,
                observation_rate=20,
                workers=4,
            )
            with contextlib.redirect_stdout(io.StringIO()):
                controller.create_review_plan(args)
            recipes = [
                json.loads(line)
                for line in (root / "plan" / "recipes.jsonl").read_text(
                    encoding="utf-8"
                ).splitlines()
                if line.strip()
            ]
            assignments = sorted(
                (root / "assignments").glob("review-assignment-*.json")
            )
            self.assertEqual(len(recipes), 180)
            self.assertEqual(len(assignments), 180)
            self.assertTrue(all(
                len(json.loads(path.read_text(encoding="utf-8"))["recipes"]) == 1
                for path in assignments
            ))


if __name__ == "__main__":
    unittest.main()
