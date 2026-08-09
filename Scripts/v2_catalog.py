#!/usr/bin/env python3
"""Deterministic catalog and temporal-sequence vocabulary for combined V2.

This module is intentionally pure: catalog identity never depends on a worker,
requested frame budget, wall clock, or Unreal installation.  Both the planner
and qualification tools import it so enumeration has one authoritative source.
"""

from __future__ import annotations

import hashlib
import json
from collections import Counter
from dataclasses import dataclass
from typing import Any, Iterable, Iterator


CATALOG_VERSION = "trajectory-throw-v2-catalog-1"
SEQUENCE_VERSION = "trajectory-throw-v2-sequences-2"
HOLD_BANDS = ("short", "medium", "long", "very_long")
SECTORS = tuple(f"S{index}" for index in range(8))
DISTANCE_BANDS = ("near", "medium", "far")
ARC_BANDS = ("low", "medium", "high")

RANDOM_FAMILIES = (
    ("R01", "movement_interval"),
    ("R02", "preview_static_cancel"),
    ("R03", "preview_yaw_cancel"),
    ("R04", "preview_pitch_cancel"),
    ("R05", "preview_combined_cancel"),
    ("R06", "throw_release_immediate"),
    ("R07", "throw_hold_short"),
    ("R08", "throw_hold_cooldown"),
    ("R09", "reject_e_without_q"),
    ("R10", "reject_first_frame_qe"),
    ("R11", "reject_e_cooldown"),
    ("R12", "post_throw_stationary"),
    ("R13", "post_throw_forward"),
    ("R14", "post_throw_backward"),
    ("R15", "post_throw_strafe_left"),
    ("R16", "post_throw_strafe_right"),
    ("R17", "post_throw_diagonal"),
    ("R18", "multi_throw_persistence"),
)

MISSION_FAMILY_COUNTS = {
    "solid_object": 504,
    "wall_corner": 192,
    "floor_bounce_rest": 288,
    "ramp": 72,
    "hoop": 56,
}
BASE_FAMILY_COUNTS = {"random_play": 72, **MISSION_FAMILY_COUNTS}

AIM_ACQUISITION_PROFILES = (
    "static_hold",
    "prelook_q_off",
    "yaw_adjust",
    "pitch_adjust",
    "combined_adjust",
    "overshoot_correct",
    "cancel_reacquire",
)
POST_THROW_CAMERA_PROFILES = (
    "fixed",
    "yaw",
    "pitch",
    "combined",
    "preselected_hold",
    "ordinary_survey",
)
Q_RETENTION_PROFILES = ("immediate_release", "short_retain", "cooldown_long_retain")
POST_THROW_MOVEMENT_PROFILES = (
    "stationary",
    "forward",
    "backward",
    "strafe_left",
    "strafe_right",
    "mirrored_diagonal",
    "ordinary_semi_markov",
    "relocation_before_next_throw",
)


@dataclass(frozen=True)
class SequenceTemplate:
    template_id: str
    slug: str
    grenade_count: int
    aim_profiles: tuple[str, ...]
    q_profiles: tuple[str, ...]
    movement_profiles: tuple[str, ...]
    camera_profiles: tuple[str, ...]
    base_family_pattern: tuple[str, ...]
    requires_reaim_difference: bool = False
    cooldown_rejection_recovery: bool = False
    natural_relocation: bool = False
    minimum_camera_delta_degrees: float = 0.0
    minimum_trajectory_pixel_change_ratio: float = 0.0

    def as_dict(self) -> dict[str, Any]:
        value = dict(self.__dict__)
        for key, item in list(value.items()):
            if isinstance(item, tuple):
                value[key] = list(item)
        return value


