#!/usr/bin/env python3
"""Coverage-first catalog for the canonical-physics curriculum.

The catalog prescribes only observable initial conditions, action semantics, and
rare geometry.  It never prescribes a grenade restitution, damping, speed,
bounce count, roll distance, or settling style.  Those are realized outcomes
of one immutable simulator and are recorded after the throw.
"""

from __future__ import annotations

import hashlib
import json
from collections import Counter
from dataclasses import dataclass
from typing import Any, Iterable, Iterator


CATALOG_VERSION = "trajectory-throw-v2-persistent-semi-markov-catalog-2"
SEQUENCE_VERSION = "trajectory-throw-v2-canonical-sequences-1"
HOLD_BANDS = ("short", "medium", "long", "very_long")
SECTORS = tuple(f"S{index}" for index in range(8))
DISTANCE_BANDS = ("near", "medium", "far")
ARC_BANDS = ("low", "medium", "high")

# Reset-rich local behavior.  These labels describe controllable action
# coverage, not physical outcomes.
SEMI_MARKOV_FAMILIES = (
    "idle_transition", "forward_reverse", "strafe_transition",
    "diagonal_transition", "camera_yaw", "camera_pitch",
    "movement_camera", "collision_escape", "center_return",
    "wall_parallel", "trajectory_cycle", "stress_combinations",
)

MISSION_FAMILY_COUNTS = {
    "trajectory_view": 96,
    "solid_object": 144,
    "wall_corner": 64,
    "floor_observe": 48,
    "ramp": 48,
    "hoop": 40,
    "temporal": 16,
    "out_of_bounds": 16,
}
BASE_FAMILY_COUNTS = {
    "semi_markov": len(SEMI_MARKOV_FAMILIES) * len(HOLD_BANDS),
    **MISSION_FAMILY_COUNTS,
}

AIM_ACQUISITION_PROFILES = (
    "static_hold", "prelook_q_off", "yaw_adjust", "pitch_adjust",
    "combined_adjust", "overshoot_correct", "cancel_reacquire",
)
POST_THROW_CAMERA_PROFILES = (
    "landing_region", "target_region", "crossing_corridor", "exit_direction",
    "ordinary_survey",
)
Q_RETENTION_PROFILES = (
    "immediate_release", "short_retain", "cooldown_long_retain",
)
POST_THROW_MOVEMENT_PROFILES = (
    "stationary", "forward", "backward", "strafe_left", "strafe_right",
    "mirrored_diagonal", "ordinary_semi_markov",
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
    camera: tuple[str, ...] = ("target_region",),
    families: tuple[str, ...] = ("solid_object",),
    reaim: bool = False,
    recovery: bool = False,
    relocation: bool = False,
) -> SequenceTemplate:
    def expand(values: tuple[str, ...]) -> tuple[str, ...]:
        return tuple(values[index % len(values)] for index in range(grenade_count))

    return SequenceTemplate(
        template_id=f"SQ{ordinal:02d}", slug=slug, grenade_count=grenade_count,
        aim_profiles=expand(aims), q_profiles=expand(q),
        movement_profiles=expand(movement), camera_profiles=expand(camera),
        base_family_pattern=expand(families),
        requires_reaim_difference=reaim,
        cooldown_rejection_recovery=recovery,
        natural_relocation=relocation,
        minimum_camera_delta_degrees=2.5 if reaim else 0.0,
        minimum_trajectory_pixel_change_ratio=0.015 if reaim else 0.0,
    )


# Bounded temporal coverage.  These sequences prescribe actions and connected
# intent; each physical throw still uses the same canonical configuration.
SEQUENCE_TEMPLATES = (
    _sequence(1, "preview_throw_release", 1),
    _sequence(2, "preview_adjust_throw", 1, aims=("combined_adjust",)),
    _sequence(3, "preview_cancel_reacquire_throw", 1,
              aims=("cancel_reacquire",), camera=("ordinary_survey",)),
    _sequence(4, "throw_relocate_reacquire", 2,
              aims=("static_hold", "prelook_q_off"),
              movement=("relocation_before_next_throw",),
              camera=("target_region", "ordinary_survey"),
              reaim=True, relocation=True),
    _sequence(5, "mixed_q_retention", 2,
              q=("immediate_release", "short_retain"), reaim=True),
    _sequence(6, "cooldown_rejection_recovery", 2,
              q=("cooldown_long_retain", "immediate_release"),
              reaim=True, recovery=True),
    _sequence(7, "two_grenade_persistence", 2,
              families=("floor_observe", "solid_object"), reaim=True),
    _sequence(8, "three_grenade_connected_play", 3,
              aims=("static_hold", "yaw_adjust", "pitch_adjust"),
              movement=("forward", "strafe_right", "backward"),
              camera=("landing_region", "target_region", "ordinary_survey"),
              families=("solid_object", "wall_corner", "floor_observe"),
              reaim=True, relocation=True),
)


