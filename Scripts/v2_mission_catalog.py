#!/usr/bin/env python3
"""Frozen V2 mission catalog and deterministic certified-region sampling.

The catalog describes named geometry and player-controlled initial conditions.
It deliberately contains no restitution, damping, bounce-count, settling, or
replacement parameters.  Final launch certification is performed in Unreal by
the same ``FGrenadeSim`` path used by preview and realized grenades.
"""

from __future__ import annotations

import hashlib
import json
import math
from collections import Counter
from dataclasses import dataclass
from fractions import Fraction
from typing import Any, Iterable


CATALOG_VERSION = "trajectory-throw-v2-sixty-certified-regions-1"
CANONICAL_PHYSICS_ID = "grenade-sim-config-r1+launch-1400cmps+cooldown-2s"
TYPE_FRAME_SHARE = Fraction(1, 200)
FAMILY_FRAME_SHARES = {
    "broad_object_surface": Fraction(13, 200),
    "object_edge_apex": Fraction(13, 200),
    "wall_corner_rebound": Fraction(12, 200),
    "hoop": Fraction(10, 200),
    "ramp": Fraction(8, 200),
    "out_of_bounds": Fraction(4, 200),
}


@dataclass(frozen=True)
class MissionType:
    slug: str
    family: str
    event_kind: str
    target_actor: str
    target_region: str
    center: tuple[float, float, float]
    approach: tuple[float, float]
    duration_seconds: Fraction
    region_radius_cm: float = 34.0
    variation_axis: tuple[float, float, float] = (0.0, 0.0, 1.0)
    variation_extent_cm: float = 30.0
    secondary_axis: tuple[float, float, float] = (0.0, 1.0, 0.0)
    secondary_extent_cm: float = 18.0
    expected_contact_order: tuple[str, ...] = ()
    boundary: str = ""
    direction: str = ""

    def as_dict(self) -> dict[str, Any]:
        value = dict(self.__dict__)
        for key, item in list(value.items()):
            if isinstance(item, tuple):
                value[key] = list(item)
            elif isinstance(item, Fraction):
                value[key] = float(item)
        value["type_frame_share"] = float(TYPE_FRAME_SHARE)
        return value


def _m(
    slug: str,
    family: str,
    event: str,
    actor: str,
    region: str,
    center: tuple[float, float, float],
    approach: tuple[float, float],
    duration: str,
    **kwargs: Any,
) -> MissionType:
    return MissionType(
        slug, family, event, actor, region, center, approach,
        Fraction(duration), **kwargs,
    )


