#!/usr/bin/env python3
"""Focused tests for the combined V2 schema-11 runtime validator."""

from __future__ import annotations

import copy
import unittest

from Scripts.review_dataset import (
    DatasetValidationError,
    v2_realized_throw_success,
    validate_v2_runtime_contract,
)


def frame(
    index: int,
    *,
    q: bool,
    cooldown: int,
    grenades: list[dict] | None = None,
) -> dict:
    grenades = grenades or []
    resting = sum(bool(grenade["resting"]) for grenade in grenades)
    return {
        "frame_index": index,
        "q_visibility": q,
        "trajectory_visible": q,
        "aim_lock_active": q,
        "cooldown_remaining_steps": cooldown,
        "crosshair_state": "Cooldown" if cooldown else "Ready",
        "grenades": grenades,
        "flying_grenade_count": len(grenades) - resting,
        "resting_grenade_count": resting,
        "visible_grenade_count": len(grenades),
        "total_grenade_count": len(grenades),
        "position": {"x": 10.0, "y": 20.0, "z": 100.0},
        "camera": {"yaw": 0.0, "pitch": 0.0, "roll": 0.0},
    }


class V2RuntimeContractTests(unittest.TestCase):

    def test_realized_outcomes_are_derived_from_evidence(self) -> None:
        direct = {
            "intended_family": "solid_object",
            "intended_outcome": "direct_center",
            "intended_target": "CurriculumObject_Rectangle",
            "realized_contact_order": ["CurriculumObject_Rectangle", "CurriculumFloor"],
        }
        self.assertTrue(v2_realized_throw_success(direct))
        wrong_target = {**direct, "realized_contact_order": ["CurriculumFloor"]}
        self.assertFalse(v2_realized_throw_success(wrong_target))

        floor = {
            "intended_family": "floor_bounce_rest",
            "intended_outcome": "one_bounce",
            "intended_target": "CurriculumFloor",
            "realized_contact_order": ["CurriculumFloor"],
            "bounce_count": 2,
            "post_contact_travel_cm": 120.0,
        }
        self.assertTrue(v2_realized_throw_success(floor))
        self.assertFalse(v2_realized_throw_success({**floor, "bounce_count": 8}))

        hoop = {
            "intended_family": "hoop",
            "intended_outcome": "clean_pass",
            "intended_target": "CurriculumObject_Hoop",
            "realized_contact_order": ["CurriculumFloor"],
            "hoop_pass_frame": 24,
        }
        self.assertTrue(v2_realized_throw_success(hoop))
        self.assertFalse(v2_realized_throw_success({**hoop, "hoop_pass_frame": None}))

    def setUp(self) -> None:
        self.dataset = {
            "schema_version": "trajectory_throw_v2-preflight-11",
            "observation_rate_hz": 20,
        }
        self.frames = [
            frame(0, q=False, cooldown=0),
            frame(1, q=True, cooldown=0),
            frame(
                2,
                q=True,
                cooldown=40,
                grenades=[{"id": 0, "resting": False}],
            ),
        ]
        self.transitions = [
            {
                "source_frame_index": 0,
                "action_mask": 1 << 8,
                "forward_axis": 0.0,
                "right_axis": 0.0,
                "q_rising_edge": True,
                "q_falling_edge": False,
                "e_request_edge": False,
                "e_accepted": False,
                "planar_movement_suppressed": False,
                "e_rejection_reason": "none",
                "accepted_throw_grenade_id": None,
                "cooldown_before_steps": 0,
                "cooldown_after_steps": 0,
            },
            {
                "source_frame_index": 1,
                "action_mask": (1 << 8) | (1 << 9),
                "forward_axis": 0.0,
                "right_axis": 0.0,
                "q_rising_edge": False,
                "q_falling_edge": False,
                "e_request_edge": True,
                "e_accepted": True,
                "planar_movement_suppressed": False,
                "e_rejection_reason": "none",
                "accepted_throw_grenade_id": 0,
                "cooldown_before_steps": 0,
                "cooldown_after_steps": 40,
            },
        ]

    def test_valid_q_then_e_sequence(self) -> None:
        validate_v2_runtime_contract(
            self.dataset, "episode", self.frames, self.transitions
        )

    def test_simultaneous_first_qe_cannot_be_accepted(self) -> None:
        transitions = copy.deepcopy(self.transitions)
        transitions[0].update(
            {
                "action_mask": (1 << 8) | (1 << 9),
                "e_request_edge": True,
                "e_accepted": True,
                "e_rejection_reason": "none",
                "accepted_throw_grenade_id": 0,
                "cooldown_after_steps": 40,
            }
        )
        with self.assertRaises(DatasetValidationError):
            validate_v2_runtime_contract(
                self.dataset, "episode", self.frames, transitions
            )

    def test_forbidden_trajectory_payload_is_rejected(self) -> None:
        frames = copy.deepcopy(self.frames)
        frames[0]["trajectory_points"] = []
        with self.assertRaises(DatasetValidationError):
            validate_v2_runtime_contract(
                self.dataset, "episode", frames, self.transitions
            )

    def make_trajectory_hold_records(self) -> tuple[list[dict], list[dict]]:
        frames = [frame(0, q=False, cooldown=0)]
        for index in range(1, 81):
            cooldown = (
                0
                if index < 11
                else 40
                if index == 11
                else max(0, 51 - index)
            )
            has_grenade = index >= 11
            grenades = (
                [{"id": 0, "resting": index >= 40}] if has_grenade else []
            )
            frames.append(
                frame(index, q=True, cooldown=cooldown, grenades=grenades)
            )

        transitions = []
        for source_index in range(80):
            accepted = source_index == 10
            cooldown = max(0, 50 - source_index) if source_index >= 11 else 0
            transitions.append(
                {
                    "source_frame_index": source_index,
                    "action_mask": (1 << 8) | ((1 << 9) if accepted else 0),
                    "forward_axis": 0.0,
                    "right_axis": 0.0,
                    "q_rising_edge": source_index == 0,
                    "q_falling_edge": False,
                    "e_request_edge": accepted,
                    "e_accepted": accepted,
                    "planar_movement_suppressed": False,
                    "e_rejection_reason": "none",
                    "accepted_throw_grenade_id": 0 if accepted else None,
                    "cooldown_before_steps": 0 if accepted else cooldown,
                    "cooldown_after_steps": 40 if accepted else cooldown,
                }
            )
        return frames, transitions

    def test_valid_trajectory_hold_mission(self) -> None:
        dataset = {
            **self.dataset,
            "collection_policy": "diagnostic_v2_trajectory_hold_mission",
        }
        episode = {
            "v2_source": "random_play",
            "v2_cell_id": "R08_throw_hold_cooldown_diagnostic",
            "accepted_for_balancing": False,
        }
        frames, transitions = self.make_trajectory_hold_records()
        validate_v2_runtime_contract(
            dataset, "episode", frames, transitions, episode
        )

    def test_trajectory_hold_mission_rejects_q_release(self) -> None:
        dataset = {
            **self.dataset,
            "collection_policy": "diagnostic_v2_trajectory_hold_mission",
        }
        episode = {
            "v2_source": "random_play",
            "v2_cell_id": "R08_throw_hold_cooldown_diagnostic",
            "accepted_for_balancing": False,
        }
        frames, transitions = self.make_trajectory_hold_records()
        frames[60]["q_visibility"] = False
        frames[60]["trajectory_visible"] = False
        frames[60]["aim_lock_active"] = False
        with self.assertRaises(DatasetValidationError):
            validate_v2_runtime_contract(
                dataset, "episode", frames, transitions, episode
            )

    def test_diagnostic_v2_rejects_balancing_credit(self) -> None:
        dataset = {
            **self.dataset,
            "collection_policy": "diagnostic_v2_trajectory_hold_mission",
        }
        episode = {
            "v2_source": "random_play",
            "v2_cell_id": "R08_throw_hold_cooldown_diagnostic",
            "accepted_for_balancing": True,
        }
        frames, transitions = self.make_trajectory_hold_records()
        with self.assertRaises(DatasetValidationError):
            validate_v2_runtime_contract(
                dataset, "episode", frames, transitions, episode
            )

    def make_contract3_random_records(
        self, masks: list[int], cell_id: str = "R01-short"
    ) -> tuple[dict, list[dict], list[dict], dict]:
        dataset = {
            **self.dataset,
            "collection_policy": "training_v2_immutable_local_recipes_v1",
        }
        frames = [frame(0, q=False, cooldown=0)]
        transitions = []
        previous = 0
        for index, mask in enumerate(masks):
            q = bool(mask & (1 << 8))
            q_previous = bool(previous & (1 << 8))
            target = frame(index + 1, q=q, cooldown=0)
            target["contact"] = False
            frames.append(target)
            transitions.append(
                {
                    "source_frame_index": index,
                    "action_mask": mask,
                    "forward_axis": 1.0 if mask & 1 else 0.0,
                    "right_axis": 0.0,
                    "q_rising_edge": q and not q_previous,
                    "q_falling_edge": q_previous and not q,
                    "e_request_edge": False,
                    "e_accepted": False,
                    "planar_movement_suppressed": False,
                    "e_rejection_reason": "none",
                    "accepted_throw_grenade_id": None,
                    "cooldown_before_steps": 0,
                    "cooldown_after_steps": 0,
                }
            )
            previous = mask
        frames[0]["contact"] = False
        episode = {
            "plan_version": "trajectory-throw-v2-local-3",
            "v2_contract_version": "v2-data-generation-spec-3+temporal-2",
            "v2_source": "random_play",
            "v2_cell_id": cell_id,
            "v2_replay_identity": "test-replay",
            "v2_aim_acquisition_profile": "static_hold",
            "v2_q_retention_profile": "immediate_release",
            "v2_post_throw_movement_profile": "stationary",
            "v2_post_throw_camera_profile": "fixed",
            "v2_expected_throw_count": 0,
            "v2_accepted_throw_count": 0,
            "v2_primary_event_complete_frame": (
                None if cell_id.startswith("R01-") else 1
            ),
            "v2_required_continuation_steps": (
                0 if cell_id.startswith("R01-") else len(frames) - 2
            ),
            "v2_throws": [],
            "planned_credited_frames": len(frames),
            "accepted_for_balancing": True,
            "mission_success": True,
        }
        return dataset, frames, transitions, episode

    def test_contract3_rejects_dead_random_tail(self) -> None:
        dataset, frames, transitions, episode = self.make_contract3_random_records(
            [1, (1 << 8), 0] + ([0] * 119)
        )
        with self.assertRaisesRegex(DatasetValidationError, "zero-input run"):
            validate_v2_runtime_contract(
                dataset, "episode", frames, transitions, episode
            )

    def test_contract3_accepts_varied_random_activity(self) -> None:
        masks = [(1 << 8), 0, (1 << 4), 1] * 30
        dataset, frames, transitions, episode = self.make_contract3_random_records(
            masks
        )
        validate_v2_runtime_contract(
            dataset, "episode", frames, transitions, episode
        )

    def test_contract3_accepts_bounded_event_random_continuation(self) -> None:
        masks = [(1 << 8), 0] + ([1, (1 << 4), (1 << 8), 0] * 10)
        dataset, frames, transitions, episode = self.make_contract3_random_records(
            masks, "R02-medium"
        )
        validate_v2_runtime_contract(
            dataset, "episode", frames, transitions, episode
        )


if __name__ == "__main__":
    unittest.main()
