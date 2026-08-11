#!/usr/bin/env python3
"""Guards for shared V1/V2 persistent semi-Markov behavior."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

from v2_dataset_controller import CONTRACT_VERSION


ROOT = Path(__file__).resolve().parent.parent
GENERATOR = ROOT / "Source/he_grenade_game/DataGenerator/CurriculumDataGenerator.cpp"
HEADER = ROOT / "Source/he_grenade_game/DataGenerator/CurriculumDataGenerator.h"
CONTROLLER = ROOT / "Scripts/v2_dataset_controller.py"


class SharedSemiMarkovRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = GENERATOR.read_text(encoding="utf-8")
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.controller = CONTROLLER.read_text(encoding="utf-8")

    def test_v1_and_v2_call_one_selector_with_only_a_capability_flag(self) -> None:
        prepare = self.source.split(
            "void ACurriculumDataGenerator::PrepareNextAction", 1
        )[1].split("void ACurriculumDataGenerator::ResetStageState", 1)[0]
        self.assertIn("SelectPersistentSemiMarkovAction(", prepare)
        self.assertIn(
            "CurriculumStage == ECurriculumStage::TrajectoryThrowV2", prepare
        )
        self.assertEqual(prepare.count("SelectPersistentSemiMarkovAction("), 1)
        selector = self.source.split(
            "uint16 ACurriculumDataGenerator::SelectPersistentSemiMarkovAction", 1
        )[1].split("int32 ACurriculumDataGenerator::SelectPersistentSemiMarkovHoldSteps", 1)[0]
        self.assertIn("const bool bAllowThrows", selector)
        self.assertIn("MovementMask | CurriculumAction::CameraMask", selector)
        self.assertNotIn("CurriculumStage", selector)
        self.assertIn("GrenadeActionRandom", selector)

    def test_shared_state_bias_uses_realized_observations(self) -> None:
        capture = self.source.split(
            "bool ACurriculumDataGenerator::CaptureObservation", 1
        )[1].split("void ACurriculumDataGenerator::AppendTransition", 1)[0]
        self.assertIn("++PersistentPositionBinFrames", capture)
        self.assertIn("++PersistentViewBinFrames", capture)

    def test_v2_mission_runtime_and_catalog_are_absent(self) -> None:
        forbidden = (
            "SelectV2ProductionAction",
            "ConfigureV2RecipeSpawn",
            "ConfigureV2SequenceStepAim",
            "ValidateV2RecipeSemantics",
            "ValidateV2ThrowSemantics",
            "FV2SequenceStep",
            "CurrentV2SequenceTemplateId",
            "semantic_success",
        )
        for token in forbidden:
            self.assertNotIn(token, self.source + self.header + self.controller)
        self.assertNotRegex(self.controller, re.compile(r"mission.*30", re.IGNORECASE))

    def test_every_throw_uses_the_one_canonical_speed_and_config(self) -> None:
        build = self.source.split(
            "bool ACurriculumDataGenerator::BuildLaunchState", 1
        )[1].split("int32 ACurriculumDataGenerator::AcceptThrow", 1)[0]
        self.assertIn("Forward * CurriculumThrowSpeedCmPerSecond", build)
        accept = self.source.split(
            "int32 ACurriculumDataGenerator::AcceptThrow", 1
        )[1].split("void ACurriculumDataGenerator::AdvanceGrenades", 1)[0]
        self.assertIn("Grenade.SimConfig = GrenadeSimConfig", accept)
        self.assertNotRegex(
            accept,
            r"Grenade\.SimConfig\.(?:BounceRestitution|TangentialDamping|"
            r"StopSpeedCmPerSec|RestSpeedCmPerSec|MaxBounces)\s*=",
        )

    def test_contract_is_semi_markov_only(self) -> None:
        self.assertEqual(CONTRACT_VERSION, "shared-persistent-semi-markov-1")
        self.assertIn(CONTRACT_VERSION, self.source)


if __name__ == "__main__":
    unittest.main()