def _sequence(
    ordinal: int,
    slug: str,
    grenade_count: int,
    *,
    aims: tuple[str, ...] = ("static_hold",),
    q: tuple[str, ...] = ("immediate_release",),
    movement: tuple[str, ...] = ("stationary",),
    camera: tuple[str, ...] = ("fixed",),
    families: tuple[str, ...] = ("solid_object",),
    reaim: bool = False,
    recovery: bool = False,
    relocation: bool = False,
) -> SequenceTemplate:
    def expand(values: tuple[str, ...]) -> tuple[str, ...]:
        if not values:
            raise ValueError(f"sequence SQ{ordinal:02d} has an empty step profile")
        return tuple(values[index % len(values)] for index in range(grenade_count))

    return SequenceTemplate(
        template_id=f"SQ{ordinal:02d}",
        slug=slug,
        grenade_count=grenade_count,
        aim_profiles=expand(aims),
        q_profiles=expand(q),
        movement_profiles=expand(movement),
        camera_profiles=expand(camera),
        base_family_pattern=expand(families),
        requires_reaim_difference=reaim,
        cooldown_rejection_recovery=recovery,
        natural_relocation=relocation,
        minimum_camera_delta_degrees=2.5 if reaim else 0.0,
        minimum_trajectory_pixel_change_ratio=0.015 if reaim else 0.0,
    )


# This is a bounded overlay on base-cell repetitions, not a Cartesian expansion.
SEQUENCE_TEMPLATES = (
    _sequence(1, "static_preview_then_throw", 2),
    _sequence(2, "prelook_q_off_then_throw", 2, aims=("prelook_q_off",)),
    _sequence(3, "yaw_adjust_then_throw", 2, aims=("yaw_adjust",), camera=("yaw",), reaim=True),
    _sequence(4, "pitch_adjust_then_throw", 2, aims=("pitch_adjust",), camera=("pitch",), reaim=True),
    _sequence(5, "combined_adjust_then_throw", 2, aims=("combined_adjust",), camera=("combined",), reaim=True),
    _sequence(6, "overshoot_reverse_correct_throw", 2, aims=("overshoot_correct",), camera=("combined",), reaim=True),
    _sequence(7, "cancel_look_reacquire_throw", 2, aims=("cancel_reacquire",), camera=("ordinary_survey",), reaim=True),
    _sequence(8, "same_object_different_arcs", 2, aims=("static_hold", "pitch_adjust"), families=("solid_object", "solid_object"), reaim=True),
    _sequence(9, "same_object_different_approaches", 2, aims=("static_hold", "combined_adjust"), movement=("relocation_before_next_throw",), families=("solid_object", "solid_object"), reaim=True, relocation=True),
    _sequence(10, "different_objects_consecutive", 2, aims=("static_hold", "yaw_adjust"), families=("solid_object", "solid_object"), reaim=True),
    _sequence(11, "object_floor_object", 3, aims=("static_hold", "pitch_adjust", "combined_adjust"), families=("solid_object", "floor_bounce_rest", "solid_object"), reaim=True),
    _sequence(12, "object_near_miss_wall", 3, aims=("static_hold", "yaw_adjust", "combined_adjust"), families=("solid_object", "solid_object", "wall_corner"), reaim=True),
    _sequence(13, "floor_bounce_hoop_open_landing", 3, aims=("pitch_adjust", "combined_adjust", "static_hold"), families=("floor_bounce_rest", "hoop", "floor_bounce_rest"), reaim=True),
    _sequence(14, "stationary_multi_throw_retain_q", 3, q=("cooldown_long_retain",) * 3, reaim=True),
    _sequence(15, "release_relocate_reacquire_throw", 2, aims=("static_hold", "prelook_q_off"), q=("immediate_release", "short_retain"), movement=("relocation_before_next_throw",), camera=("fixed", "ordinary_survey"), reaim=True, relocation=True),
    _sequence(16, "mixed_q_retention", 3, q=("immediate_release", "short_retain", "cooldown_long_retain"), reaim=True),
    _sequence(17, "cooldown_edge_then_valid_recovery", 2, q=("cooldown_long_retain", "immediate_release"), recovery=True, reaim=True),
    _sequence(18, "two_grenade_persistence", 2, families=("floor_bounce_rest", "solid_object"), reaim=True),
    _sequence(19, "three_grenade_persistence", 3, movement=("forward", "strafe_right", "stationary"), families=("solid_object", "wall_corner", "floor_bounce_rest"), reaim=True),
    _sequence(20, "four_grenade_persistence", 4, families=("solid_object", "floor_bounce_rest", "ramp", "hoop"), reaim=True),
    _sequence(21, "long_random_five_plus", 5, aims=("static_hold", "yaw_adjust", "pitch_adjust", "combined_adjust", "overshoot_correct"), q=("immediate_release", "short_retain", "cooldown_long_retain", "immediate_release", "short_retain"), movement=("ordinary_semi_markov", "forward", "strafe_left", "backward", "mirrored_diagonal"), camera=("ordinary_survey", "yaw", "pitch", "combined", "preselected_hold"), families=("random_play",) * 5, reaim=True),
)


