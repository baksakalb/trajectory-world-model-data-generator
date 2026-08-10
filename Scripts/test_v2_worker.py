#!/usr/bin/env python3
"""Ledger tests for exact local packaged-build binding."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from dataset_worker import bind_execution_build, command_for


class V2WorkerBuildBindingTests(unittest.TestCase):
    def test_editor_command_opens_the_project_in_game_mode(self) -> None:
        command = command_for(
            Path("UnrealEditor-Cmd.exe"),
            Path("request.json"),
            Path("output"),
        )
        self.assertEqual(Path(command[1]).name, "he_grenade_game.uproject")
        self.assertEqual(command[2], "-game")

    def test_packaged_command_does_not_add_an_editor_project(self) -> None:
        command = command_for(Path("he_grenade_game.exe"), Path("request.json"), Path("output"))
        self.assertEqual(command[1], "-GenerateDataset")

    def test_collection_rejects_a_different_packaged_runtime(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root / "package"
            executable = package / "game.exe"
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"bootstrap")
            runtime = package / "game" / "Binaries" / "Win64" / "game.exe"
            runtime.parent.mkdir(parents=True)
            runtime.write_bytes(b"runtime-one")
            generated_config = package / "game" / "Saved" / "Config" / "GameUserSettings.ini"
            generated_config.parent.mkdir(parents=True)
            generated_config.write_bytes(b"mutable-one")
            plan = {"plan_id": "plan-a", "generator_source_sha256": "source-a"}

            first = bind_execution_build(root / "collection", executable, plan)
            second = bind_execution_build(root / "collection", executable, plan)
            self.assertEqual(first, second)
            self.assertEqual(first["package_runtime_file_count"], 2)

            generated_config.write_bytes(b"mutable-two")
            self.assertEqual(
                first,
                bind_execution_build(root / "collection", executable, plan),
            )

            runtime.write_bytes(b"runtime-two")
            with self.assertRaisesRegex(ValueError, "immutable execution-build.json"):
                bind_execution_build(root / "collection", executable, plan)


if __name__ == "__main__":
    unittest.main()
