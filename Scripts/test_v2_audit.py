#!/usr/bin/env python3

from __future__ import annotations

import unittest

from v2_audit import coverage, event_indices, select_examples


class V2AuditTests(unittest.TestCase):
    def test_selection_is_semantic_and_stable(self) -> None:
        slots = [{"slot_id": "A", "family": "hoop", "title": "clean", "cell_id": "cell-a"}]
        candidates = [
            {"cell_id": "cell-a", "semantic_success": False, "replay_identity": "000", "seed": 1, "episode_id": "e0", "output_directory": "z"},
            {"cell_id": "cell-a", "semantic_success": True, "replay_identity": "bbb", "seed": 2, "episode_id": "e2", "output_directory": "z"},
            {"cell_id": "cell-a", "semantic_success": True, "replay_identity": "aaa", "seed": 3, "episode_id": "e3", "output_directory": "z"},
        ]
        selected, missing = select_examples(candidates, slots)
        self.assertFalse(missing)
        self.assertEqual(selected[0]["replay_identity"], "aaa")

    def test_missing_slots_are_explicit(self) -> None:
        slots = [{"slot_id": "A", "family": "floor", "title": "one", "cell_id": "absent"}]
        selected, missing = select_examples([], slots)
        self.assertFalse(selected)
        self.assertEqual(missing, slots)
        self.assertFalse(coverage(selected, missing)["complete"])

    def test_event_manifest_uses_authoritative_transition_edges(self) -> None:
        transitions = [
            {"source_frame_index": 4, "q_rising_edge": True, "q_falling_edge": False,
             "e_request_edge": False, "e_accepted": False},
            {"source_frame_index": 9, "q_rising_edge": False, "q_falling_edge": False,
             "e_request_edge": True, "e_accepted": True},
            {"source_frame_index": 12, "q_rising_edge": False, "q_falling_edge": True,
             "e_request_edge": False, "e_accepted": False},
        ]
        example = {"throws": [{"first_contact_frame": 18, "rest_frame": 31}]}
        indices = event_indices(example, transitions, [{"frame_index": 0}, {"frame_index": 40}])
        self.assertEqual(indices["q_start"], [4])
        self.assertEqual(indices["q_release"], [12])
        self.assertEqual(indices["e_request"], [9])
        self.assertEqual(indices["e_accept"], [9])
        self.assertEqual(indices["tail_end"], [40])


if __name__ == "__main__":
    unittest.main()
