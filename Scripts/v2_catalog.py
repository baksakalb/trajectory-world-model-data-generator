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


CATALOG_VERSION = "trajectory-throw-v2-certified-positive-events-3"
SEQUENCE_VERSION = "trajectory-throw-v2-certified-sequences-2"
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
    "trajectory_view": 64,
    "solid_object": 60,
    "wall_corner": 32,
    "floor_observe": 24,
    "ramp": 24,
    "hoop": 36,
    "temporal": 8,
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
              families=("floor_observe", "floor_observe"),
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
              families=("floor_observe", "floor_observe", "floor_observe"),
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
        "arena_center", "open_corridor",
    )
    modes = ("static_hold", "yaw_adjust", "pitch_adjust", "cancel_reacquire")
    scenario = 0
    for target_index, target in enumerate(targets):
        sectors = ("S0", "S4")
        for sector_index, sector in enumerate(sectors):
            for mode_index, mode in enumerate(modes):
                yield {
                    "source": "mission", "family": "trajectory_view",
                    "cell_id": f"TV-{target}-{sector}-{mode}",
                    "scenario_index": scenario, "target": target,
                    "approach_sector": sector,
                    "interaction_mode": mode,
                    "distance_band": DISTANCE_BANDS[(target_index + mode_index) % 3],
                    "arc_band": ARC_BANDS[(target_index + sector_index + mode_index) % 3],
                }
                scenario += 1


