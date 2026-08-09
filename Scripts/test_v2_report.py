#!/usr/bin/env python3

from __future__ import annotations

import io
import unittest

from PIL import Image

from v2_report import bounce_band, perceptual_hash


class V2ReportTests(unittest.TestCase):
    def test_bounce_bands_are_exhaustive_at_boundaries(self) -> None:
        self.assertEqual([bounce_band(value) for value in (0, 1, 2, 3, 4)], [
            "zero", "one", "two_to_three", "two_to_three", "four_plus",
        ])

    def test_perceptual_hash_is_deterministic(self) -> None:
        image = Image.new("RGB", (16, 16), "green")
        payload = io.BytesIO()
        image.save(payload, format="PNG")
        self.assertEqual(perceptual_hash(payload.getvalue()), perceptual_hash(payload.getvalue()))


if __name__ == "__main__":
    unittest.main()