def mission_types() -> tuple[MissionType, ...]:
    b = "broad_object_surface"
    e = "object_edge_apex"
    w = "wall_corner_rebound"
    h = "hoop"
    r = "ramp"
    o = "out_of_bounds"
    rect = "CurriculumObject_Rectangle"
    pyramid = "CurriculumObject_Pyramid"
    sphere = "CurriculumObject_Sphere"
    hoop = "CurriculumObject_Hoop"
    ramp = "CurriculumObject_Ramp"
    result = (
        # Broad rectangle faces and top, safely inset from every edge.
        _m("rectangle_north_face", b, "contact_region", rect, "north_face", (-615, 700, 135), (1, 0), "6", variation_axis=(0, 1, 0), variation_extent_cm=58, secondary_extent_cm=55),
        _m("rectangle_south_face", b, "contact_region", rect, "south_face", (-785, 700, 135), (-1, 0), "6", variation_axis=(0, 1, 0), variation_extent_cm=58, secondary_extent_cm=55),
        _m("rectangle_east_face", b, "contact_region", rect, "east_face", (-700, 840, 135), (0, 1), "6", variation_axis=(1, 0, 0), variation_extent_cm=32, secondary_extent_cm=55),
        _m("rectangle_west_face", b, "contact_region", rect, "west_face", (-700, 560, 135), (0, -1), "6", variation_axis=(1, 0, 0), variation_extent_cm=32, secondary_extent_cm=55),
        _m("rectangle_top_surface", b, "contact_region", rect, "top_inset", (-700, 700, 250), (-1, 0), "6", variation_axis=(1, 0, 0), variation_extent_cm=30, secondary_axis=(0, 1, 0), secondary_extent_cm=55),
        # Pyramid is 280x280x250; points lie in the interior of each slope.
        _m("pyramid_north_face", b, "contact_region", pyramid, "north_slope", (770, 700, 125), (1, 0), "6", variation_axis=(0, 1, 0), variation_extent_cm=38, secondary_axis=(-0.49, 0, 0.87), secondary_extent_cm=25),
        _m("pyramid_south_face", b, "contact_region", pyramid, "south_slope", (630, 700, 125), (-1, 0), "6", variation_axis=(0, 1, 0), variation_extent_cm=38, secondary_axis=(0.49, 0, 0.87), secondary_extent_cm=25),
        _m("pyramid_east_face", b, "contact_region", pyramid, "east_slope", (700, 770, 125), (0, 1), "6", variation_axis=(1, 0, 0), variation_extent_cm=38, secondary_axis=(0, -0.49, 0.87), secondary_extent_cm=25),
        _m("pyramid_west_face", b, "contact_region", pyramid, "west_slope", (700, 630, 125), (0, -1), "6", variation_axis=(1, 0, 0), variation_extent_cm=38, secondary_axis=(0, 0.49, 0.87), secondary_extent_cm=25),
        _m("sphere_north_quadrant", b, "contact_region", sphere, "north_quadrant", (-580, -700, 125), (1, 0), "6", variation_axis=(0, 1, 0), variation_extent_cm=28, secondary_extent_cm=30),
        _m("sphere_south_quadrant", b, "contact_region", sphere, "south_quadrant", (-820, -700, 125), (-1, 0), "6", variation_axis=(0, 1, 0), variation_extent_cm=28, secondary_extent_cm=30),
        _m("sphere_east_quadrant", b, "contact_region", sphere, "east_quadrant", (-700, -580, 125), (0, 1), "6", variation_axis=(1, 0, 0), variation_extent_cm=28, secondary_extent_cm=30),
        _m("sphere_west_quadrant", b, "contact_region", sphere, "west_quadrant", (-700, -820, 125), (0, -1), "6", variation_axis=(1, 0, 0), variation_extent_cm=28, secondary_extent_cm=30),
        # Narrow but physical grenade-radius-aware edge corridors.
        _m("rectangle_ne_vertical_edge", e, "edge_contact", rect, "north_east_vertical", (-615, 840, 135), (1, 1), "13/2", variation_axis=(0, 0, 1), variation_extent_cm=55, region_radius_cm=22),
        _m("rectangle_nw_vertical_edge", e, "edge_contact", rect, "north_west_vertical", (-615, 560, 135), (1, -1), "13/2", variation_axis=(0, 0, 1), variation_extent_cm=55, region_radius_cm=22),
        _m("rectangle_se_vertical_edge", e, "edge_contact", rect, "south_east_vertical", (-785, 840, 135), (-1, 1), "13/2", variation_axis=(0, 0, 1), variation_extent_cm=55, region_radius_cm=22),
        _m("rectangle_sw_vertical_edge", e, "edge_contact", rect, "south_west_vertical", (-785, 560, 135), (-1, -1), "13/2", variation_axis=(0, 0, 1), variation_extent_cm=55, region_radius_cm=22),
        _m("rectangle_north_upper_edge", e, "edge_contact", rect, "north_upper", (-615, 700, 250), (1, 0), "13/2", variation_axis=(0, 1, 0), variation_extent_cm=62, region_radius_cm=22),
        _m("rectangle_south_upper_edge", e, "edge_contact", rect, "south_upper", (-785, 700, 250), (-1, 0), "13/2", variation_axis=(0, 1, 0), variation_extent_cm=62, region_radius_cm=22),
        _m("rectangle_east_upper_edge", e, "edge_contact", rect, "east_upper", (-700, 840, 250), (0, 1), "13/2", variation_axis=(1, 0, 0), variation_extent_cm=34, region_radius_cm=22),
        _m("rectangle_west_upper_edge", e, "edge_contact", rect, "west_upper", (-700, 560, 250), (0, -1), "13/2", variation_axis=(1, 0, 0), variation_extent_cm=34, region_radius_cm=22),
        _m("pyramid_ne_ridge", e, "edge_contact", pyramid, "north_east_ridge", (750, 750, 160), (1, 1), "13/2", variation_axis=(-0.35, -0.35, 0.87), variation_extent_cm=48, region_radius_cm=24),
        _m("pyramid_nw_ridge", e, "edge_contact", pyramid, "north_west_ridge", (750, 650, 160), (1, -1), "13/2", variation_axis=(-0.35, 0.35, 0.87), variation_extent_cm=48, region_radius_cm=24),
        _m("pyramid_se_ridge", e, "edge_contact", pyramid, "south_east_ridge", (650, 750, 160), (-1, 1), "13/2", variation_axis=(0.35, -0.35, 0.87), variation_extent_cm=48, region_radius_cm=24),
        _m("pyramid_sw_ridge", e, "edge_contact", pyramid, "south_west_ridge", (650, 650, 160), (-1, -1), "13/2", variation_axis=(0.35, 0.35, 0.87), variation_extent_cm=48, region_radius_cm=24),
        _m("pyramid_apex", e, "apex_contact", pyramid, "apex_corridor", (700, 700, 250), (-1, 0), "13/2", region_radius_cm=28, variation_axis=(1, 0, 0), variation_extent_cm=12, secondary_axis=(0, 1, 0), secondary_extent_cm=12),
        # Four direct, four oblique, and four two-wall corner interactions.
        *tuple(_m(f"{side}_wall_direct", w, "wall_contact", f"CurriculumWall_{side.title()}", f"{side}_direct", point, approach, "7", variation_axis=tangent, variation_extent_cm=380, secondary_extent_cm=90, boundary=side) for side, point, approach, tangent in (
            ("north", (1600, 0, 190), (-1, 0), (0, 1, 0)), ("south", (-1600, 0, 190), (1, 0), (0, 1, 0)),
            ("east", (0, 1600, 190), (0, -1), (1, 0, 0)), ("west", (0, -1600, 190), (0, 1), (1, 0, 0)),)),
        *tuple(_m(f"{side}_wall_oblique", w, "wall_contact", f"CurriculumWall_{side.title()}", f"{side}_oblique", point, approach, "7", variation_axis=tangent, variation_extent_cm=330, secondary_extent_cm=90, boundary=side) for side, point, approach, tangent in (
            ("north", (1600, -240, 190), (-1, .42), (0, 1, 0)), ("south", (-1600, 240, 190), (1, -.42), (0, 1, 0)),
            ("east", (240, 1600, 190), (-.42, -1), (1, 0, 0)), ("west", (-240, -1600, 190), (.42, 1), (1, 0, 0)),)),
        *tuple(_m(f"{corner}_corner_two_wall", w, "two_wall_contact", "CurriculumArena", f"{corner}_corner", point, approach, "15/2", region_radius_cm=70, variation_axis=axis, variation_extent_cm=35, expected_contact_order=order, boundary=corner) for corner, point, approach, axis, order in (
            ("north_east", (1592, 1592, 175), (-1, -1), (1, -1, 0), ("CurriculumWall_North", "CurriculumWall_East")),
            ("north_west", (1592, -1592, 175), (-1, 1), (1, 1, 0), ("CurriculumWall_North", "CurriculumWall_West")),
            ("south_east", (-1592, 1592, 175), (1, -1), (1, 1, 0), ("CurriculumWall_South", "CurriculumWall_East")),
            ("south_west", (-1592, -1592, 175), (1, 1), (1, -1, 0), ("CurriculumWall_South", "CurriculumWall_West")),)),
        # Hoop plane is YZ and throws travel along X.
        _m("hoop_clean_negx_to_posx", h, "hoop_passage", hoop, "safe_opening", (700, -700, 145), (-1, 0), "7", direction="negative_x_to_positive_x", variation_axis=(0, 1, 0), variation_extent_cm=32, secondary_extent_cm=32),
        _m("hoop_clean_posx_to_negx", h, "hoop_passage", hoop, "safe_opening", (700, -700, 145), (1, 0), "7", direction="positive_x_to_negative_x", variation_axis=(0, 1, 0), variation_extent_cm=32, secondary_extent_cm=32),
        *tuple(_m(f"hoop_{rim}_{short}", h, "rim_contact", hoop, f"{rim}_rim", point, approach, "7", direction=direction, region_radius_cm=24, variation_axis=axis, variation_extent_cm=24) for direction, short, approach in (("negative_x_to_positive_x", "negx_to_posx", (-1, 0)), ("positive_x_to_negative_x", "posx_to_negx", (1, 0))) for rim, point, axis in (("upper", (700, -700, 250), (0, 1, 0)), ("lower", (700, -700, 40), (0, 1, 0)), ("left", (700, -805, 145), (0, 0, 1)), ("right", (700, -595, 145), (0, 0, 1)))),
        # Ramp surface, crossover, and physical edge/lip corridors.
        _m("ramp_uphill_surface", r, "ramp_contact", ramp, "uphill_surface", (80, 0, 82), (1, 0), "7", variation_axis=(0, 1, 0), variation_extent_cm=62),
        _m("ramp_downhill_surface", r, "ramp_contact", ramp, "downhill_surface", (-80, 0, 132), (-1, 0), "7", variation_axis=(0, 1, 0), variation_extent_cm=62),
        _m("ramp_crossover_left_to_right", r, "ramp_crossover", ramp, "body_crossing", (0, 0, 150), (0, -1), "15/2", variation_axis=(1, 0, 0), variation_extent_cm=100, direction="left_to_right"),
        _m("ramp_crossover_right_to_left", r, "ramp_crossover", ramp, "body_crossing", (0, 0, 150), (0, 1), "15/2", variation_axis=(1, 0, 0), variation_extent_cm=100, direction="right_to_left"),
        _m("ramp_left_side_edge", r, "edge_contact", ramp, "left_side_edge", (0, -130, 108), (0, -1), "7", variation_axis=(1, 0, .325), variation_extent_cm=95, region_radius_cm=24),
        _m("ramp_right_side_edge", r, "edge_contact", ramp, "right_side_edge", (0, 130, 108), (0, 1), "7", variation_axis=(1, 0, .325), variation_extent_cm=95, region_radius_cm=24),
        _m("ramp_high_end_lip", r, "edge_contact", ramp, "high_end_lip", (-250, 0, 190), (-1, 0), "7", variation_axis=(0, 1, 0), variation_extent_cm=70, region_radius_cm=25),
        _m("ramp_low_end_lip", r, "edge_contact", ramp, "low_end_lip", (250, 0, 28), (1, 0), "7", variation_axis=(0, 1, 0), variation_extent_cm=70, region_radius_cm=25),
        # Boundary exits are aimed above the wall, retaining the named wall in view.
        *tuple(_m(f"{side}_boundary_exit", o, "arena_exit", f"CurriculumWall_{side.title()}", f"{side}_boundary", point, approach, "8", variation_axis=tangent, variation_extent_cm=300, secondary_extent_cm=90, boundary=side) for side, point, approach, tangent in (
            ("north", (1900, 0, 650), (-1, 0), (0, 1, 0)), ("south", (-1900, 0, 650), (1, 0), (0, 1, 0)),
            ("east", (0, 1900, 650), (0, -1), (1, 0, 0)), ("west", (0, -1900, 650), (0, 1), (1, 0, 0)),)),
    )
    validate_catalog(result)
    return result


