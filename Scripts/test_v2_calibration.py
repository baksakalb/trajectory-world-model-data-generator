#!/usr/bin/env python3

from __future__ import annotations

import unittest

from v2_calibration import derive_calibration
from v2_catalog import SEQUENCE_TEMPLATES, base_cells


class V2CalibrationTests(unittest.TestCase):
    def test_complete_evidence_qualifies_and_uses_conservative_minima(self) -> None:
        records = []
        for cell in base_cells():
            records.append({
                "v2_source": cell["source"], "v2_cell_id": cell["cell_id"],
                "v2_sequence_template_id": None, "v2_throws": [{"intended_family": cell["family"]}],
                "observation_count": 120, "accepted_for_balancing": True,
            })
        for template in SEQUENCE_TEMPLATES:
            records.append({
                "v2_source": "mission", "v2_cell_id": "sequence",
                "v2_sequence_template_id": template.template_id,
                "v2_expected_throw_count": template.grenade_count,
                "v2_throws": [{"intended_family": "solid_object"}],
                "observation_count": 200 + template.grenade_count,
                "accepted_for_balancing": True,
            })
        value = derive_calibration(records)
        self.assertTrue(value["qualified"])
        self.assertEqual(value["expected_credited_frames_by_family"]["solid_object"], 120)
        self.assertEqual(value["qualification_evidence"]["accepted_base_cell_count"], 1184)

    def test_partial_evidence_never_claims_qualification(self) -> None:
        records = [
            {"v2_source": family == "random_play" and "random_play" or "mission",
             "v2_cell_id": family, "v2_sequence_template_id": None,
             "v2_throws": [{"intended_family": family}], "observation_count": 100,
             "accepted_for_balancing": True}
            for family in ("random_play", "solid_object", "wall_corner", "floor_bounce_rest", "ramp", "hoop")
        ]
        records.extend({
            "v2_source": "mission", "v2_cell_id": "s", "v2_sequence_template_id": f"S{count}",
            "v2_expected_throw_count": count, "v2_throws": [{"intended_family": "solid_object"}],
            "observation_count": 200, "accepted_for_balancing": True,
        } for count in (2, 3, 4, 5))
        self.assertFalse(derive_calibration(records)["qualified"])


if __name__ == "__main__":
    unittest.main()
