#!/usr/bin/env python3
"""Focused tests for the semi-Markov-only V2 validator."""

from __future__ import annotations

import copy
import unittest

from Scripts.finalize_production_dataset import V2_THROW
from Scripts.review_dataset import DatasetValidationError, validate_v2_runtime_contract


def frame(
    index: int, *, q: bool, cooldown: int,
    grenades: list[dict] | None = None, mission_type: str | None = None,
) -> dict:
    grenades = grenades or []
    resting = sum(bool(item["resting"]) for item in grenades)
    return {
        "frame_index": index,
        "q_visibility": q,
        "trajectory_visible": q,
        "aim_lock_active": q,
        "cooldown_remaining_steps": cooldown,
        "grenades": grenades,
        "flying_grenade_count": len(grenades) - resting,
        "resting_grenade_count": resting,
        "visible_grenade_count": len(grenades),
        "total_grenade_count": len(grenades),
        "v2_mission_type": mission_type,
        "v2_mission_region_visible": mission_type is not None,
        "v2_preview_region_visible": mission_type is not None,
    }


class V2RuntimeContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.dataset = {
            "schema_version": "trajectory_throw_v2-preflight-12",
            "observation_rate_hz": 20,
        }
        self.frames = [
            frame(0, q=False, cooldown=0),
            frame(1, q=True, cooldown=0),
            frame(2, q=True, cooldown=40, grenades=[{"id": 0, "resting": False}]),
        ]
        self.transitions = [
            {
                "action_mask": 1 << 8,
                "q_rising_edge": True,
                "q_falling_edge": False,
                "e_request_edge": False,
                "e_accepted": False,
                "accepted_throw_grenade_id": None,
                "forward_axis": 0.0,
                "right_axis": 0.0,
                "cooldown_before_steps": 0,
            },
            {
                "action_mask": (1 << 8) | (1 << 9),
                "q_rising_edge": False,
                "q_falling_edge": False,
                "e_request_edge": True,
                "e_accepted": True,
                "accepted_throw_grenade_id": 0,
                "forward_axis": 0.0,
                "right_axis": 0.0,
                "cooldown_before_steps": 0,
            },
        ]

    def test_valid_q_then_e_sequence(self) -> None:
        validate_v2_runtime_contract(
            self.dataset, "episode", self.frames, self.transitions
        )

    def test_simultaneous_first_qe_is_rejected(self) -> None:
        transitions = copy.deepcopy(self.transitions)
        transitions[0].update(
            {
                "action_mask": (1 << 8) | (1 << 9),
                "e_request_edge": True,
                "e_accepted": True,
                "accepted_throw_grenade_id": 0,
            }
        )
        with self.assertRaises(DatasetValidationError):
            validate_v2_runtime_contract(
                self.dataset, "episode", self.frames, transitions
            )

    def test_q_suppresses_planar_movement(self) -> None:
        transitions = copy.deepcopy(self.transitions)
        transitions[0]["forward_axis"] = 1.0
        with self.assertRaisesRegex(DatasetValidationError, "planar movement"):
            validate_v2_runtime_contract(
                self.dataset, "episode", self.frames, transitions
            )

    def test_throw_schema_contains_physics_not_mission_labels(self) -> None:
        names = {field.name for field in V2_THROW}
        self.assertIn("realized_contact_order", names)
        self.assertIn("preview_to_realized_flight_parity", names)
        self.assertIn("launch_position", names)
        self.assertIn("launch_velocity", names)
        self.assertIn("physics_config_identity", names)
        self.assertNotIn("intended_family", names)
        self.assertNotIn("semantic_success", names)
        self.assertNotIn("base_cell_id", names)

    def test_valid_certified_mission_contract(self) -> None:
        mission = "rectangle_north_face"
        frames = [
            frame(0, q=False, cooldown=0, mission_type=mission),
            frame(1, q=True, cooldown=0, mission_type=mission),
            frame(
                2, q=True, cooldown=40,
                grenades=[{"id": 0, "resting": False}], mission_type=mission,
            ),
        ]
        episode = {
            "v2_source": "mission",
            "v2_contract_version": "shared-persistent-semi-markov-1+certified-sixty-missions-1",
            "v2_mission_type": mission,
            "collection_mission": mission,
            "mission_required": True,
            "mission_success": True,
            "accepted_for_balancing": True,
            "v2_construction_certified": True,
            "v2_mission_region_visible_all_frames": True,
            "v2_preview_region_visible_all_q_frames": True,
            "v2_opening_arena_context_visible": True,
            "v2_mission_event_frame": 2,
            "v2_accepted_throw_count": 1,
            "v2_throws": [{
                "grenade_id": 0,
                "preview_to_realized_flight_parity": True,
                "physics_config_identity": "grenade-sim-config-r1",
                "launch_position": {"x": 0, "y": 0, "z": 0},
                "launch_velocity": {"x": 1400, "y": 0, "z": 0},
            }],
        }
        dataset = {
            "schema_version": "trajectory_throw_v2-preflight-13",
            "observation_rate_hz": 20,
            "collection_policy": "training_v2_combined_sixty_missions_v1",
        }
        validate_v2_runtime_contract(
            dataset, "episode", frames, self.transitions, episode
        )


if __name__ == "__main__":
    unittest.main()