def random_cells() -> Iterator[dict[str, Any]]:
    for family_index, (family_id, behavior) in enumerate(RANDOM_FAMILIES):
        for hold_index, hold_band in enumerate(HOLD_BANDS):
            yield {
                "source": "random_play",
                "family": "random_play",
                "cell_id": f"{family_id}-{hold_band}",
                "scenario_index": family_index * len(HOLD_BANDS) + hold_index,
                "behavior_id": family_id,
                "behavior_family": behavior,
                "hold_band": hold_band,
            }


def solid_object_cells() -> Iterator[dict[str, Any]]:
    targets = ("rectangle", "pyramid", "sphere")
    modes = ("direct_center", "direct_upper", "direct_lower", "glance_left", "glance_right", "floor_bounce_then_contact", "near_miss")
    scenario = 0
    for target in targets:
        for sector_index, sector in enumerate(SECTORS):
            for mode_index, mode in enumerate(modes):
                for variation in range(3):
                    yield {
                        "source": "mission", "family": "solid_object",
                        "cell_id": f"SO-{target}-{sector}-{mode}-v{variation}",
                        "scenario_index": scenario, "target": target,
                        "approach_sector": sector, "interaction_mode": mode,
                        "variation": variation,
                        "distance_band": DISTANCE_BANDS[(sector_index + mode_index + variation) % 3],
                        "arc_band": ARC_BANDS[(2 * sector_index + mode_index + variation) % 3],
                    }
                    scenario += 1


def wall_corner_cells() -> Iterator[dict[str, Any]]:
    walls = ("north", "south", "east", "west")
    approaches = ("direct", "glance_left", "glance_right", "near_parallel")
    heights = ("low", "middle", "high")
    sequences = ("wall_first", "floor_then_wall", "wall_then_floor", "adjacent_corner")
    scenario = 0
    for wall in walls:
        for approach in approaches:
            for height in heights:
                for sequence in sequences:
                    yield {
                        "source": "mission", "family": "wall_corner",
                        "cell_id": f"WA-{wall}-{approach}-{height}-{sequence}",
                        "scenario_index": scenario, "wall": wall,
                        "approach_profile": approach, "height_band": height,
                        "contact_sequence": sequence,
                    }
                    scenario += 1


def floor_cells() -> Iterator[dict[str, Any]]:
    outcomes = ("direct_settle", "one_bounce", "multi_bounce", "long_roll")
    scenario = 0
    for sector in SECTORS:
        for distance in DISTANCE_BANDS:
            for arc in ARC_BANDS:
                for outcome in outcomes:
                    yield {
                        "source": "mission", "family": "floor_bounce_rest",
                        "cell_id": f"FL-{sector}-{distance}-{arc}-{outcome}",
                        "scenario_index": scenario, "azimuth_sector": sector,
                        "distance_band": distance, "arc_band": arc, "outcome": outcome,
                    }
                    scenario += 1


def ramp_cells() -> Iterator[dict[str, Any]]:
    approaches = ("uphill", "downhill", "lateral_left_to_right", "lateral_right_to_left")
    regions = ("lower", "middle", "upper")
    modes = ("direct_surface", "glancing_surface", "floor_then_ramp", "ramp_then_floor", "cross_over", "near_miss_side")
    scenario = 0
    for approach in approaches:
        for region in regions:
            for mode in modes:
                yield {
                    "source": "mission", "family": "ramp",
                    "cell_id": f"RA-{approach}-{region}-{mode}",
                    "scenario_index": scenario, "approach": approach,
                    "ramp_region": region, "interaction_mode": mode,
                }
                scenario += 1


