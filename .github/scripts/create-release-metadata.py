#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path

SHA40 = re.compile(r"^[0-9a-f]{40}$")


def sha40(value: str) -> str:
    if not SHA40.fullmatch(value):
        raise argparse.ArgumentTypeError(
            f"expected a 40-character lowercase Git SHA, got {value!r}"
        )
    return value


def dependency(value: str):
    name, separator, revision = value.partition("=")
    if not separator or not name:
        raise argparse.ArgumentTypeError("dependencies must use NAME=REVISION")
    return name, sha40(revision)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Write deterministic release provenance metadata."
    )
    parser.add_argument("--project", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--source-repository", required=True)
    parser.add_argument("--source-branch", default="master")
    parser.add_argument("--source-revision", required=True, type=sha40)
    parser.add_argument("--reference-repository", required=True)
    parser.add_argument("--reference-branch", required=True)
    parser.add_argument("--reference-snapshot", required=True)
    parser.add_argument("--reference-revision", required=True, type=sha40)
    parser.add_argument("--reference-asset", required=True)
    parser.add_argument("--gcc-branch", default="gcc")
    parser.add_argument("--dependency", action="append", default=[], type=dependency)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    dependencies: dict[str, dict[str, str]] = {}
    for name, revision in args.dependency:
        if name in dependencies:
            parser.error(f"duplicate dependency {name!r}")
        dependencies[name] = {"revision": revision}

    metadata = {
        "schemaVersion": 1,
        "project": {
            "name": args.project,
            "version": args.version,
            "tag": args.tag,
        },
        "source": {
            "repository": args.source_repository,
            "branch": args.source_branch,
            "revision": args.source_revision,
        },
        "referenceToolchain": {
            "repository": args.reference_repository,
            "developmentBranch": args.reference_branch,
            "snapshot": args.reference_snapshot,
            "revision": args.reference_revision,
            "asset": args.reference_asset,
        },
        "compilerSupport": {
            "gcc": {
                "branch": args.gcc_branch,
                "releaseBearing": False,
            },
        },
    }
    if dependencies:
        metadata["dependencies"] = dependencies

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