def validate_catalog(types: Iterable[MissionType]) -> None:
    values = tuple(types)
    if len(values) != 60:
        raise ValueError(f"V2 catalog must contain 60 mission types, got {len(values)}")
    slugs = [item.slug for item in values]
    if len(slugs) != len(set(slugs)):
        raise ValueError("V2 mission type slugs must be unique")
    counts = Counter(item.family for item in values)
    expected = {family: int(share / TYPE_FRAME_SHARE) for family, share in FAMILY_FRAME_SHARES.items()}
    if counts != Counter(expected):
        raise ValueError(f"V2 mission family counts changed: {dict(counts)}")
    if sum(FAMILY_FRAME_SHARES.values(), Fraction()) != Fraction(3, 10):
        raise ValueError("V2 mission family shares must total 30%")
    if any(item.duration_seconds <= 0 for item in values):
        raise ValueError("mission durations must be positive")


def radical_inverse(index: int, base: int) -> float:
    """Return a deterministic low-discrepancy coordinate in [0, 1)."""
    value = 0.0
    denominator = 1.0
    while index:
        index, digit = divmod(index, base)
        denominator *= base
        value += digit / denominator
    return value


def _normalized_xy(value: tuple[float, float]) -> tuple[float, float]:
    length = math.hypot(*value)
    if length <= 1e-9:
        raise ValueError("mission approach vector must be non-zero")
    return value[0] / length, value[1] / length


