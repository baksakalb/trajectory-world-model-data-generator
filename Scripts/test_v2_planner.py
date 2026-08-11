#!/usr/bin/env python3
"""Tests for the semi-Markov-only V2 planner."""

from __future__ import annotations

import argparse
import contextlib
import io
import tempfile
import unittest
from pathlib import Path

import v2_dataset_controller as controller


def plan_args(root: Path, **overrides: object) -> argparse.Namespace:
    values: dict[str, object] = {
        "collection": root,
        "frame_budget": 700_000,
        "workers": 2,
        "recipes_per_assignment": 8,
        "episode_seconds": 150,
        "observation_rate": 20,
        "width": 384,
        "height": 384,
        "storage_format": "webp_parquet",
        "webp_effort": 0,
        "seed_start": 200_000,
        "evaluation_percent": 10,
        "plan_id": None,
    }
    values.update(overrides)
    return argparse.Namespace(**values)


class V2SemiMarkovPlannerTests(unittest.TestCase):
    def test_plan_contains_only_semi_markov_recipes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "collection"
            with contextlib.redirect_stdout(io.StringIO()):
                controller.create_plan(plan_args(root))
            recipes = controller.read_jsonl(root / "plan" / "recipes.jsonl")
            self.assertTrue(recipes)
            self.assertEqual(
                {(item["mission"], item["source"], item["family"]) for item in recipes},
                {("semi_markov", "semi_markov", "semi_markov")},
            )
            self.assertFalse(any("cell" in item for item in recipes))
            self.assertFalse(any("sequence" in item for item in recipes))
            self.assertTrue(controller.verify_plan(root)["valid"])

    def test_planned_credit_matches_exact_requested_budget(self) -> None:
        recipes = controller.build_recipes(123_457, 150, 20, 1_000, 10)
        self.assertEqual(
            sum(int(item["planned_credited_frames"]) for item in recipes),
            123_457,
        )

    def test_episode_duration_is_restricted_to_two_to_three_minutes(self) -> None:
        for seconds in (119, 181):
            with self.assertRaisesRegex(ValueError, "120-180"):
                controller.build_recipes(10_000, seconds, 20, 1_000, 10)
        for seconds in (120, 150, 180):
            self.assertTrue(controller.build_recipes(10_000, seconds, 20, 1_000, 10))

    def test_no_reserve_or_replacement_path_exists(self) -> None:
        self.assertFalse(hasattr(controller, "activate_reserves"))
        self.assertNotIn("activate-reserves", controller.parser().format_help())


if __name__ == "__main__":
    unittest.main()
