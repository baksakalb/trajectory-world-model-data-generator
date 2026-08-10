#!/usr/bin/env python3
"""Build an identity-rich, family-grouped video index for a V2 review run."""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
from pathlib import Path


def safe_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "-", value).strip("-")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("collection", type=Path)
    args = parser.parse_args()
    collection = args.collection.resolve()
    recipes_path = collection / "plan" / "recipes.jsonl"
    review = collection / "review"
    destination = collection / "audit-by-id"
    destination.mkdir(parents=True, exist_ok=True)

    recipes = [
        json.loads(line)
        for line in recipes_path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    rows: list[dict[str, object]] = []
    for recipe in recipes:
        episode_index = int(recipe["episode_index"])
        episode_id = f"p-e{episode_index:09d}"
        source = review / f"{episode_id}.mp4"
        if not source.is_file():
            raise SystemExit(f"missing rendered video: {source}")
        sequence_id = recipe.get("sequence_template_id")
        test_id = str(sequence_id or recipe["cell_id"])
        family = "sequences" if sequence_id else str(recipe["family"])
        recipe_id = str(recipe["recipe_id"])
        seed = int(recipe["seed"])
        filename = safe_name(
            f"seed-{seed}__{test_id}__{episode_id}__{recipe_id}.mp4"
        )
        family_dir = destination / safe_name(family)
        family_dir.mkdir(parents=True, exist_ok=True)
        target = family_dir / filename
        shutil.copy2(source, target)
        rows.append(
            {
                "seed": seed,
                "test_id": test_id,
                "cell_id": recipe["cell_id"],
                "sequence_template_id": sequence_id,
                "family": family,
                "episode_id": episode_id,
                "recipe_id": recipe_id,
                "replay_identity": recipe["replay_identity"],
                "video": target.relative_to(collection).as_posix(),
            }
        )

    fields = list(rows[0])
    with (destination / "audit-index.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    (destination / "audit-index.json").write_text(
        json.dumps(rows, indent=2) + "\n", encoding="utf-8"
    )

    markdown = [
        "# V2 contract-3 visual audit",
        "",
        f"Plan: `{json.loads((collection / 'plan' / 'collection-plan.json').read_text())['plan_id']}`",
        "",
        "Report any problem using the seed or test ID embedded in the filename.",
        "",
    ]
    for family in sorted({str(row["family"]) for row in rows}):
        markdown.extend([f"## {family}", ""])
        for row in (item for item in rows if item["family"] == family):
            relative = Path(str(row["video"])).relative_to("audit-by-id").as_posix()
            markdown.append(
                f"- [{row['test_id']} — seed {row['seed']}]({relative}) "
                f"(`{row['episode_id']}`, `{row['recipe_id']}`)"
            )
        markdown.append("")
    (destination / "README.md").write_text("\n".join(markdown), encoding="utf-8")
    print(json.dumps({"videos": len(rows), "output": str(destination)}))


if __name__ == "__main__":
    main()
