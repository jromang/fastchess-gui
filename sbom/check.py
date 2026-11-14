#!/usr/bin/env python3
"""The SBOM (sbom/fastchess-gui.cdx.json) against what the build really uses.

    check.py              checks that every FetchContent_Declare of CMakeLists.txt is in the SBOM with the same pin
                          (its GIT_TAG, or its URL and URL_HASH), that the SBOM lists no dependency the build dropped,
                          and that the versions of app/third_party's libraries are the SBOM's. Fails on a difference.
    check.py checkouts DIR
                          makes, in DIR, a git repository at each component's commit (fetched alone, without its
                          files), for OSV-Scanner: it reads a C or C++ library's vulnerabilities by commit, which an
                          SBOM cannot give it.

Standard library only, so that it runs anywhere CMake does.
"""

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SBOM = ROOT / "sbom" / "fastchess-gui.cdx.json"


def properties(component):
    return {p["name"]: p["value"] for p in component.get("properties", [])}


def declarations(cmake_text):
    """Every FetchContent_Declare: its name and its arguments, by keyword."""
    out = {}
    for name, body in re.findall(r"FetchContent_Declare\(\s*(\w+)(.*?)\)", cmake_text, re.S):
        words = body.split()
        args = {}
        for key in ("GIT_REPOSITORY", "GIT_TAG", "URL", "URL_HASH"):
            if key in words and words.index(key) + 1 < len(words):
                args[key] = words[words.index(key) + 1]
        out[name] = args
    return out


def header_versions():
    """The versions app/third_party's headers state, by bom-ref."""
    tp = ROOT / "app" / "third_party"
    found = {}
    if m := re.search(r"version (\d+\.\d+\.\d+)", (tp / "json.hpp").read_text(errors="replace")[:2000]):
        found["app:json"] = m.group(1)
    fmt = (tp / "fmt" / "include" / "fmt" / "core.h").read_text(errors="replace")
    fmt += (tp / "fmt" / "include" / "fmt" / "base.h").read_text(errors="replace")
    if m := re.search(r"#define FMT_VERSION (\d+)", fmt):
        v = int(m.group(1))
        found["app:fmt"] = f"{v // 10000}.{v // 100 % 100}.{v % 100}"
    doctest = (tp / "doctest" / "doctest.hpp").read_text(errors="replace")
    parts = [re.search(rf"#define DOCTEST_VERSION_{p} (\d+)", doctest) for p in ("MAJOR", "MINOR", "PATCH")]
    if all(parts):
        found["app:doctest"] = ".".join(p.group(1) for p in parts)
    if m := re.search(r"VERSION: (\d+\.\d+\.\d+)", (tp / "chess.hpp").read_text(errors="replace")[:4000]):
        found["app:chess-library"] = m.group(1)
    expected = (tp / "expected.hpp").read_text(errors="replace")
    parts = [re.search(rf"#define TL_EXPECTED_VERSION_{p} (\d+)", expected) for p in ("MAJOR", "MINOR", "PATCH")]
    if all(parts):
        found["app:expected"] = ".".join(p.group(1) for p in parts)
    return found


def check(sbom):
    errors = []
    components = {c.get("bom-ref"): c for c in sbom["components"]}
    declared = declarations((ROOT / "CMakeLists.txt").read_text())
    if not declared:
        errors.append("CMakeLists.txt: no FetchContent_Declare found")

    for name, args in declared.items():
        c = components.get(f"fetchcontent:{name}")
        if c is None:
            errors.append(f"FetchContent {name}: missing from the SBOM (a component with bom-ref fetchcontent:{name})")
            continue
        props = properties(c)
        refs = {(r["type"], r["url"]) for r in c.get("externalReferences", [])}
        if "GIT_TAG" in args:
            if props.get("fastchess-gui:pin") != args["GIT_TAG"]:
                errors.append(f"FetchContent {name}: GIT_TAG {args['GIT_TAG']}, the SBOM says "
                              f"{props.get('fastchess-gui:pin')}")
            if ("vcs", args.get("GIT_REPOSITORY")) not in refs:
                errors.append(f"FetchContent {name}: the SBOM has no vcs reference to {args.get('GIT_REPOSITORY')}")
        elif "URL" in args:
            if props.get("fastchess-gui:pin") != args["URL"] or ("distribution", args["URL"]) not in refs:
                errors.append(f"FetchContent {name}: URL {args['URL']}, the SBOM says {props.get('fastchess-gui:pin')}")
            want = args.get("URL_HASH", "").removeprefix("SHA256=").lower()
            have = {h["content"].lower() for h in c.get("hashes", []) if h["alg"] == "SHA-256"}
            if want and want not in have:
                errors.append(f"FetchContent {name}: URL_HASH SHA256={want} is not the SBOM's")
        else:
            errors.append(f"FetchContent {name}: neither GIT_TAG nor URL, unpinned")
        if not c.get("licenses"):
            errors.append(f"FetchContent {name}: no license in the SBOM")

    for ref in components:
        if ref and ref.startswith("fetchcontent:") and ref.removeprefix("fetchcontent:") not in declared:
            errors.append(f"SBOM component {ref}: no such FetchContent_Declare in CMakeLists.txt any more")

    for ref, version in header_versions().items():
        c = components.get(ref)
        if c is None:
            errors.append(f"app/third_party: {ref} {version} is missing from the SBOM")
        elif not c["version"].startswith(version):
            errors.append(f"app/third_party: {ref} is {version}, the SBOM says {c['version']}")

    for c in sbom["components"]:
        for key in ("name", "version", "purl", "licenses"):
            if not c.get(key):
                errors.append(f"SBOM component {c.get('bom-ref')}: no {key}")
    return errors


def checkouts(sbom, directory):
    directory.mkdir(parents=True, exist_ok=True)
    for c in sbom["components"]:
        commit = properties(c).get("fastchess-gui:commit")
        vcs = next((r["url"] for r in c.get("externalReferences", []) if r["type"] == "vcs"), None)
        if not commit or not vcs:
            continue
        repo = directory / c["bom-ref"].replace(":", "-")
        subprocess.run(["git", "init", "-q", str(repo)], check=True)
        # The commit object alone: OSV-Scanner reads HEAD's commit, nothing else.
        subprocess.run(["git", "-C", str(repo), "fetch", "-q", "--depth=1", "--filter=tree:0", vcs, commit], check=True)
        subprocess.run(["git", "-C", str(repo), "update-ref", "HEAD", "FETCH_HEAD"], check=True)
        print(f"{c['name']} {c['version']}: {commit}")


def main():
    sbom = json.loads(SBOM.read_text())
    if len(sys.argv) == 3 and sys.argv[1] == "checkouts":
        checkouts(sbom, Path(sys.argv[2]))
        return 0
    if len(sys.argv) != 1:
        print(__doc__)
        return 2
    errors = check(sbom)
    for e in errors:
        print(f"error: {e}")
    if not errors:
        print(f"SBOM up to date: {len(sbom['components'])} components")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