def hoop_cells() -> Iterator[dict[str, Any]]:
    directions = ("negative_x_to_positive_x", "positive_x_to_negative_x")
    paths = ("center", "horizontal_left", "horizontal_right", "vertical_above", "vertical_below", "oblique_left_to_right", "oblique_right_to_left")
    outcomes = ("clean_pass", "rim_contact", "near_miss", "floor_bounce_then_hoop")
    scenario = 0
    for direction in directions:
        for path in paths:
            for outcome in outcomes:
                yield {
                    "source": "mission", "family": "hoop",
                    "cell_id": f"HO-{direction}-{path}-{outcome}",
                    "scenario_index": scenario, "direction": direction,
                    "path_profile": path, "outcome": outcome,
                }
                scenario += 1


def base_cells() -> list[dict[str, Any]]:
    cells = [
        *random_cells(), *solid_object_cells(), *wall_corner_cells(),
        *floor_cells(), *ramp_cells(), *hoop_cells(),
    ]
    validate_catalog(cells)
    return cells


def catalog_fingerprint(cells: Iterable[dict[str, Any]] | None = None) -> str:
    payload = json.dumps(
        list(cells if cells is not None else base_cells()),
        sort_keys=True, separators=(",", ":"), ensure_ascii=True,
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def sequence_fingerprint() -> str:
    payload = json.dumps(
        [template.as_dict() for template in SEQUENCE_TEMPLATES],
        sort_keys=True, separators=(",", ":"), ensure_ascii=True,
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def validate_catalog(cells: list[dict[str, Any]]) -> None:
    counts = Counter(cell["family"] for cell in cells)
    if counts != Counter(BASE_FAMILY_COUNTS):
        raise ValueError(f"V2 catalog counts changed: {dict(counts)}")
    ids = [cell["cell_id"] for cell in cells]
    if len(ids) != 1184 or len(ids) != len(set(ids)):
        raise ValueError("V2 catalog must contain 1,184 unique stable cell IDs")
    for family, expected in BASE_FAMILY_COUNTS.items():
        scenarios = sorted(
            int(cell["scenario_index"]) for cell in cells if cell["family"] == family
        )
        if scenarios != list(range(expected)):
            raise ValueError(f"{family} scenario indices are not contiguous")
    template_ids = [template.template_id for template in SEQUENCE_TEMPLATES]
    if len(template_ids) != len(set(template_ids)):
        raise ValueError("sequence-template IDs must be unique")
    required_profiles = {
        *AIM_ACQUISITION_PROFILES,
        *POST_THROW_CAMERA_PROFILES,
        *Q_RETENTION_PROFILES,
        *POST_THROW_MOVEMENT_PROFILES,
    }
    realized_profiles = {
        value
        for template in SEQUENCE_TEMPLATES
        for values in (
            template.aim_profiles, template.camera_profiles,
            template.q_profiles, template.movement_profiles,
        )
        for value in values
    }
    missing = required_profiles - realized_profiles
    if missing:
        raise ValueError(f"sequence templates omit temporal profiles: {sorted(missing)}")


def audit_slots() -> list[dict[str, Any]]:
    """Return the frozen deterministic 128-slot base visual-audit selection."""
    cells = base_cells()
    by_family = {
        family: [cell for cell in cells if cell["family"] == family]
        for family in BASE_FAMILY_COUNTS
    }
    slots: list[dict[str, Any]] = []
    for index, (_, behavior) in enumerate(RANDOM_FAMILIES):
        cell = by_family["random_play"][index * 4 + index % 4]
        slots.append({"slot_id": f"AUD-R{index + 1:02d}", "family": "random_play", "title": behavior, "cell_id": cell["cell_id"]})
    modes = ("direct_center", "direct_upper", "direct_lower", "glance_left", "glance_right", "floor_bounce_then_contact", "near_miss")
    targets = ("rectangle", "pyramid", "sphere")
    for target_index, target in enumerate(targets):
        for sector_index, sector in enumerate(SECTORS):
            mode = modes[(sector_index + 2 * target_index) % 7]
            distance = DISTANCE_BANDS[(sector_index + target_index) % 3]
            arc = ARC_BANDS[(2 * sector_index + target_index) % 3]
            cell = next(cell for cell in by_family["solid_object"] if cell["target"] == target and cell["approach_sector"] == sector and cell["interaction_mode"] == mode and cell["distance_band"] == distance and cell["arc_band"] == arc)
            slots.append({"slot_id": f"AUD-SO-{target_index}-{sector_index}", "family": "solid_object", "title": f"{target} from {sector}", "cell_id": cell["cell_id"]})
    approaches = ("direct", "glance_left", "glance_right", "near_parallel")
    walls = ("north", "south", "east", "west")
    heights = ("low", "middle", "high")
    sequences = ("wall_first", "floor_then_wall", "wall_then_floor", "adjacent_corner")
    for wall_index, wall in enumerate(walls):
        for approach_index, approach in enumerate(approaches):
            height = heights[(wall_index + 2 * approach_index) % 3]
            sequence = sequences[(wall_index + approach_index) % 4]
            cell = next(cell for cell in by_family["wall_corner"] if cell["wall"] == wall and cell["approach_profile"] == approach and cell["height_band"] == height and cell["contact_sequence"] == sequence)
            slots.append({"slot_id": f"AUD-WA-{wall_index}-{approach_index}", "family": "wall_corner", "title": f"{wall} {approach}", "cell_id": cell["cell_id"]})
    outcomes = ("direct_settle", "one_bounce", "multi_bounce", "long_roll")
    for sector_index, sector in enumerate(SECTORS):
        for outcome_index, outcome in enumerate(outcomes):
            distance = DISTANCE_BANDS[(sector_index + outcome_index) % 3]
            arc = ARC_BANDS[(2 * sector_index + outcome_index) % 3]
            cell = next(cell for cell in by_family["floor_bounce_rest"] if cell["azimuth_sector"] == sector and cell["outcome"] == outcome and cell["distance_band"] == distance and cell["arc_band"] == arc)
            slots.append({"slot_id": f"AUD-FL-{sector_index}-{outcome_index}", "family": "floor_bounce_rest", "title": f"{sector} {outcome}", "cell_id": cell["cell_id"]})
    ramp_approaches = ("uphill", "downhill", "lateral_left_to_right", "lateral_right_to_left")
    ramp_modes = ("direct_surface", "glancing_surface", "floor_then_ramp", "ramp_then_floor", "cross_over", "near_miss_side")
    regions = ("lower", "middle", "upper")
    for approach_index, approach in enumerate(ramp_approaches):
        for mode_index, mode in enumerate(ramp_modes):
            region = regions[(approach_index + mode_index) % 3]
            cell = next(cell for cell in by_family["ramp"] if cell["approach"] == approach and cell["interaction_mode"] == mode and cell["ramp_region"] == region)
            slots.append({"slot_id": f"AUD-RA-{approach_index}-{mode_index}", "family": "ramp", "title": f"{approach} {mode}", "cell_id": cell["cell_id"]})
    directions = ("negative_x_to_positive_x", "positive_x_to_negative_x")
    paths = ("center", "horizontal_left", "horizontal_right", "vertical_above", "vertical_below", "oblique_left_to_right", "oblique_right_to_left")
    hoop_outcomes = ("clean_pass", "rim_contact", "near_miss", "floor_bounce_then_hoop")
    for direction_index, direction in enumerate(directions):
        for path_index, path in enumerate(paths):
            outcome = hoop_outcomes[(direction_index + path_index) % 4]
            cell = next(cell for cell in by_family["hoop"] if cell["direction"] == direction and cell["path_profile"] == path and cell["outcome"] == outcome)
            slots.append({"slot_id": f"AUD-HO-{direction_index}-{path_index}", "family": "hoop", "title": f"{direction} {path}", "cell_id": cell["cell_id"]})
    if len(slots) != 128 or len({slot["slot_id"] for slot in slots}) != 128:
        raise ValueError("visual audit selection must contain 128 unique slots")
    return slots


if __name__ == "__main__":
    catalog = base_cells()
    print(json.dumps({
        "catalog_version": CATALOG_VERSION,
        "sequence_version": SEQUENCE_VERSION,
        "counts": dict(Counter(cell["family"] for cell in catalog)),
        "base_total": len(catalog),
        "sequence_templates": len(SEQUENCE_TEMPLATES),
        "audit_slots": len(audit_slots()),
        "catalog_sha256": catalog_fingerprint(catalog),
        "sequence_sha256": sequence_fingerprint(),
    }, indent=2, sort_keys=True))
