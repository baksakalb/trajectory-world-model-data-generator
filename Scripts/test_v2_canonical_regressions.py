#!/usr/bin/env python3
"""Focused guards for the human-audit V2 failure classes."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

from v2_catalog import base_cells
from v2_dataset_controller import CONTRACT_VERSION, SOURCE_FRAME_SHARES


ROOT = Path(__file__).resolve().parent.parent
GENERATOR = ROOT / "Source/he_grenade_game/DataGenerator/CurriculumDataGenerator.cpp"
VALIDATOR = (ROOT / "Scripts/review_dataset.py").read_text(encoding="utf-8")

# Stable audit identities retained even though their rejected recipes are not
# reused as production cells.
FAILED_SEEDS = {
    "post_throw_backward": 920013,
    "post_throw_strafe_left": 920014,
    "post_throw_diagonal": 920016,
    "scene_establish_rectangle": 920019,
    "glance_dwell_left": 920047,
    "glance_dwell_right": 920048,
    "ordinary_out_of_bounds": 920060,
    "lateral_ramp_cross_over": 920106,
}
POSITIVE_CONTROL_SEEDS = {"post_throw_strafe_right": 920015}


class CanonicalV2RegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = GENERATOR.read_text(encoding="utf-8")

    def test_runtime_metadata_matches_controller_contract(self) -> None:
        self.assertIn(CONTRACT_VERSION, self.source)

    def test_semi_markov_preferences_are_not_per_episode_failures(self) -> None:
        self.assertNotIn("central_attention <", VALIDATOR)
        self.assertNotIn("wall_zone_frames >", VALIDATOR)
        self.assertNotIn("len(position_bins) <", VALIDATOR)

    def test_named_human_audit_cases_remain_frozen(self) -> None:
        self.assertEqual(len(set(FAILED_SEEDS.values())), len(FAILED_SEEDS))
        self.assertEqual(POSITIVE_CONTROL_SEEDS["post_throw_strafe_right"], 920015)

    def test_every_throw_uses_the_one_canonical_speed(self) -> None:
        build = self.source.split(
            "bool ACurriculumDataGenerator::BuildLaunchState", 1
        )[1].split("int32 ACurriculumDataGenerator::AcceptThrow", 1)[0]
        self.assertIn("Forward * CurriculumThrowSpeedCmPerSecond", build)
        self.assertNotRegex(build, r"GetV2(?:Sequence)?ThrowSpeed")

    def test_no_recipe_mutates_grenade_physics(self) -> None:
        accept = self.source.split(
            "int32 ACurriculumDataGenerator::AcceptThrow", 1
        )[1].split("void ACurriculumDataGenerator::AdvanceGrenades", 1)[0]
        self.assertNotRegex(
            accept,
            r"Grenade\.SimConfig\.(?:BounceRestitution|TangentialDamping|"
            r"StopSpeedCmPerSec|RestSpeedCmPerSec|MaxBounces)\s*=",
        )
        self.assertIn("Grenade.SimConfig = GrenadeSimConfig", accept)
        self.assertIn("GrenadeSimConfig.MaxBounces = 0", self.source)

    def test_preview_parity_is_computed_not_hardcoded(self) -> None:
        self.assertNotIn('"preview_to_realized_flight_parity":true', self.source)
        self.assertIn("PredictedBounceCount == Grenade.State.BounceCount", self.source)
        self.assertIn("PredictedExitDirection == Grenade.ArenaExitDirection", self.source)

    def test_human_dwell_scene_establishment_and_event_camera_are_enforced(self) -> None:
        self.assertIn("HoldStepsRemaining = SelectV2PersistentHoldSteps()", self.source)
        self.assertIn("const int32 EstablishSteps = FMath::Max(1, ObservationRate)", self.source)
        self.assertIn(
            "SelectStableV2CameraBitsToward(Grenades.Last().State.Position)",
            self.source,
        )
        self.assertIn("CurrentV2CameraNeutralStepsRemaining", self.source)
        self.assertIn("MaximumSignedDisplacementCm < 100.0f", self.source)

    def test_oob_and_lateral_crossing_have_geometric_gates(self) -> None:
        families = {cell["family"] for cell in base_cells()}
        self.assertIn("out_of_bounds", families)
        self.assertIn("Grenade.ArenaExitDirection == CurrentV2Wall", self.source)
        self.assertRegex(self.source, re.escape("ThrowPlayerPosition.Y * Grenade.State.Position.Y < 0.0f"))

    def test_persistent_semi_markov_source_mix_is_explicit(self) -> None:
        self.assertEqual(
            {key: float(value) for key, value in SOURCE_FRAME_SHARES.items()},
            {"semi_markov": 0.70, "mission": 0.30},
        )
        self.assertIn("canonical-physics", CONTRACT_VERSION)
        self.assertIn("persistent-semi-markov", CONTRACT_VERSION)
        self.assertNotIn('CurrentV2Source == TEXT("long_play")', self.source)
        self.assertIn('? 180', self.source)
        self.assertIn('CurriculumAction::CanonicalMask', self.source)
        self.assertIn("CurrentV2PositionBinFrames", self.source)
        self.assertIn("CurrentV2ViewBinFrames", self.source)
        self.assertIn("ApplyV2PostThrowAttention", self.source)
        self.assertIn("AttentionRoll < 40", self.source)

    def test_v1_uses_persistent_policy_without_grenade_controls(self) -> None:
        route = self.source.split("void ACurriculumDataGenerator::PrepareNextAction", 1)[1]
        self.assertIn("CurriculumStage == ECurriculumStage::Movement", route)
        self.assertIn("SelectV2SemiMarkovAction(false)", route)
        selector = self.source.split(
            "uint16 ACurriculumDataGenerator::SelectV2SemiMarkovAction", 1
        )[1].split("int32 ACurriculumDataGenerator::SelectV2PersistentHoldSteps", 1)[0]
        self.assertIn("const bool bMovementOnly", selector)
        self.assertIn("CurriculumAction::MovementMask | CurriculumAction::CameraMask", selector)


if __name__ == "__main__":
    unittest.main()
