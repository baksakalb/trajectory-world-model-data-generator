#!/usr/bin/env python3
"""Validate a generated shard and build review MP4s from its saved RGB frames.

The converter never renders Unreal or captures the screen. Every MP4 frame is
read directly from the authoritative tar shard in recorded frame-index order.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import shutil
import struct
import subprocess
import sys
import tarfile
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, BinaryIO


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class DatasetValidationError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate a generated dataset and derive exact review MP4s."
    )
    parser.add_argument("dataset", type=Path, help="Directory containing dataset.json")
    parser.add_argument(
        "--output",
        type=Path,
        help="Review directory (default: <dataset>/review)",
    )
    parser.add_argument(
        "--ffmpeg",
        type=Path,
        help="Path to ffmpeg.exe; otherwise ffmpeg is resolved from PATH.",
    )
    parser.add_argument(
        "--episode",
        action="append",
        help="Only render this episode ID; may be supplied more than once.",
    )
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="Validate without requiring ffmpeg or creating MP4s.",
    )
    return parser.parse_args()


def read_json_lines(tar: tarfile.TarFile, name: str) -> list[dict[str, Any]]:
    extracted = tar.extractfile(name)
    if extracted is None:
        raise DatasetValidationError(f"Missing required metadata entry: {name}")

    records: list[dict[str, Any]] = []
    for line_number, raw_line in enumerate(extracted, start=1):
        if not raw_line.strip():
            continue
        try:
            records.append(json.loads(raw_line))
        except json.JSONDecodeError as error:
            raise DatasetValidationError(
                f"{name}:{line_number}: invalid JSON: {error}"
            ) from error
    return records


def read_parquet_records(
    tar: tarfile.TarFile, name: str, *, episodes: bool = False
) -> list[dict[str, Any]]:
    try:
        import pyarrow as pa
        import pyarrow.parquet as pq
    except ImportError as error:
        raise DatasetValidationError(
            "PyArrow is required to validate Parquet metadata. Install "
            "Scripts/requirements-production.txt."
        ) from error

    extracted = tar.extractfile(name)
    if extracted is None:
        raise DatasetValidationError(f"Missing required metadata entry: {name}")
    try:
        records = pq.read_table(pa.BufferReader(extracted.read())).to_pylist()
    except (pa.ArrowException, OSError) as error:
        raise DatasetValidationError(f"Could not read {name}: {error}") from error
    if episodes:
        for record in records:
            try:
                record["mission_parameters"] = json.loads(
                    record.pop("mission_parameters_json")
                )
            except (KeyError, json.JSONDecodeError) as error:
                raise DatasetValidationError(
                    f"{name}: invalid mission_parameters_json"
                ) from error
    return records


def md5_file(path: Path) -> str:
    digest = hashlib.md5(usedforsecurity=False)
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_checksum(dataset_dir: Path, shard_path: Path) -> str:
    checksum_path = dataset_dir / "checksums.md5"
    if not checksum_path.is_file():
        raise DatasetValidationError(f"Missing checksum file: {checksum_path}")

    expected_by_name: dict[str, str] = {}
    for line in checksum_path.read_text(encoding="utf-8").splitlines():
        fields = line.strip().split(maxsplit=1)
        if len(fields) == 2:
            expected_by_name[fields[1].lstrip("*")] = fields[0].lower()

    expected = expected_by_name.get(shard_path.name)
    if not expected:
        raise DatasetValidationError(
            f"No checksum entry found for shard {shard_path.name}"
        )
    actual = md5_file(shard_path)
    if actual != expected:
        raise DatasetValidationError(
            f"Checksum mismatch for {shard_path.name}: {actual} != {expected}"
        )
    return actual


def png_dimensions(stream: BinaryIO) -> tuple[int, int]:
    header = stream.read(24)
    if len(header) != 24 or header[:8] != PNG_SIGNATURE or header[12:16] != b"IHDR":
        raise DatasetValidationError("Observation is not a valid PNG stream.")
    return struct.unpack(">II", header[16:24])


def image_dimensions(stream: BinaryIO, rgb_format: str) -> tuple[int, int]:
    if rgb_format == "lossless_png":
        return png_dimensions(stream)
    if rgb_format == "lossless_webp":
        try:
            from PIL import Image
        except ImportError as error:
            raise DatasetValidationError(
                "Pillow is required to validate WebP observations."
            ) from error
        try:
            with Image.open(io.BytesIO(stream.read())) as image:
                if image.format != "WEBP":
                    raise DatasetValidationError(
                        "Observation is not a valid WebP stream."
                    )
                return image.size
        except OSError as error:
            raise DatasetValidationError(
                f"Observation is not a valid WebP stream: {error}"
            ) from error
    raise DatasetValidationError(f"Unsupported RGB format: {rgb_format}")


def validate_final_agent_mission(
    episode: dict[str, Any],
    episode_frames: list[dict[str, Any]],
    *,
    strict_v8: bool = False,
    strict_v9: bool = False,
    strict_v10: bool = False,
    observation_rate_hz: int = 20,
) -> None:
    """Validate recorded success against the finalized mission contract."""
    episode_id = str(episode["episode_id"])
    mission = episode.get("collection_mission")
    known_missions = {
        "semi_markov",
        "object_view",
        "contact_recovery",
        "ramp_traverse",
        "hoop_pass",
    }
    if mission not in known_missions:
        raise DatasetValidationError(f"{episode_id}: unknown mission {mission!r}.")

    mission_required = bool(episode.get("mission_required"))
    mission_success = bool(episode.get("mission_success"))
    termination_reason = episode.get("termination_reason")
    if mission == "semi_markov":
        if mission_required or termination_reason != "completed":
            raise DatasetValidationError(
                f"{episode_id}: invalid semi-Markov termination contract."
            )
        if strict_v10:
            if any(
                frame.get("mission_phase") != "semi_markov"
                or frame.get("mission_success")
                or int(frame.get("required_post_success_steps", 0)) != 0
                for frame in episode_frames
            ):
                raise DatasetValidationError(
                    f"{episode_id}: invalid semi-Markov phase/post-success metadata."
                )
            current_contact_run = 0
            maximum_contact_run = 0
            for frame in episode_frames:
                if frame.get("contact"):
                    current_contact_run += 1
                    recorded_run = int(frame.get("natural_play_contact_steps", -1))
                    limit = int(
                        frame.get("natural_play_contact_limit_steps", -1)
                    )
                    if recorded_run != current_contact_run or not 4 <= limit <= 10:
                        raise DatasetValidationError(
                            f"{episode_id}: inconsistent natural-play contact "
                            f"state on frame {frame.get('frame_index')}."
                        )
                    maximum_contact_run = max(maximum_contact_run, current_contact_run)
                else:
                    current_contact_run = 0
                    if (
                        int(frame.get("natural_play_contact_steps", -1)) != 0
                        or int(
                            frame.get("natural_play_contact_limit_steps", -1)
                        )
                        != 0
                    ):
                        raise DatasetValidationError(
                            f"{episode_id}: contact counters did not reset."
                        )
            recorded_maximum = int(
                episode.get("maximum_consecutive_contact_steps", -1)
            )
            if recorded_maximum != maximum_contact_run:
                raise DatasetValidationError(
                    f"{episode_id}: maximum contact run {recorded_maximum} "
                    f"!= realized {maximum_contact_run}."
                )
            if recorded_maximum > max(1, observation_rate_hz):
                raise DatasetValidationError(
                    f"{episode_id}: natural play remained in contact for "
                    f"{recorded_maximum} frames (>1 second)."
                )
        return

    if not mission_required:
        raise DatasetValidationError(
            f"{episode_id}: guided mission is not marked required."
        )
    if mission_success and termination_reason != "mission_success":
        raise DatasetValidationError(
            f"{episode_id}: successful mission has termination "
            f"{termination_reason!r}."
        )
    if not mission_success:
        if termination_reason not in {"mission_timeout", "mission_no_progress"}:
            raise DatasetValidationError(
                f"{episode_id}: failed mission has termination "
                f"{termination_reason!r}."
            )
        if strict_v8 and episode.get("accepted_for_balancing"):
            raise DatasetValidationError(
                f"{episode_id}: failed mission advanced balancing counters."
            )
        if strict_v10 and (
            episode.get("mission_success_frame_index") is not None
            or int(episode.get("post_success_observation_frames", 0)) != 0
            or int(episode.get("post_success_steps", 0)) != 0
        ):
            raise DatasetValidationError(
                f"{episode_id}: failed mission contains a post-success rollout."
            )
        return

    if not episode_frames or not episode_frames[-1].get("mission_success"):
        raise DatasetValidationError(
            f"{episode_id}: final observation does not record mission success."
        )
    if strict_v8:
        if not episode.get("primary_objective_achieved"):
            raise DatasetValidationError(
                f"{episode_id}: success was recorded before the primary objective."
            )
        required_tail = int(episode.get("required_post_objective_steps", 0))
        achieved_tail = int(episode.get("post_objective_steps", 0))
        if required_tail <= 0 or achieved_tail < required_tail:
            raise DatasetValidationError(
                f"{episode_id}: post-objective tail "
                f"{achieved_tail} < {required_tail}."
            )
        if not episode.get("accepted_for_balancing"):
            raise DatasetValidationError(
                f"{episode_id}: successful mission was not accepted for balancing."
            )

    if strict_v10:
        success_frame_index = int(episode.get("mission_success_frame_index", -1))
        if success_frame_index <= 0 or success_frame_index >= len(episode_frames) - 1:
            raise DatasetValidationError(
                f"{episode_id}: success frame {success_frame_index} does not "
                "leave a post-success rollout."
            )
        required_rollout = int(episode.get("required_post_success_steps", 0))
        achieved_rollout = int(episode.get("post_success_steps", 0))
        recorded_rollout_frames = int(
            episode.get("post_success_observation_frames", -1)
        )
        minimum_rollout = max(1, round(observation_rate_hz * 0.75))
        maximum_rollout = max(minimum_rollout, round(observation_rate_hz * 1.50))
        if not minimum_rollout <= required_rollout <= maximum_rollout:
            raise DatasetValidationError(
                f"{episode_id}: post-success requirement {required_rollout} "
                f"is outside {minimum_rollout}-{maximum_rollout} frames."
            )
        realized_rollout = len(episode_frames) - 1 - success_frame_index
        if (
            achieved_rollout != required_rollout
            or recorded_rollout_frames != required_rollout
            or realized_rollout != required_rollout
            or int(episode.get("mission_observation_frames", 0))
            != success_frame_index + 1
        ):
            raise DatasetValidationError(
                f"{episode_id}: post-success rollout metadata disagree "
                f"(required={required_rollout}, achieved={achieved_rollout}, "
                f"frames={recorded_rollout_frames}, realized={realized_rollout})."
            )
        known_rollout_styles = {
            "continue",
            "gentle_turn",
            "glance_reacquire",
            "strafe_blend",
            "ease_and_observe",
            "drift_and_settle",
        }
        rollout_style = episode.get("post_success_style")
        if rollout_style not in known_rollout_styles:
            raise DatasetValidationError(
                f"{episode_id}: unknown post-success style {rollout_style!r}."
            )

        frozen_fields = (
            "visited_azimuth_bins_mask",
            "visible_azimuth_bins_mask",
            "visible_hold_steps",
            "ramp_traversals",
            "hoop_passes",
            "contact_hold_steps",
            "verified_contact_steps",
            "recovery_steps",
            "primary_objective_achieved",
            "post_objective_steps",
            "facing_moving_frames",
            "facing_matched_frames",
            "hoop_crossing_recorded",
            "hoop_crossing_y",
            "hoop_crossing_z",
        )
        success_frame = episode_frames[success_frame_index]
        for frame_index, frame in enumerate(episode_frames):
            expected_success = frame_index >= success_frame_index
            expected_phase = (
                "success"
                if frame_index == success_frame_index
                else "post_success"
                if frame_index > success_frame_index
                else None
            )
            expected_post_step = max(0, frame_index - success_frame_index)
            if (
                bool(frame.get("mission_success")) != expected_success
                or (
                    expected_phase is not None
                    and frame.get("mission_phase") != expected_phase
                )
                or (
                    expected_phase is None
                    and frame.get("mission_phase")
                    not in {"objective", "completion_hold"}
                )
                or int(frame.get("post_success_steps", -1))
                != expected_post_step
                or int(frame.get("required_post_success_steps", 0))
                != required_rollout
                or frame.get("post_success_style") != rollout_style
                or frame.get("mission_success_frame_index")
                != (success_frame_index if expected_success else None)
            ):
                raise DatasetValidationError(
                    f"{episode_id}: invalid success latch/phase metadata "
                    f"on frame {frame_index}."
                )
            if frame_index > success_frame_index and any(
                frame.get(field) != success_frame.get(field)
                for field in frozen_fields
            ):
                raise DatasetValidationError(
                    f"{episode_id}: mission counters changed during "
                    f"post-success frame {frame_index}."
                )

    parameters = episode.get("mission_parameters") or {}
    if mission == "object_view":
        mode = episode.get("object_view_mode")
        gaze_pattern = episode.get("object_gaze_pattern")
        known_gaze_patterns = {
            "target_center",
            "target_offset",
            "travel_direction",
            "roam_reacquire",
        }
        if gaze_pattern is not None:
            if gaze_pattern not in known_gaze_patterns:
                raise DatasetValidationError(
                    f"{episode_id}: unknown object gaze pattern {gaze_pattern!r}."
                )
            if parameters.get("gaze_pattern") != gaze_pattern:
                raise DatasetValidationError(
                    f"{episode_id}: episode and mission-parameter gaze patterns "
                    "do not match."
                )
            gaze_plan = parameters.get("gaze_plan")
            if not isinstance(gaze_plan, list) or not gaze_plan:
                raise DatasetValidationError(
                    f"{episode_id}: object gaze plan is missing or empty."
                )
            known_gaze_intents = {
                "target_center",
                "target_offset",
                "travel_direction",
                "survey_point",
            }
            for frame in episode_frames:
                if frame.get("object_gaze_pattern") != gaze_pattern:
                    raise DatasetValidationError(
                        f"{episode_id}: frame gaze pattern does not match episode."
                    )
                if frame.get("object_gaze_intent") not in known_gaze_intents:
                    raise DatasetValidationError(
                        f"{episode_id}: frame has an unknown gaze intent."
                    )
        if mode in {"approach_observe", "pass_by"}:
            required_hold = int(parameters.get("required_visible_hold_steps", 0))
            achieved_hold = int(episode.get("visible_hold_steps", 0))
            if required_hold <= 0 or achieved_hold < required_hold:
                raise DatasetValidationError(
                    f"{episode_id}: visible hold {achieved_hold} < {required_hold}."
                )
        elif mode in {"partial_orbit", "full_orbit"}:
            if strict_v8:
                orbit_direction = episode.get("orbit_direction")
                if orbit_direction not in {"clockwise", "counter_clockwise"}:
                    raise DatasetValidationError(
                        f"{episode_id}: orbit direction is missing or invalid."
                    )
                if parameters.get("orbit_direction") != orbit_direction:
                    raise DatasetValidationError(
                        f"{episode_id}: episode and parameter "
                        "orbit directions differ."
                    )
            required_bins = int(
                episode.get(
                    "required_azimuth_bin_count",
                    episode.get("required_visible_bin_count", 0),
                )
            )
            achieved_bins = int(
                episode.get(
                    "visited_azimuth_bin_count",
                    episode.get("visible_azimuth_bin_count", 0),
                )
            )
            required_mask = int(episode.get("required_azimuth_bins_mask", 0))
            visited_mask = int(
                episode.get(
                    "visited_azimuth_bins_mask",
                    episode.get("visible_azimuth_bins_mask", 0),
                )
            )
            expected_bins = 12 if mode == "full_orbit" else required_bins
            if (
                required_bins != expected_bins
                or achieved_bins < required_bins
                or required_mask == 0
                or (visited_mask & required_mask) != required_mask
            ):
                raise DatasetValidationError(
                    f"{episode_id}: visited bins {achieved_bins} do not satisfy "
                    f"{mode} requirement {required_bins} / mask {required_mask}."
                )
            if strict_v8:
                waypoint_count = int(parameters.get("waypoint_count", 0))
                expected_waypoints = 13 if mode == "full_orbit" else required_bins
                if waypoint_count != expected_waypoints:
                    raise DatasetValidationError(
                        f"{episode_id}: {mode} has {waypoint_count} waypoints, "
                        f"expected {expected_waypoints}."
                    )
        else:
            raise DatasetValidationError(
                f"{episode_id}: unknown object-view mode {mode!r}."
            )
    elif mission == "contact_recovery":
        if strict_v8:
            recovery_style = episode.get("contact_recovery_style")
            if recovery_style not in {
                "backward",
                "strafe_left",
                "strafe_right",
                "diagonal_left",
                "diagonal_right",
            }:
                raise DatasetValidationError(
                    f"{episode_id}: invalid contact recovery style "
                    f"{recovery_style!r}."
                )
            if parameters.get("recovery_style") != recovery_style:
                raise DatasetValidationError(
                    f"{episode_id}: contact recovery style metadata disagree."
                )
            if int(parameters.get("approach_sector", -1)) not in range(8):
                raise DatasetValidationError(
                    f"{episode_id}: invalid contact approach sector."
                )
        if strict_v9:
            approach_profile = episode.get("contact_approach_profile")
            if approach_profile not in {"direct", "glance_left", "glance_right"}:
                raise DatasetValidationError(
                    f"{episode_id}: invalid contact approach profile "
                    f"{approach_profile!r}."
                )
            if parameters.get("approach_profile") != approach_profile:
                raise DatasetValidationError(
                    f"{episode_id}: contact approach profile metadata disagree."
                )
        required_hold = int(parameters.get("required_contact_hold_steps", 0))
        required_recovery = int(parameters.get("required_recovery_steps", 0))
        if (
            int(episode.get("contact_hold_steps", 0)) < required_hold
            or int(episode.get("verified_contact_steps", 0)) < min(2, required_hold)
            or int(episode.get("recovery_steps", 0)) < required_recovery
        ):
            raise DatasetValidationError(
                f"{episode_id}: contact/recovery counters do not satisfy "
                "the recorded requirements."
            )
    elif mission == "ramp_traverse":
        if int(episode.get("ramp_traversals", 0)) < 1:
            raise DatasetValidationError(
                f"{episode_id}: successful ramp mission has no traversal."
            )
        if strict_v9:
            path_profile = episode.get("ramp_path_profile")
            if path_profile not in {
                "center",
                "diagonal_left_to_right",
                "diagonal_right_to_left",
            }:
                raise DatasetValidationError(
                    f"{episode_id}: invalid ramp path profile {path_profile!r}."
                )
            if parameters.get("path_profile") != path_profile:
                raise DatasetValidationError(
                    f"{episode_id}: ramp path profile metadata disagree."
                )
            scenario_index = int(episode.get("ramp_scenario_index", -1))
            if scenario_index not in range(30):
                raise DatasetValidationError(
                    f"{episode_id}: ramp scenario index {scenario_index} is invalid."
                )
    elif mission == "hoop_pass":
        if int(episode.get("hoop_passes", 0)) != 1:
            raise DatasetValidationError(
                f"{episode_id}: hoop mission must record exactly one passage."
            )
        if strict_v9:
            path_profile = episode.get("hoop_path_profile")
            if path_profile not in {
                "center",
                "oblique_left_to_right",
                "oblique_right_to_left",
            }:
                raise DatasetValidationError(
                    f"{episode_id}: invalid hoop path profile {path_profile!r}."
                )
            if parameters.get("path_profile") != path_profile:
                raise DatasetValidationError(
                    f"{episode_id}: hoop path profile metadata disagree."
                )
            scenario_index = int(episode.get("hoop_scenario_index", -1))
            if scenario_index not in range(30):
                raise DatasetValidationError(
                    f"{episode_id}: hoop scenario index {scenario_index} is invalid."
                )
            crossing_y = float(episode.get("hoop_crossing_y", 1e9))
            crossing_z = float(episode.get("hoop_crossing_z", 1e9))
            if (
                not episode.get("hoop_crossing_recorded")
                or abs(crossing_y + 700.0) >= 90.0
                or not 80.0 <= crossing_z <= 145.0
            ):
                raise DatasetValidationError(
                    f"{episode_id}: interpolated hoop crossing "
                    f"(y={crossing_y:.1f}, z={crossing_z:.1f}) is outside the opening."
                )

    if strict_v9 and mission in {
        "contact_recovery",
        "ramp_traverse",
        "hoop_pass",
    }:
        facing_profile = episode.get("locomotion_facing_profile")
        known_facing_profiles = {
            "forward",
            "backward",
            "strafe_left",
            "strafe_right",
            "free_attention",
        }
        if facing_profile not in known_facing_profiles:
            raise DatasetValidationError(
                f"{episode_id}: invalid locomotion-facing profile "
                f"{facing_profile!r}."
            )
        if parameters.get("locomotion_facing_profile") != facing_profile:
            raise DatasetValidationError(
                f"{episode_id}: locomotion-facing metadata disagree."
            )
        moving_frames = int(episode.get("facing_moving_frames", 0))
        matched_frames = int(episode.get("facing_matched_frames", 0))
        match_ratio = float(episode.get("facing_match_ratio", -1.0))
        if (
            moving_frames < 0
            or matched_frames < 0
            or matched_frames > moving_frames
            or not 0.0 <= match_ratio <= 1.0001
        ):
            raise DatasetValidationError(
                f"{episode_id}: invalid realized-facing counters."
            )
        expected_ratio = matched_frames / moving_frames if moving_frames else 0.0
        if abs(match_ratio - expected_ratio) > 0.002:
            raise DatasetValidationError(
                f"{episode_id}: realized-facing ratio does not match its counters."
            )
        if facing_profile != "free_attention" and (
            moving_frames < 5 or match_ratio < 0.45
        ):
            raise DatasetValidationError(
                f"{episode_id}: {facing_profile} was selected but only "
                f"{matched_frames}/{moving_frames} qualifying moving frames matched."
            )
        for frame in episode_frames:
            if frame.get("locomotion_facing_profile") != facing_profile:
                raise DatasetValidationError(
                    f"{episode_id}: frame locomotion-facing profile changed."
                )

    if strict_v8:
        final_distance = float(
            episode.get(
                "distance_to_goal_at_success_cm"
                if strict_v10
                else "final_distance_to_goal_cm",
                1e9,
            )
        )
        maximum_final_distance = 180.0 if mission == "contact_recovery" else 165.0
        if final_distance >= maximum_final_distance:
            raise DatasetValidationError(
                f"{episode_id}: success distance {final_distance:.1f} cm is outside "
                f"the completion radius {maximum_final_distance:.1f} cm."
            )

        for field in ("start", "goal", "recovery_goal"):
            point = parameters.get(field)
            if not isinstance(point, dict):
                continue
            x = abs(float(point.get("x", 0.0)))
            y = abs(float(point.get("y", 0.0)))
            if x >= 1450.0 or y >= 1450.0:
                raise DatasetValidationError(
                    f"{episode_id}: {field} lies on/outside "
                    "the old clamp boundary."
                )


def validate_v2_runtime_contract(
    dataset: dict[str, Any],
    episode_id: str,
    frames: list[dict[str, Any]],
    transitions: list[dict[str, Any]],
    episode: dict[str, Any] | None = None,
) -> None:
    """Validate schema-11 combined V2 action, overlay, and cooldown semantics."""
    forbidden_fragments = (
        "trajectory_mask",
        "trajectory_points",
        "trajectory_polyline",
        "launch_velocity",
        "grenade_mask",
    )
    for record in [dataset, *frames, *transitions]:
        lowered_keys = {str(key).lower() for key in record}
        for fragment in forbidden_fragments:
            if any(fragment in key for key in lowered_keys):
                raise DatasetValidationError(
                    f"{episode_id}: forbidden engineered trajectory field {fragment}."
                )

    expected_cooldown = 2 * int(dataset.get("observation_rate_hz", 20))
    previous_action_mask = 0
    for transition in transitions:
        source_index = int(transition["source_frame_index"])
        source = frames[source_index]
        target = frames[source_index + 1]
        action_mask = int(transition["action_mask"])
        q_held = bool(action_mask & (1 << 8))
        e_held = bool(action_mask & (1 << 9))
        q_was_held = bool(previous_action_mask & (1 << 8))
        e_was_held = bool(previous_action_mask & (1 << 9))
        e_edge = e_held and not e_was_held
        q_rising = q_held and not q_was_held
        q_falling = not q_held and q_was_held
        movement_requested = bool(action_mask & 0x0F)

        if bool(transition["q_rising_edge"]) != q_rising:
            raise DatasetValidationError(f"{episode_id}: inconsistent Q rising edge.")
        if bool(transition["q_falling_edge"]) != q_falling:
            raise DatasetValidationError(f"{episode_id}: inconsistent Q falling edge.")
        if bool(transition["e_request_edge"]) != e_edge:
            raise DatasetValidationError(f"{episode_id}: inconsistent E request edge.")
        if bool(transition["planar_movement_suppressed"]) != (
            q_held and movement_requested
        ):
            raise DatasetValidationError(
                f"{episode_id}: inconsistent Q-priority movement suppression."
            )
        if q_held and (
            float(transition["forward_axis"]) != 0.0
            or float(transition["right_axis"]) != 0.0
        ):
            raise DatasetValidationError(
                f"{episode_id}: Q-held transition has effective planar movement."
            )

        cooldown_before = int(transition["cooldown_before_steps"])
        cooldown_after = int(transition["cooldown_after_steps"])
        source_q_visible = bool(source["trajectory_visible"])
        if not e_held:
            expected_reason = "none"
        elif not e_edge:
            expected_reason = "not_rising_edge"
        elif not q_held:
            expected_reason = "q_not_held"
        elif not source_q_visible:
            expected_reason = "q_not_previously_visible"
        elif cooldown_before > 0:
            expected_reason = "cooldown"
        else:
            expected_reason = "none"
        if transition["e_rejection_reason"] != expected_reason:
            raise DatasetValidationError(
                f"{episode_id}: E rejection reason is inconsistent."
            )

        accepted = bool(transition["e_accepted"])
        if accepted:
            if not (e_edge and q_held and source_q_visible and cooldown_before == 0):
                raise DatasetValidationError(
                    f"{episode_id}: accepted throw violates the Q-before-E gate."
                )
            if transition.get("accepted_throw_grenade_id") is None:
                raise DatasetValidationError(
                    f"{episode_id}: accepted throw lacks a grenade ID."
                )
            if cooldown_after != expected_cooldown:
                raise DatasetValidationError(
                    f"{episode_id}: accepted throw did not start exact cooldown."
                )
        else:
            if transition.get("accepted_throw_grenade_id") is not None:
                raise DatasetValidationError(
                    f"{episode_id}: rejected/non-request throw has a grenade ID."
                )
            if cooldown_after != cooldown_before:
                raise DatasetValidationError(
                    f"{episode_id}: rejected request changed cooldown."
                )
        if int(target["cooldown_remaining_steps"]) != cooldown_after:
            raise DatasetValidationError(
                f"{episode_id}: target frame cooldown disagrees with transition."
            )

        previous_action_mask = action_mask

    for frame in frames:
        q_visible = bool(frame["q_visibility"])
        if bool(frame["trajectory_visible"]) != q_visible:
            raise DatasetValidationError(
                f"{episode_id}: trajectory visibility disagrees with Q."
            )
        if bool(frame["aim_lock_active"]) != q_visible:
            raise DatasetValidationError(
                f"{episode_id}: aim-lock state disagrees with Q."
            )
        grenades = frame.get("grenades") or []
        flying = sum(not grenade.get("resting", False) for grenade in grenades)
        resting = sum(bool(grenade.get("resting", False)) for grenade in grenades)
        if int(frame["flying_grenade_count"]) != flying:
            raise DatasetValidationError(f"{episode_id}: flying count mismatch.")
        if int(frame["resting_grenade_count"]) != resting:
            raise DatasetValidationError(f"{episode_id}: resting count mismatch.")
        if int(frame["total_grenade_count"]) != len(grenades):
            raise DatasetValidationError(f"{episode_id}: total grenade count mismatch.")
        expected_crosshair = (
            "Cooldown" if int(frame["cooldown_remaining_steps"]) > 0 else "Ready"
        )
        if frame["crosshair_state"] != expected_crosshair:
            raise DatasetValidationError(
                f"{episode_id}: crosshair does not match cooldown state."
            )

    if dataset.get("collection_policy") != "diagnostic_v2_trajectory_hold_mission":
        return
    if episode is None:
        raise DatasetValidationError(
            f"{episode_id}: trajectory-hold validation lacks an episode record."
        )
    if episode.get("v2_source") != "random_play" or episode.get("v2_cell_id") != (
        "R08_throw_hold_cooldown_diagnostic"
    ):
        raise DatasetValidationError(
            f"{episode_id}: trajectory-hold V2 identity is inconsistent."
        )
    if not frames or bool(frames[0]["q_visibility"]):
        raise DatasetValidationError(
            f"{episode_id}: trajectory-hold mission needs one initial Q-off observation."
        )
    if any(not bool(frame["q_visibility"]) for frame in frames[1:]):
        raise DatasetValidationError(
            f"{episode_id}: Q was released during the trajectory-hold mission."
        )

    observation_rate = int(dataset.get("observation_rate_hz", 20))
    expected_throw_frame = max(2, (observation_rate + 1) // 2)
    accepted = [transition for transition in transitions if transition["e_accepted"]]
    if len(accepted) != 1 or int(accepted[0]["source_frame_index"]) != expected_throw_frame:
        raise DatasetValidationError(
            f"{episode_id}: trajectory-hold mission must throw exactly once after "
            "a half-second preview."
        )
    for transition in transitions:
        source_index = int(transition["source_frame_index"])
        expected_mask = (1 << 8) | ((1 << 9) if source_index == expected_throw_frame else 0)
        if int(transition["action_mask"]) != expected_mask:
            raise DatasetValidationError(
                f"{episode_id}: trajectory-hold action changed at transition "
                f"{source_index}."
            )

    stable_position = frames[1]["position"]
    stable_camera = frames[1]["camera"]
    for frame in frames[1:]:
        position = frame["position"]
        camera = frame["camera"]
        if (
            abs(float(position["x"]) - float(stable_position["x"])) > 1e-3
            or abs(float(position["y"]) - float(stable_position["y"])) > 1e-3
            or abs(float(camera["yaw"]) - float(stable_camera["yaw"])) > 1e-3
            or abs(float(camera["pitch"]) - float(stable_camera["pitch"])) > 1e-3
        ):
            raise DatasetValidationError(
                f"{episode_id}: trajectory-hold mission moved the player or camera."
            )

    throw_target_index = expected_throw_frame + 1
    post_throw_frames = frames[throw_target_index:]
    if not post_throw_frames or any(
        not bool(frame["trajectory_visible"]) for frame in post_throw_frames
    ):
        raise DatasetValidationError(
            f"{episode_id}: trajectory was not continuously visible after the throw."
        )
    positive_cooldown_frames = sum(
        int(frame["cooldown_remaining_steps"]) > 0 for frame in post_throw_frames
    )
    if positive_cooldown_frames != expected_cooldown:
        raise DatasetValidationError(
            f"{episode_id}: trajectory-hold mission did not show the complete cooldown."
        )
    if int(frames[-1]["resting_grenade_count"]) != 1:
        raise DatasetValidationError(
            f"{episode_id}: trajectory-hold mission ended before its grenade rested."
        )


def validate_run_distributions(
    dataset: dict[str, Any],
    episodes: list[dict[str, Any]],
) -> None:
    """Enforce run-level diversity gates when a run is large enough to judge."""
    policy = dataset.get("collection_policy")
    strict_v9 = str(dataset.get("schema_version", "")).endswith(
        ("-preflight-9", "-preflight-10", "-preflight-11", "-production-11")
    )
    guided = [
        episode
        for episode in episodes
        if episode.get("collection_mission") != "semi_markov"
    ]
    failures = [episode for episode in guided if not episode.get("mission_success")]
    if policy == "inspection_only_mission_review_suite":
        expected_episode_count = 60 if strict_v9 else 44
        expected_guided_count = 59 if strict_v9 else 43
        if (
            len(episodes) != expected_episode_count
            or len(guided) != expected_guided_count
        ):
            raise DatasetValidationError(
                "Mission review suite must contain "
                f"{expected_episode_count} episodes / "
                f"{expected_guided_count} guided missions."
            )
        slugs = [episode.get("mission_review_slug") for episode in episodes]
        if any(not slug for slug in slugs) or len(set(slugs)) != expected_episode_count:
            raise DatasetValidationError(
                "Mission review slugs must be present and unique."
            )
        mission_counts = Counter(
            episode.get("collection_mission") for episode in episodes
        )
        expected_counts = (
            {
                "semi_markov": 1,
                "object_view": 30,
                "contact_recovery": 9,
                "ramp_traverse": 10,
                "hoop_pass": 10,
            }
            if strict_v9
            else {
                "semi_markov": 1,
                "object_view": 30,
                "contact_recovery": 9,
                "ramp_traverse": 2,
                "hoop_pass": 2,
            }
        )
        if mission_counts != expected_counts:
            raise DatasetValidationError(
                f"Mission review family counts {dict(mission_counts)} "
                f"!= {expected_counts}."
            )
        orbit_pairs = Counter(
            (
                episode.get("coverage_target"),
                episode.get("object_view_mode"),
                episode.get("orbit_direction"),
            )
            for episode in episodes
            if episode.get("object_view_mode") in {"partial_orbit", "full_orbit"}
        )
        if len(orbit_pairs) != 20 or any(count != 1 for count in orbit_pairs.values()):
            raise DatasetValidationError(
                "Review suite must contain each target/mode/orbit-direction once."
            )
        if strict_v9:
            for mission, direction_field, directions in (
                ("ramp_traverse", "ramp_direction", {"uphill", "downhill"}),
                (
                    "hoop_pass",
                    "mission_parameters.direction",
                    {"positive_x_to_negative_x", "negative_x_to_positive_x"},
                ),
            ):
                mission_episodes = [
                    episode
                    for episode in episodes
                    if episode.get("collection_mission") == mission
                ]
                by_direction: dict[str, set[str]] = defaultdict(set)
                for episode in mission_episodes:
                    if direction_field == "ramp_direction":
                        direction = str(episode.get(direction_field))
                    else:
                        direction = str(
                            (episode.get("mission_parameters") or {}).get("direction")
                        )
                    by_direction[direction].add(
                        str(episode.get("locomotion_facing_profile"))
                    )
                expected_facing = {
                    "forward",
                    "backward",
                    "strafe_left",
                    "strafe_right",
                    "free_attention",
                }
                if set(by_direction) != directions or any(
                    profiles != expected_facing for profiles in by_direction.values()
                ):
                    raise DatasetValidationError(
                        f"Review suite does not demonstrate every {mission} "
                        "direction with all five locomotion-facing profiles."
                    )
        if failures:
            raise DatasetValidationError(
                f"Mission review suite has {len(failures)} guided failure(s)."
            )

    orbit_episodes = [
        episode
        for episode in guided
        if episode.get("object_view_mode") in {"partial_orbit", "full_orbit"}
        and episode.get("mission_success")
    ]
    if len(orbit_episodes) >= 10:
        direction_counts = Counter(
            episode.get("orbit_direction") for episode in orbit_episodes
        )
        if set(direction_counts) != {"clockwise", "counter_clockwise"}:
            raise DatasetValidationError(
                "Orbit run is one-sided; both directions are required."
            )
        minority_share = min(direction_counts.values()) / len(orbit_episodes)
        if len(orbit_episodes) >= 20 and minority_share < 0.30:
            raise DatasetValidationError(
                f"Orbit direction minority share is only {minority_share:.1%}."
            )

    object_episodes = [
        episode
        for episode in guided
        if episode.get("collection_mission") == "object_view"
        and episode.get("mission_success")
    ]
    if len(object_episodes) >= 80:
        distinct_cells = {
            int(episode["object_scenario_index"])
            for episode in object_episodes
            if episode.get("object_scenario_index") is not None
        }
        minimum_cells = 45 if len(object_episodes) < 400 else 90
        if len(distinct_cells) < minimum_cells:
            raise DatasetValidationError(
                f"Only {len(distinct_cells)} object scenario cells were covered; "
                f"expected at least {minimum_cells}."
            )

    contact_episodes = [
        episode
        for episode in guided
        if episode.get("collection_mission") == "contact_recovery"
        and episode.get("mission_success")
    ]
    if len(contact_episodes) >= 45:
        distinct_cells = {
            int(episode["contact_scenario_index"])
            for episode in contact_episodes
            if episode.get("contact_scenario_index") is not None
        }
        if len(distinct_cells) < 40:
            raise DatasetValidationError(
                f"Only {len(distinct_cells)} contact scenario cells were covered."
            )
        if strict_v9:
            profile_sets = {
                field: {episode.get(field) for episode in contact_episodes}
                for field in (
                    "contact_recovery_style",
                    "contact_approach_profile",
                    "locomotion_facing_profile",
                )
            }
            expected_sizes = {
                "contact_recovery_style": 5,
                "contact_approach_profile": 3,
                "locomotion_facing_profile": 5,
            }
            for field, expected_size in expected_sizes.items():
                if len(profile_sets[field]) != expected_size:
                    raise DatasetValidationError(
                        f"Contact run covers only {len(profile_sets[field])} "
                        f"{field} values; expected {expected_size}."
                    )

    for mission, scenario_field, path_field in (
        ("ramp_traverse", "ramp_scenario_index", "ramp_path_profile"),
        ("hoop_pass", "hoop_scenario_index", "hoop_path_profile"),
    ) if strict_v9 else ():
        mission_episodes = [
            episode
            for episode in guided
            if episode.get("collection_mission") == mission
            and episode.get("mission_success")
        ]
        if len(mission_episodes) >= 30:
            scenario_count = len(
                {
                    int(episode[scenario_field])
                    for episode in mission_episodes
                    if episode.get(scenario_field) is not None
                }
            )
            if scenario_count < 24:
                raise DatasetValidationError(
                    f"Only {scenario_count} / 30 {mission} scenario cells were covered."
                )
            if len({episode.get(path_field) for episode in mission_episodes}) != 3:
                raise DatasetValidationError(
                    f"{mission} run did not cover all three path profiles."
                )
            if (
                len(
                    {
                        episode.get("locomotion_facing_profile")
                        for episode in mission_episodes
                    }
                )
                != 5
            ):
                raise DatasetValidationError(
                    f"{mission} run did not cover all five facing profiles."
                )

    radii = [
        round(float((episode.get("mission_parameters") or {}).get("orbit_radius_cm", 0)), 3)
        for episode in object_episodes
        if episode.get("object_view_mode") in {"partial_orbit", "full_orbit"}
    ]
    if len(radii) >= 16:
        most_common = Counter(radii).most_common(1)[0][1]
        if len(set(radii)) < 8 or most_common / len(radii) > 0.25:
            raise DatasetValidationError(
                "Orbit radii show a clamp spike or insufficient stratified diversity."
            )


def normalize_angle_degrees(value: float) -> float:
    return (value + 180.0) % 360.0 - 180.0


def validate_camera_pitch_contract(
    dataset: dict[str, Any],
    episode_id: str,
    frames: list[dict[str, Any]],
    transitions: list[dict[str, Any]],
) -> None:
    required_fields = (
        "camera_pitch_min_degrees",
        "camera_pitch_max_degrees",
        "camera_pitch_rate_degrees_per_second",
    )
    if any(field not in dataset for field in required_fields):
        return

    minimum = float(dataset["camera_pitch_min_degrees"])
    maximum = float(dataset["camera_pitch_max_degrees"])
    pitch_per_step = float(dataset["camera_pitch_rate_degrees_per_second"]) / float(
        dataset["observation_rate_hz"]
    )
    tolerance = 0.02

    for frame in frames:
        recorded = float(frame["camera"]["pitch"])
        normalized = normalize_angle_degrees(recorded)
        if abs(recorded - normalized) > tolerance:
            raise DatasetValidationError(
                f"{episode_id}: frame {frame['frame_index']} has noncanonical "
                f"camera pitch {recorded}."
            )
        if normalized < minimum - tolerance or normalized > maximum + tolerance:
            raise DatasetValidationError(
                f"{episode_id}: frame {frame['frame_index']} camera pitch "
                f"{normalized} lies outside engine limits [{minimum}, {maximum}]."
            )

    for transition, source, target in zip(transitions, frames, frames[1:]):
        source_pitch = normalize_angle_degrees(float(source["camera"]["pitch"]))
        target_pitch = normalize_angle_degrees(float(target["camera"]["pitch"]))
        expected_pitch = min(
            maximum,
            max(
                minimum,
                source_pitch
                + float(transition["pitch_axis"]) * pitch_per_step,
            ),
        )
        if abs(target_pitch - expected_pitch) > tolerance:
            raise DatasetValidationError(
                f"{episode_id}: transition {transition['source_frame_index']} "
                f"camera pitch {source_pitch} -> {target_pitch} does not match "
                f"recorded pitch axis {transition['pitch_axis']} and engine "
                f"limits [{minimum}, {maximum}]; expected {expected_pitch}."
            )


def validate_dataset(
    dataset_dir: Path,
) -> tuple[
    dict[str, Any],
    Path,
    str,
    dict[str, list[dict[str, Any]]],
]:
    dataset_json_path = dataset_dir / "dataset.json"
    if not dataset_json_path.is_file():
        raise DatasetValidationError(f"Missing dataset manifest: {dataset_json_path}")

    dataset = json.loads(dataset_json_path.read_text(encoding="utf-8"))
    if not dataset.get("complete"):
        raise DatasetValidationError(
            f"Dataset is marked incomplete: {dataset.get('error', 'unknown error')}"
        )
    shards = dataset.get("shards")
    if not isinstance(shards, list) or len(shards) != 1:
        raise DatasetValidationError(
            "Preflight schema requires exactly one shard per worker run."
        )

    shard_path = dataset_dir / shards[0]
    if not shard_path.is_file():
        raise DatasetValidationError(f"Missing shard: {shard_path}")
    checksum = validate_checksum(dataset_dir, shard_path)

    expected_width = int(dataset["rgb_width"])
    expected_height = int(dataset["rgb_height"])
    records_by_episode: dict[str, list[dict[str, Any]]] = defaultdict(list)

    with tarfile.open(shard_path, mode="r:") as tar:
        names = {member.name for member in tar.getmembers() if member.isfile()}
        metadata_format = str(dataset.get("metadata_format", ""))
        if metadata_format == "jsonl":
            frames = read_json_lines(tar, "metadata/frames.jsonl")
            transitions = read_json_lines(tar, "metadata/transitions.jsonl")
            episodes = read_json_lines(tar, "metadata/episodes.jsonl")
        elif metadata_format == "parquet":
            frames = read_parquet_records(tar, "frames.parquet")
            transitions = read_parquet_records(tar, "transitions.parquet")
            episodes = read_parquet_records(
                tar, "episodes.parquet", episodes=True
            )
        else:
            raise DatasetValidationError(
                f"Unsupported metadata format: {metadata_format}"
            )

        if len(frames) != int(dataset["observation_count"]):
            raise DatasetValidationError(
                f"Frame metadata count {len(frames)} != manifest "
                f"{dataset['observation_count']}"
            )
        if len(transitions) != int(dataset["transition_count"]):
            raise DatasetValidationError(
                f"Transition count {len(transitions)} != manifest "
                f"{dataset['transition_count']}"
            )
        if len(episodes) != int(dataset["completed_episode_count"]):
            raise DatasetValidationError(
                f"Episode count {len(episodes)} != manifest "
                f"{dataset['completed_episode_count']}"
            )
        if dataset.get("prescribed_recipes"):
            recipe_ids = [episode.get("recipe_id") for episode in episodes]
            if None in recipe_ids or len(recipe_ids) != len(set(recipe_ids)):
                raise DatasetValidationError(
                    "Prescribed shard recipe IDs must be present and unique."
                )

        for frame in frames:
            image_key = frame["rgb_key"]
            if image_key not in names:
                raise DatasetValidationError(f"Missing RGB observation: {image_key}")
            records_by_episode[frame["episode_id"]].append(frame)

        transition_groups: dict[str, list[dict[str, Any]]] = defaultdict(list)
        for transition in transitions:
            transition_groups[transition["episode_id"]].append(transition)

        for episode in episodes:
            episode_id = episode["episode_id"]
            if dataset.get("prescribed_recipes"):
                required_identity = (
                    "plan_id",
                    "plan_version",
                    "assignment_id",
                    "attempt_id",
                    "executor_id",
                    "split",
                    "recipe_id",
                )
                missing_identity = [
                    field for field in required_identity if not episode.get(field)
                ]
                if missing_identity:
                    raise DatasetValidationError(
                        f"{episode_id}: prescribed episode is missing controller "
                        f"identity fields: {', '.join(missing_identity)}"
                    )
                for field in (
                    "continuous_sample_ordinal",
                    "refinement_level",
                    "repetition_index",
                    "prescribed_scenario_index",
                ):
                    if episode.get(field) is None or int(episode[field]) < 0:
                        raise DatasetValidationError(
                            f"{episode_id}: invalid prescribed field {field}."
                        )
                if episode.get("plan_id") != dataset.get("plan_id"):
                    raise DatasetValidationError(
                        f"{episode_id}: plan_id disagrees with dataset manifest."
                    )
            episode_frames = sorted(
                records_by_episode.get(episode_id, []),
                key=lambda record: int(record["frame_index"]),
            )
            episode_transitions = sorted(
                transition_groups.get(episode_id, []),
                key=lambda record: int(record["source_frame_index"]),
            )
            expected_transitions = int(episode["actual_transitions"])
            expected_frame_indices = list(range(expected_transitions + 1))
            actual_frame_indices = [
                int(record["frame_index"]) for record in episode_frames
            ]
            actual_transition_indices = [
                int(record["source_frame_index"]) for record in episode_transitions
            ]
            if actual_frame_indices != expected_frame_indices:
                raise DatasetValidationError(
                    f"{episode_id}: discontinuous frame indices."
                )
            if actual_transition_indices != list(range(expected_transitions)):
                raise DatasetValidationError(
                    f"{episode_id}: discontinuous transition indices."
                )
            validate_camera_pitch_contract(
                dataset,
                episode_id,
                episode_frames,
                episode_transitions,
            )

            schema_version = str(dataset.get("schema_version", ""))
            schema_is_v8_or_newer = schema_version.endswith(
                (
                    "-preflight-8",
                    "-preflight-9",
                    "-preflight-10",
                    "-preflight-11",
                    "-production-1",
                    "-production-11",
                )
            )
            if (
                dataset.get("collection_policy")
                in {
                    "training_frame_balanced_final_agent_v1",
                    "training_frame_balanced_final_agent_v2",
                    "training_frame_balanced_final_agent_v3",
                    "training_frame_balanced_final_agent_v4",
                    "training_frame_balanced_final_agent_v5",
                    "inspection_only_mission_review_suite",
                }
                and schema_version.endswith(
                    (
                        "-preflight-6",
                        "-preflight-7",
                        "-preflight-8",
                        "-preflight-9",
                        "-preflight-10",
                        "-preflight-11",
                        "-production-1",
                        "-production-11",
                    )
                )
            ):
                validate_final_agent_mission(
                    episode,
                    episode_frames,
                    strict_v8=schema_is_v8_or_newer,
                    strict_v9=schema_version.endswith(
                        (
                            "-preflight-9",
                            "-preflight-10",
                            "-preflight-11",
                            "-production-1",
                            "-production-11",
                        )
                    ),
                    strict_v10=schema_version.endswith(
                        (
                            "-preflight-10",
                            "-preflight-11",
                            "-production-1",
                            "-production-11",
                        )
                    ),
                    observation_rate_hz=int(dataset.get("observation_rate_hz", 20)),
                )

            if schema_version.endswith(("-preflight-11", "-production-11")):
                validate_v2_runtime_contract(
                    dataset,
                    episode_id,
                    episode_frames,
                    episode_transitions,
                    episode,
                )

            for frame in episode_frames:
                extracted = tar.extractfile(frame["rgb_key"])
                if extracted is None:
                    raise DatasetValidationError(
                        f"Could not read observation: {frame['rgb_key']}"
                    )
                dimensions = image_dimensions(
                    extracted, str(dataset.get("rgb_format", ""))
                )
                if dimensions != (expected_width, expected_height):
                    raise DatasetValidationError(
                        f"{frame['rgb_key']}: dimensions {dimensions} != "
                        f"{(expected_width, expected_height)}"
                    )
            records_by_episode[episode_id] = episode_frames

        if str(dataset.get("schema_version", "")).endswith(
            (
                "-preflight-8",
                "-preflight-9",
                "-preflight-10",
                "-preflight-11",
                "-production-1",
                "-production-11",
            )
        ):
            validate_run_distributions(dataset, episodes)

    return dataset, shard_path, checksum, dict(records_by_episode)


def resolve_ffmpeg(explicit_path: Path | None) -> Path:
    if explicit_path:
        candidate = explicit_path.resolve()
        if candidate.is_file():
            return candidate
        raise DatasetValidationError(f"ffmpeg was not found at {candidate}")

    resolved = shutil.which("ffmpeg")
    if resolved:
        return Path(resolved)
    raise DatasetValidationError(
        "Dataset validation passed, but ffmpeg is not installed or on PATH. "
        "Install ffmpeg or pass --ffmpeg C:\\path\\to\\ffmpeg.exe."
    )


def render_episode(
    tar: tarfile.TarFile,
    ffmpeg: Path,
    output_path: Path,
    frame_records: list[dict[str, Any]],
    fps: int,
    image_codec: str,
) -> None:
    command = [
        str(ffmpeg),
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-f",
        "image2pipe",
        "-framerate",
        str(fps),
        "-vcodec",
        image_codec,
        "-i",
        "pipe:0",
        "-an",
        "-c:v",
        "libx264",
        "-crf",
        "18",
        "-preset",
        "medium",
        "-pix_fmt",
        "yuv420p",
        str(output_path),
    ]
    process = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    assert process.stdin is not None
    try:
        for frame in frame_records:
            extracted = tar.extractfile(frame["rgb_key"])
            if extracted is None:
                raise DatasetValidationError(
                    f"Could not read observation: {frame['rgb_key']}"
                )
            shutil.copyfileobj(extracted, process.stdin)
        process.stdin.close()
        stderr = process.stderr.read().decode("utf-8", errors="replace")
        return_code = process.wait()
    except BaseException:
        process.kill()
        process.wait()
        output_path.unlink(missing_ok=True)
        raise

    if return_code != 0:
        output_path.unlink(missing_ok=True)
        raise DatasetValidationError(
            f"ffmpeg failed for {output_path.name}: {stderr.strip()}"
        )


def main() -> int:
    args = parse_args()
    dataset_dir = args.dataset.resolve()
    output_dir = (args.output or dataset_dir / "review").resolve()

    try:
        dataset, shard_path, checksum, records_by_episode = validate_dataset(dataset_dir)
        print(
            "Validation passed: "
            f"{dataset['completed_episode_count']} episode(s), "
            f"{dataset['transition_count']} transitions, "
            f"{dataset['observation_count']} observations."
        )
        if args.validate_only:
            return 0

        selected = set(args.episode or records_by_episode.keys())
        unknown = selected.difference(records_by_episode)
        if unknown:
            raise DatasetValidationError(
                f"Unknown episode ID(s): {', '.join(sorted(unknown))}"
            )
        ffmpeg = resolve_ffmpeg(args.ffmpeg)
        output_dir.mkdir(parents=True, exist_ok=True)
        rendered: list[dict[str, Any]] = []
        used_video_stems: set[str] = set()

        with tarfile.open(shard_path, mode="r:") as tar:
            for episode_id in sorted(selected):
                frame_records = records_by_episode[episode_id]
                review_slug = (
                    frame_records[0].get("mission_review_slug")
                    if frame_records
                    else None
                )
                video_stem = str(review_slug or episode_id)
                if video_stem in used_video_stems:
                    raise DatasetValidationError(
                        f"Duplicate review-video filename stem: {video_stem}"
                    )
                used_video_stems.add(video_stem)
                output_path = output_dir / f"{video_stem}.mp4"
                render_episode(
                    tar,
                    ffmpeg,
                    output_path,
                    frame_records,
                    int(dataset["observation_rate_hz"]),
                    "webp"
                    if dataset.get("rgb_format") == "lossless_webp"
                    else "png",
                )
                rendered.append(
                    {
                        "episode_id": episode_id,
                        "mission_review_slug": review_slug,
                        "frame_count": len(frame_records),
                        "source_rgb_keys": [
                            frame["rgb_key"]
                            for frame in frame_records
                        ],
                        "video": output_path.name,
                    }
                )
                print(f"Created {output_path}")

        review_manifest = {
            "source_dataset": str(dataset_dir),
            "source_shard": shard_path.name,
            "source_shard_md5": checksum,
            "fps": int(dataset["observation_rate_hz"]),
            "videos": rendered,
        }
        (output_dir / "review_manifest.json").write_text(
            json.dumps(review_manifest, indent=2) + "\n",
            encoding="utf-8",
        )
        return 0
    except (DatasetValidationError, OSError, KeyError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