def semi_markov_cells() -> Iterator[dict[str, Any]]:
    scenario = 0
    for behavior in SEMI_MARKOV_FAMILIES:
        for hold_band in HOLD_BANDS:
            yield {
                "source": "semi_markov", "family": "semi_markov",
                "cell_id": f"SM-{behavior}-{hold_band}",
                "scenario_index": scenario, "behavior_family": behavior,
                "hold_band": hold_band,
            }
            scenario += 1


def trajectory_view_cells() -> Iterator[dict[str, Any]]:
    targets = (
        "rectangle", "pyramid", "sphere", "ramp", "hoop", "floor",
        "north_boundary", "south_boundary", "east_boundary", "west_boundary",
        "arena_center", "open_corridor",
    )
    modes = ("static_hold", "yaw_adjust", "pitch_adjust", "cancel_reacquire")
    scenario = 0
    for target_index, target in enumerate(targets):
        for sector_index in range(2):
            for mode_index, mode in enumerate(modes):
                yield {
                    "source": "mission", "family": "trajectory_view",
                    "cell_id": f"TV-{target}-S{sector_index * 4}-{mode}",
                    "scenario_index": scenario, "target": target,
                    "approach_sector": f"S{sector_index * 4}",
                    "interaction_mode": mode,
                    "distance_band": DISTANCE_BANDS[(target_index + mode_index) % 3],
                    "arc_band": ARC_BANDS[(target_index + sector_index + mode_index) % 3],
                }
                scenario += 1


def solid_object_cells() -> Iterator[dict[str, Any]]:
    targets = ("rectangle", "pyramid", "sphere")
    regions = ("center", "upper", "lower")
    intents = ("direct", "glance", "near_miss")
    scenario = 0
    for target_index, target in enumerate(targets):
        for sector_index, sector in enumerate(SECTORS):
            for region_index, region in enumerate(regions):
                for intent_index in range(2):
                    intent = intents[(sector_index + region_index + intent_index) % 3]
                    yield {
                        "source": "mission", "family": "solid_object",
                        "cell_id": f"SO-{target}-{sector}-{region}-{intent}-v{intent_index}",
                        "scenario_index": scenario, "target": target,
                        "approach_sector": sector, "target_region": region,
                        "interaction_mode": intent,
                        "distance_band": DISTANCE_BANDS[(sector_index + region_index + intent_index) % 3],
                        "arc_band": ARC_BANDS[(2 * sector_index + region_index + intent_index) % 3],
                    }
                    scenario += 1


def wall_corner_cells() -> Iterator[dict[str, Any]]:
    walls = ("north", "south", "east", "west")
    approaches = ("direct", "glance_left", "glance_right", "near_parallel")
    heights = ("low", "high")
    intents = ("wall_contact", "corner_or_miss")
    scenario = 0
    for wall in walls:
        for approach in approaches:
            for height in heights:
                for intent in intents:
                    yield {
                        "source": "mission", "family": "wall_corner",
                        "cell_id": f"WA-{wall}-{approach}-{height}-{intent}",
                        "scenario_index": scenario, "wall": wall,
                        "approach_profile": approach, "height_band": height,
                        "contact_sequence": intent,
                    }
                    scenario += 1


def floor_observe_cells() -> Iterator[dict[str, Any]]:
    scenario = 0
    for sector_index, sector in enumerate(SECTORS):
        for distance_index, distance in enumerate(DISTANCE_BANDS):
            for arc_index, arc in enumerate(("low", "high")):
                yield {
                    "source": "mission", "family": "floor_observe",
                    "cell_id": f"FL-{sector}-{distance}-{arc}-observe",
                    "scenario_index": scenario, "azimuth_sector": sector,
                    "distance_band": distance, "arc_band": arc,
                    "outcome": "natural_floor_motion",
                }
                scenario += 1


def ramp_cells() -> Iterator[dict[str, Any]]:
    approaches = (
        "uphill", "downhill", "lateral_left_to_right",
        "lateral_right_to_left",
    )
    regions = ("lower", "middle", "upper")
    intents = ("surface_contact", "glancing_contact", "cross_over", "side_miss")
    scenario = 0
    for approach in approaches:
        for region in regions:
            for intent in intents:
                yield {
                    "source": "mission", "family": "ramp",
                    "cell_id": f"RA-{approach}-{region}-{intent}",
                    "scenario_index": scenario, "approach": approach,
                    "ramp_region": region, "interaction_mode": intent,
                }
                scenario += 1


