#!/usr/bin/env python3
"""Guards for V1 preservation and canonical shared V1/V2 behavior."""

from __future__ import annotations

import hashlib
import re
import unittest
from pathlib import Path

from v2_dataset_controller import CONTRACT_VERSION


ROOT = Path(__file__).resolve().parent.parent
GENERATOR = ROOT / "Source/he_grenade_game/DataGenerator/CurriculumDataGenerator.cpp"
HEADER = ROOT / "Source/he_grenade_game/DataGenerator/CurriculumDataGenerator.h"
CONTROLLER = ROOT / "Scripts/v2_dataset_controller.py"
V1_CONTROLLER = ROOT / "Scripts/dataset_controller.py"

V1_FUNCTION_HASHES = {
    "SelectCoverageGuidedAction": "6a700435167d0e73b8eff6f77bfff808d3cd7ee48c8cfa113f4f870a109792c4",
    "ConfigureObjectViewMission": "618a9f9f737a8915d68a780c7fd3773671257602e10b46ae7783f11fc5fbde8d",
    "ConfigureContactRecoveryMission": "67096c47d079791f050f4c8cfcf69f23ba2285cffa72e9d99c1ce1441e1204f1",
    "ConfigureRampMission": "8c8d735d51cd0361470712323bf4e8eaca806b3b256f5efaf240ce2296784600",
    "ConfigureHoopMission": "71b0656f3cc67a90199790413a9c58606dd21e8a48d0a097ae971e795aaac6c1",
    "GetCoverageMissionSpawn": "04809fcd1adc52bb938c3f7842ef2f21c45399f9efe9db9a4c0433d09e552e92",
}
V1_CONTROLLER_HASH = "50259ffa2bbdf0dca84307ffc8ffda11f86f1d9bf1a354df14736203095dbf07"


def function_text(source: str, name: str) -> str:
    marker = f"ACurriculumDataGenerator::{name}"
    marker_index = source.index(marker)
    start = source.rfind("\n", 0, marker_index) + 1
    brace = source.index("{", marker_index)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated function {name}")


class SharedCanonicalRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = GENERATOR.read_text(encoding="utf-8")
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.controller = CONTROLLER.read_text(encoding="utf-8")

    def test_v1_and_v2_call_one_shared_selector_with_capability_flag(self) -> None:
        prepare = self.source.split("void ACurriculumDataGenerator::PrepareNextAction", 1)[1].split(
            "void ACurriculumDataGenerator::ResetStageState", 1
        )[0]
        self.assertIn("SelectPersistentSemiMarkovAction(", prepare)
        self.assertIn("CurriculumStage == ECurriculumStage::TrajectoryThrowV2", prepare)
        self.assertEqual(prepare.count("SelectPersistentSemiMarkovAction("), 1)
        selector = self.source.split(
            "uint16 ACurriculumDataGenerator::SelectPersistentSemiMarkovAction", 1
        )[1].split("int32 ACurriculumDataGenerator::SelectPersistentSemiMarkovHoldSteps", 1)[0]
        self.assertIn("const bool bAllowThrows", selector)
        self.assertIn("MovementMask | CurriculumAction::CameraMask", selector)
        self.assertNotIn("CurriculumStage", selector)
        self.assertIn("GrenadeActionRandom", selector)

    def test_v1_guided_planner_and_mission_functions_are_byte_preserved(self) -> None:
        normalized_controller = V1_CONTROLLER.read_text(encoding="utf-8").encode()
        self.assertEqual(hashlib.sha256(normalized_controller).hexdigest(), V1_CONTROLLER_HASH)
        for name, expected in V1_FUNCTION_HASHES.items():
            actual = hashlib.sha256(function_text(self.source, name).encode()).hexdigest()
            self.assertEqual(actual, expected, name)

    def test_v1_is_still_limited_to_movement_and_camera(self) -> None:
        apply = function_text(self.source, "ApplyAction")
        self.assertIn("CurrentActionMask &= ~(CurriculumAction::Q | CurriculumAction::E)", apply)

    def test_every_throw_and_preview_use_one_speed_and_config(self) -> None:
        build = function_text(self.source, "BuildLaunchState")
        self.assertIn("Forward * CurriculumThrowSpeedCmPerSecond", build)
        accept = function_text(self.source, "AcceptThrow")
        self.assertIn("Grenade.SimConfig = GrenadeSimConfig", accept)
        self.assertIn("CurrentV2CertifiedLaunchPosition", accept)
        preview = function_text(self.source, "DrawTrajectoryOverlay")
        self.assertIn("GrenadeSimConfig", preview)
        certification = function_text(self.source, "CertifyV2MissionConstruction")
        self.assertIn("FGrenadeSim::Step", certification)
        self.assertIn("GrenadeSimConfig", certification)
        construction = function_text(self.source, "ConfigureV2MissionSpawn")
        self.assertIn("false,", construction)  # low ballistic branch
        self.assertNotRegex(
            accept,
            r"Grenade\.SimConfig\.(?:BounceRestitution|TangentialDamping|"
            r"StopSpeedCmPerSec|RestSpeedCmPerSec|MaxBounces)\s*=",
        )

    def test_combined_contract_and_no_replacement_system(self) -> None:
        self.assertEqual(
            CONTRACT_VERSION,
            "shared-persistent-semi-markov-1+certified-sixty-missions-1",
        )
        self.assertIn("SelectV2MissionAction", self.source)
        self.assertIn("CertifyV2MissionConstruction", self.source)
        for token in (
            "activate_reserves", "replacement_seed", "candidate_seed",
            "reserve_for", "semantic_success_search",
        ):
            self.assertNotIn(token, self.controller)

    def test_semi_markov_credit_has_no_behavioral_gate(self) -> None:
        worker = (ROOT / "Scripts/dataset_worker.py").read_text(encoding="utf-8")
        self.assertIn('source == "semi_markov"', worker)
        forbidden = re.compile(
            r"(?:camera|eye_level|wall_time|map_coverage|throw_count).*threshold",
            re.IGNORECASE,
        )
        self.assertNotRegex(self.controller + worker, forbidden)


if __name__ == "__main__":
    unittest.main()
