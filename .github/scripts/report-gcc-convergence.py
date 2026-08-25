#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
from pathlib import Path


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], text=True).strip()


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Measure the full tree delta between master and the GCC "
            "compatibility candidate."
        )
    )
    parser.add_argument("--master", default="origin/master")
    parser.add_argument("--gcc", default="HEAD")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    master_sha = git("rev-parse", args.master)
    gcc_sha = git("rev-parse", args.gcc)
    merge_base = git("merge-base", args.master, args.gcc)
    ahead = int(git("rev-list", "--count", f"{args.master}..{args.gcc}"))
    behind = int(git("rev-list", "--count", f"{args.gcc}..{args.master}"))

    status_lines = git(
        "diff", "--no-renames", "--name-status", f"{args.master}..{args.gcc}"
    ).splitlines()
    numstat_lines = git(
        "diff", "--no-renames", "--numstat", f"{args.master}..{args.gcc}"
    ).splitlines()

    stats: dict[str, tuple[int | None, int | None]] = {}
    additions = 0
    deletions = 0
    for line in numstat_lines:
        if not line:
            continue
        added, deleted, path = line.split("\t", 2)
        a = None if added == "-" else int(added)
        d = None if deleted == "-" else int(deleted)
        stats[path] = (a, d)
        if a is not None:
            additions += a
        if d is not None:
            deletions += d

    changed_files = []
    for line in status_lines:
        if not line:
            continue
        status, path = line.split("\t", 1)
        added, deleted = stats.get(path, (None, None))
        changed_files.append(
            {
                "path": path,
                "status": status,
                "additions": added,
                "deletions": deleted,
            }
        )

    report = {
        "schemaVersion": 1,
        "master": master_sha,
        "gcc": gcc_sha,
        "mergeBase": merge_base,
        "aheadBy": ahead,
        "behindBy": behind,
        "treeEqual": not changed_files,
        "changedFileCount": len(changed_files),
        "additions": additions,
        "deletions": deletions,
        "files": changed_files,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        with Path(summary_path).open("a", encoding="utf-8") as summary:
            summary.write("### GCC compatibility delta\n\n")
            summary.write(f"- master: `{master_sha}`\n")
            summary.write(f"- gcc candidate: `{gcc_sha}`\n")
            summary.write(
                f"- graph: gcc is {ahead} commit(s) ahead / {behind} behind\n"
            )
            summary.write(
                f"- tree delta: {len(changed_files)} file(s), "
                f"+{additions}/-{deletions}\n"
            )
            if changed_files:
                summary.write("- retirement state: compatibility delta remains\n\n")
                summary.write(
                    "| status | file | + | - |\n| --- | --- | ---: | ---: |\n"
                )
                for item in changed_files[:25]:
                    a = (
                        "binary"
                        if item["additions"] is None
                        else str(item["additions"])
                    )
                    d = (
                        "binary"
                        if item["deletions"] is None
                        else str(item["deletions"])
                    )
                    summary.write(
                        f"| `{item['status']}` | `{item['path']}` | {a} | {d} |\n"
                    )
                if len(changed_files) > 25:
                    summary.write(
                        f"\n_{len(changed_files) - 25} additional file(s) "
                        "are recorded in the JSON artifact._\n"
                    )
            else:
                summary.write(
                    "- retirement state: **tree-equal promotion candidate**\n"
                )

    if not changed_files:
        print(
            "::notice::GCC compatibility tree matches master; "
            "candidate for direct master GCC validation."
        )


if __name__ == "__main__":
    main()