def hoop_cells() -> Iterator[dict[str, Any]]:
    directions = ("negative_x_to_positive_x", "positive_x_to_negative_x")
    paths = ("center", "horizontal_left", "horizontal_right", "vertical", "oblique")
    intents = ("clean_pass", "rim_contact", "near_miss", "open_path")
    scenario = 0
    for direction in directions:
        for path in paths:
            for intent in intents:
                yield {
                    "source": "mission", "family": "hoop",
                    "cell_id": f"HO-{direction}-{path}-{intent}",
                    "scenario_index": scenario, "direction": direction,
                    "path_profile": path, "outcome": intent,
                }
                scenario += 1


def temporal_cells() -> Iterator[dict[str, Any]]:
    behaviors = (
        "preview_cancel", "preview_adjust", "throw_release", "throw_retain",
        "throw_move", "throw_relocate_reacquire", "cooldown_recovery",
        "persistent_revisit",
    )
    scenario = 0
    for behavior in behaviors:
        for variation in range(2):
            yield {
                "source": "mission", "family": "temporal",
                "cell_id": f"TP-{behavior}-v{variation}",
                "scenario_index": scenario, "behavior_family": behavior,
                "variation": variation,
            }
            scenario += 1


def out_of_bounds_cells() -> Iterator[dict[str, Any]]:
    for scenario, (boundary, angle, height) in enumerate(
        (boundary, angle, height)
        for boundary in ("north", "south", "east", "west")
        for angle in ("direct", "oblique")
        for height in ("middle", "high")
    ):
        yield {
            "source": "mission", "family": "out_of_bounds",
            "cell_id": f"OOB-{boundary}-{angle}-{height}",
            "scenario_index": scenario, "boundary": boundary,
            "approach_profile": angle, "height_band": height,
        }


def base_cells() -> list[dict[str, Any]]:
    cells = [
        *semi_markov_cells(), *trajectory_view_cells(),
        *solid_object_cells(), *wall_corner_cells(), *floor_observe_cells(),
        *ramp_cells(), *hoop_cells(), *temporal_cells(),
        *out_of_bounds_cells(),
    ]
    validate_catalog(cells)
    return cells


def catalog_fingerprint(cells: Iterable[dict[str, Any]] | None = None) -> str:
    payload = json.dumps(
        list(cells if cells is not None else base_cells()), sort_keys=True,
        separators=(",", ":"), ensure_ascii=True,
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def sequence_fingerprint() -> str:
    payload = json.dumps(
        [template.as_dict() for template in SEQUENCE_TEMPLATES], sort_keys=True,
        separators=(",", ":"), ensure_ascii=True,
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def validate_catalog(cells: list[dict[str, Any]]) -> None:
    counts = Counter(cell["family"] for cell in cells)
    if counts != Counter(BASE_FAMILY_COUNTS):
        raise ValueError(f"V2 canonical catalog counts changed: {dict(counts)}")
    ids = [cell["cell_id"] for cell in cells]
    if len(ids) != len(set(ids)):
        raise ValueError("V2 canonical catalog cell IDs must be unique")
    for family, expected in BASE_FAMILY_COUNTS.items():
        scenarios = sorted(
            int(cell["scenario_index"]) for cell in cells
            if cell["family"] == family
        )
        if scenarios != list(range(expected)):
            raise ValueError(f"{family} scenario indices are not contiguous")
    template_ids = [template.template_id for template in SEQUENCE_TEMPLATES]
    if len(template_ids) != len(set(template_ids)):
        raise ValueError("sequence-template IDs must be unique")


def audit_slots() -> list[dict[str, Any]]:
    """Small deterministic structural audit; named failures are added separately."""
    cells = base_cells()
    grouped = {
        family: [cell for cell in cells if cell["family"] == family]
        for family in BASE_FAMILY_COUNTS
    }
    requested = {
        "semi_markov": 12, "trajectory_view": 12,
        "solid_object": 12, "wall_corner": 8, "floor_observe": 8,
        "ramp": 8, "hoop": 8, "temporal": 8, "out_of_bounds": 8,
    }
    slots: list[dict[str, Any]] = []
    for family, count in requested.items():
        values = grouped[family]
        for ordinal in range(count):
            index = 0 if count == 1 else round(ordinal * (len(values) - 1) / (count - 1))
            cell = values[index]
            slots.append({
                "slot_id": f"AUD-{family.upper()}-{ordinal:02d}",
                "family": family, "title": cell["cell_id"],
                "cell_id": cell["cell_id"],
            })
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