def solid_object_cells() -> Iterator[dict[str, Any]]:
    targets = ("rectangle", "pyramid", "sphere")
    regions = ("center", "upper", "lower", "left_edge", "right_edge")
    # Four well-separated launch sectors plus bounded per-repetition mutations
    # cover all sides without pretending eight exact rays are eight semantics.
    sectors = ("S0", "S2", "S4", "S6")
    scenario = 0
    for target in targets:
        for sector in sectors:
            for region in regions:
                yield {
                    "source": "mission", "family": "solid_object",
                    "cell_id": f"SO-{target}-{sector}-{region}-contact",
                    "scenario_index": scenario, "target": target,
                    "approach_sector": sector, "target_region": region,
                    "interaction_mode": "contact",
                    "distance_band": DISTANCE_BANDS[scenario % 3],
                    # High lob arcs are too pitch-sensitive for robust broad
                    # object-region certification.  They remain covered by
                    # trajectory-view and floor motion; contacts use low/medium.
                    "arc_band": ("low", "medium")[(scenario // 3) % 2],
                }
                scenario += 1


def wall_corner_cells() -> Iterator[dict[str, Any]]:
    walls = ("north", "south", "east", "west")
    heights = ("low", "high")
    intents = (
        "direct_contact", "glance_left", "glance_right",
        "adjacent_wall_bank",
    )
    scenario = 0
    for wall in walls:
        for height in heights:
            for intent in intents:
                    yield {
                        "source": "mission", "family": "wall_corner",
                        "cell_id": f"WA-{wall}-{height}-{intent}",
                        "scenario_index": scenario, "wall": wall,
                        "approach_profile": intent, "height_band": height,
                        "contact_sequence": intent,
                    }
                    scenario += 1


def floor_observe_cells() -> Iterator[dict[str, Any]]:
    scenario = 0
    for sector_index, sector in enumerate(("S0", "S2", "S4", "S6")):
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
    intents = ("surface_contact", "cross_over")
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
    approaches = ("direct", "oblique")
    pass_regions = ("center", "left", "right", "upper", "lower")
    rim_regions = ("upper", "lower", "left", "right")
    scenario = 0
    for direction in directions:
        for approach in approaches:
            for path in pass_regions:
                yield {
                    "source": "mission", "family": "hoop",
                    "cell_id": f"HO-{direction}-{approach}-{path}-clean_pass",
                    "scenario_index": scenario, "direction": direction,
                    "approach_profile": approach,
                    "path_profile": path, "outcome": "clean_pass",
                }
                scenario += 1
            for region in rim_regions:
                yield {
                    "source": "mission", "family": "hoop",
                    "cell_id": f"HO-{direction}-{approach}-{region}-rim_contact",
                    "scenario_index": scenario, "direction": direction,
                    "approach_profile": approach,
                    "path_profile": region, "outcome": "rim_contact",
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
        yield {
            "source": "mission", "family": "temporal",
            "cell_id": f"TP-{behavior}",
            "scenario_index": scenario, "behavior_family": behavior,
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
    """Return one human-review slot per meaningfully distinct positive event.

    Direction, distance, and bounded geometric mutations are distributed across
    the slots.  Misses are natural semi-Markov observations and never receive a
    prescribed label or an audit quota.
    """
    cells = base_cells()
    by_family = {
        family: [cell for cell in cells if cell["family"] == family]
        for family in BASE_FAMILY_COUNTS
    }

    def choose(family: str, **criteria: Any) -> dict[str, Any]:
        matches = [
            cell for cell in by_family[family]
            if all(cell.get(key) == value for key, value in criteria.items())
        ]
        if not matches:
            raise ValueError(f"no {family} audit cell matches {criteria}")
        return matches[0]

    slots: list[dict[str, Any]] = []

    def add(family: str, cell: dict[str, Any], purpose: str) -> None:
        ordinal = sum(slot["family"] == family for slot in slots)
        slots.append({
            "slot_id": f"AUD-{family.upper()}-{ordinal:02d}",
            "family": family,
            "title": purpose,
            "audit_purpose": purpose,
            "cell_id": cell["cell_id"],
        })

    # 12 persistent-play openings. The two-to-three-minute controller after
    # the opening is shared, so duration bands are rotated rather than crossed.
    for index, behavior in enumerate(SEMI_MARKOV_FAMILIES):
        band = HOLD_BANDS[index % len(HOLD_BANDS)]
        add("semi_markov", choose(
            "semi_markov", behavior_family=behavior, hold_band=band,
        ), f"Persistent play opening: {behavior}; {band} episode")

    # Every target context and every preview gesture is represented without a
    # Cartesian target x gesture review explosion.
    trajectory_targets = (
        "rectangle", "pyramid", "sphere", "ramp", "hoop", "floor",
        "arena_center", "open_corridor",
    )
    trajectory_modes = (
        "static_hold", "yaw_adjust", "pitch_adjust", "cancel_reacquire",
    )
    for index, target in enumerate(trajectory_targets):
        mode = trajectory_modes[index % len(trajectory_modes)]
        sector = ("S0", "S4")[index % 2]
        add("trajectory_view", choose(
            "trajectory_view", target=target, interaction_mode=mode,
            approach_sector=sector,
        ), f"Trajectory view of {target}: {mode}")
    for index, mode in enumerate(trajectory_modes):
        target = ("rectangle", "ramp", "hoop", "open_corridor")[index]
        sector = ("S4", "S0")[index % 2]
        add("trajectory_view", choose(
            "trajectory_view", target=target, interaction_mode=mode,
            approach_sector=sector,
        ), f"Trajectory gesture control: {mode}; {target}")

    # Audit each object x broad contact region.  Off-center contacts create
    # varied bounce angles without inventing a fragile "glance" class.
    objects = ("rectangle", "pyramid", "sphere")
    regions = ("center", "upper", "lower", "left_edge", "right_edge")
    for target in objects:
        for region in regions:
            add("solid_object", choose(
                "solid_object", target=target, interaction_mode="contact",
                target_region=region,
            ), f"{target} contact at broad {region} region")

    wall_intents = (
        "direct_contact", "glance_left", "glance_right",
        "adjacent_wall_bank",
    )
    walls = ("north", "south", "east", "west")
    for intent_index, intent in enumerate(wall_intents):
        for height_index, height in enumerate(("low", "high")):
            wall = walls[(intent_index + 2 * height_index) % len(walls)]
            add("wall_corner", choose(
                "wall_corner", wall=wall,
                height_band=height, contact_sequence=intent,
            ), f"Wall trajectory: {intent}; {wall}; {height}")

    for distance_index, distance in enumerate(DISTANCE_BANDS):
        for arc_index, arc in enumerate(("low", "high")):
            sector = ("S0", "S2", "S4", "S6")[(distance_index + arc_index) % 4]
            add("floor_observe", choose(
                "floor_observe", azimuth_sector=sector,
                distance_band=distance, arc_band=arc,
            ), f"Natural floor motion: {distance} distance, {arc} arc")

    ramp_approaches = (
        "uphill", "downhill", "lateral_left_to_right",
        "lateral_right_to_left",
    )
    ramp_intents = ("surface_contact", "cross_over")
    ramp_regions = ("lower", "middle", "upper")
    for approach_index, approach in enumerate(ramp_approaches):
        for intent_index, intent in enumerate(ramp_intents):
            region = ramp_regions[(approach_index + intent_index) % len(ramp_regions)]
            add("ramp", choose(
                "ramp", approach=approach, ramp_region=region,
                interaction_mode=intent,
            ), f"Ramp {approach}: {intent}; {region} region")

    hoop_pass_regions = ("center", "left", "right", "upper", "lower")
    hoop_rim_regions = ("upper", "lower", "left", "right")
    hoop_approaches = ("direct", "oblique")
    hoop_directions = ("negative_x_to_positive_x", "positive_x_to_negative_x")
    for approach_index, approach in enumerate(hoop_approaches):
        for path_index, path in enumerate(hoop_pass_regions):
            direction = hoop_directions[(approach_index + path_index) % 2]
            add("hoop", choose(
                "hoop", direction=direction, approach_profile=approach,
                path_profile=path, outcome="clean_pass",
            ), f"Hoop clean pass: {path}; {approach}; {direction}")
        for region_index, region in enumerate(hoop_rim_regions):
            direction = hoop_directions[(approach_index + region_index) % 2]
            add("hoop", choose(
                "hoop", direction=direction, approach_profile=approach,
                path_profile=region, outcome="rim_contact",
            ), f"Hoop rim contact: {region}; {approach}; {direction}")

    temporal_behaviors = (
        "preview_cancel", "preview_adjust", "throw_release", "throw_retain",
        "throw_move", "throw_relocate_reacquire", "cooldown_recovery",
        "persistent_revisit",
    )
    for index, behavior in enumerate(temporal_behaviors):
        add("temporal", choose(
            "temporal", behavior_family=behavior,
        ), f"Temporal behavior: {behavior}")

    oob_profiles = (
        ("north", "direct", "middle"),
        ("south", "oblique", "high"),
        ("east", "direct", "high"),
        ("west", "oblique", "middle"),
    )
    for boundary, angle, height in oob_profiles:
        add("out_of_bounds", choose(
            "out_of_bounds", boundary=boundary,
            approach_profile=angle, height_band=height,
        ), f"Deliberate {boundary} exit: {angle}, {height}")

    # Connected sequences are semantic audit slots in their own right. Their
    # first base cell is recorded so the qualification planner can materialize
    # the executable sequence deterministically.
    cell_lookup = {cell["cell_id"]: cell for cell in cells}
    family_pattern_first = {
        family: next(cell for cell in cells if cell["family"] == family)
        for family in MISSION_FAMILY_COUNTS
    }
    for template in SEQUENCE_TEMPLATES:
        first = family_pattern_first[template.base_family_pattern[0]]
        slots.append({
            "slot_id": f"AUD-SEQUENCE-{template.template_id}",
            "family": "sequence",
            "title": f"Connected sequence: {template.slug}",
            "audit_purpose": f"Connected sequence: {template.slug}",
            "cell_id": cell_lookup[first["cell_id"]]["cell_id"],
            "sequence_template_id": template.template_id,
        })

    if len(slots) != 99:
        raise ValueError(f"positive-event audit must contain exactly 99 slots, got {len(slots)}")
    if len({slot["cell_id"] for slot in slots if "sequence_template_id" not in slot}) != 91:
        raise ValueError("positive-event audit base-cell selections must be unique")
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