def build_solution(item: MissionType, repetition: int, observation_rate: int) -> dict[str, Any]:
    """Sample one immutable point in a known-working certified region.

    Repetition zero is central. Later points use separated Halton coordinates;
    the seed affects identity, never mission validity.
    """
    if repetition < 0 or observation_rate <= 0:
        raise ValueError("invalid mission repetition or observation rate")
    if repetition == 0:
        u = v = distance_u = arc_u = 0.5
    else:
        ordinal = repetition
        u, v = radical_inverse(ordinal, 2), radical_inverse(ordinal, 3)
        distance_u, arc_u = radical_inverse(ordinal, 5), radical_inverse(ordinal, 7)
    center = list(item.center)
    for coordinate in range(3):
        center[coordinate] += item.variation_axis[coordinate] * (2 * u - 1) * item.variation_extent_cm
        center[coordinate] += item.secondary_axis[coordinate] * (2 * v - 1) * item.secondary_extent_cm
    ax, ay = _normalized_xy(item.approach)
    distance = 430.0 + 260.0 * distance_u
    # Approach points outward from geometry toward the player.
    spawn = [center[0] + ax * distance, center[1] + ay * distance, 100.0]
    # Small deterministic tangent displacement produces direct/oblique spread.
    oblique = (2 * radical_inverse(max(1, repetition), 11) - 1) * 115.0
    spawn[0] += -ay * oblique
    spawn[1] += ax * oblique
    if item.event_kind == "arena_exit":
        distance = 1050.0 + 180.0 * distance_u
        spawn = [center[0] + ax * distance, center[1] + ay * distance, 100.0]
    if item.event_kind == "hoop_passage" or item.event_kind == "rim_contact":
        side = -1.0 if item.direction == "negative_x_to_positive_x" else 1.0
        spawn = [center[0] + side * (560.0 + 100.0 * distance_u), center[1], 100.0]
        spawn[1] += oblique * 0.35
    if item.event_kind == "ramp_crossover":
        side = -1.0 if item.direction == "left_to_right" else 1.0
        spawn = [center[0], center[1] + side * (520.0 + 80.0 * distance_u), 100.0]
    duration_frames = math.ceil(float(item.duration_seconds) * observation_rate) + 1
    establish = round((0.95 + 0.4 * radical_inverse(repetition + 1, 13)) * observation_rate)
    adjust = max(3, round(0.30 * observation_rate))
    preview = round((0.75 + 0.3 * arc_u) * observation_rate)
    throw_source = establish + adjust + preview
    return {
        "catalog_version": CATALOG_VERSION,
        "canonical_physics_id": CANONICAL_PHYSICS_ID,
        "mission_type": item.slug,
        "family": item.family,
        "event_kind": item.event_kind,
        "target_actor": item.target_actor,
        "target_region": item.target_region,
        "target_point": {"x": center[0], "y": center[1], "z": center[2]},
        "player_spawn": {"x": spawn[0], "y": spawn[1], "z": spawn[2]},
        "region_radius_cm": item.region_radius_cm,
        "expected_contact_order": list(item.expected_contact_order),
        "boundary": item.boundary or None,
        "direction": item.direction or None,
        "ballistic_branch": "low",
        "launch_speed_cm_per_second": 1400.0,
        "timing": {
            "establish_steps": establish,
            "camera_adjust_steps": adjust,
            "preview_dwell_steps": preview,
            "throw_source_frame": throw_source,
            "total_observation_frames": duration_frames,
        },
        "variation": {
            "repetition": repetition,
            "surface_u": u,
            "surface_v": v,
            "distance_u": distance_u,
            "arc_u": arc_u,
            "oblique_offset_cm": oblique,
        },
        "camera_railguards": {
            "region_visible_all_frames": True,
            "preview_and_region_visible_during_q": True,
            "opening_has_arena_context": True,
        },
    }


def catalog_fingerprint() -> str:
    payload = json.dumps(
        [item.as_dict() for item in mission_types()],
        sort_keys=True, separators=(",", ":"), ensure_ascii=True,
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def review_recipes(observation_rate: int = 20) -> list[dict[str, Any]]:
    """Exactly three separated 384x384 review examples per mission type."""
    values: list[dict[str, Any]] = []
    for item in mission_types():
        for review_variant, repetition in enumerate((0, 5, 17)):
            values.append({
                "mission_type": item.slug,
                "family": item.family,
                "review_variant": review_variant,
                "repetition_index": repetition,
                "solution": build_solution(item, repetition, observation_rate),
                "width": 384,
                "height": 384,
            })
    if len(values) != 180:
        raise AssertionError("V2 review set must contain exactly 180 recipes")
    return values
