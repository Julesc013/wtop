#!/usr/bin/env python3
"""Validate Wtop's bounded OKF authoring profile and canonical catalogs.

Only the documented JSON-value YAML writer subset is accepted. This is not a
general YAML parser or general-purpose JSON Schema engine. No work-unit command
or external resource is executed. Python is development tooling only.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit

MAX_FILE_BYTES = 1024 * 1024
MAX_DOCUMENTS = 512
MAX_DEPTH = 32
CATALOGS = {
    "references/sources.json": ("sources", "sources"),
    "catalog/traceability.json": ("traceability", "requirements"),
    "catalog/acceptance.json": ("acceptance", "cases"),
    "catalog/evidence.json": ("evidence", "records"),
    "catalog/targets.json": ("targets", "targets"),
    "catalog/commands.json": ("commands", "commands"),
    "catalog/metrics.json": ("metrics", "metrics"),
    "work/units.json": ("work", "units"),
}
SCHEMA_KEYS = {
    "$schema", "$id", "$comment", "$defs", "title", "description", "type",
    "properties", "required", "additionalProperties", "items", "minItems",
    "maxItems", "uniqueItems", "minLength", "maxLength", "pattern", "format",
    "enum", "const", "minimum", "maximum",
}


class SpecError(ValueError):
    """An invalid or unverifiable local authoring input."""


def require(condition, message):
    if not condition:
        raise SpecError(message)


def pairs_no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key: " + key)
        result[key] = value
    return result


def bounded_value(value, depth=0):
    require(depth <= MAX_DEPTH, "maximum structured-data depth exceeded")
    if isinstance(value, dict):
        for child in value.values():
            bounded_value(child, depth + 1)
    elif isinstance(value, list):
        require(len(value) <= 10000, "maximum array length exceeded")
        for child in value:
            bounded_value(child, depth + 1)


def json_value(text, label):
    def invalid_constant(value):
        raise SpecError(label + ": nonfinite JSON value " + value)
    try:
        value = json.loads(text, object_pairs_hook=pairs_no_duplicates,
                           parse_constant=invalid_constant)
    except (json.JSONDecodeError, RecursionError) as error:
        raise SpecError(label + ": invalid JSON value: " + str(error)) from error
    bounded_value(value)
    return value


def contained(path, root, must_exist=True):
    try:
        resolved = path.resolve(strict=must_exist)
        resolved.relative_to(root.resolve())
    except (OSError, ValueError, RuntimeError) as error:
        raise SpecError("missing or escaping path: " + str(path)) from error
    return resolved


def read_text(path, root):
    resolved = contained(path, root)
    require(resolved.is_file(), "not a file: " + str(path))
    require(resolved.stat().st_size <= MAX_FILE_BYTES,
            "file exceeds bounded profile: " + str(path))
    try:
        return resolved.read_text(encoding="utf-8")
    except UnicodeError as error:
        raise SpecError("not UTF-8: " + str(path)) from error


def read_json(path, root):
    return json_value(read_text(path, root), str(path))


def parse_frontmatter(text, label):
    lines = text.splitlines()
    require(lines and lines[0] == "---", label + ": missing YAML frontmatter")
    try:
        end = lines.index("---", 1)
    except ValueError as error:
        raise SpecError(label + ": unclosed YAML frontmatter") from error
    require(end <= 128, label + ": frontmatter line limit exceeded")
    result = {}
    for line in lines[1:end]:
        require(bool(line.strip()), label + ": empty metadata line outside writer subset")
        match = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_-]*): (.+)", line)
        require(match is not None, label + ": metadata is outside JSON-value YAML writer subset")
        key, value = match.groups()
        require(key not in result, label + ": duplicate metadata key " + key)
        result[key] = json_value(value, label + ":" + key)
    return result, "\n".join(lines[end + 1:]).lstrip("\n")


def timestamp(value, label):
    try:
        parsed = dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
    except (ValueError, TypeError, AttributeError) as error:
        raise SpecError(label + ": invalid timestamp") from error
    require(parsed.tzinfo is not None and parsed.utcoffset() is not None,
            label + ": timestamp requires explicit UTC offset")
    return parsed


def check_schema(schema, label="schema"):
    require(isinstance(schema, dict), label + ": schema must be an object")
    unknown = set(schema) - SCHEMA_KEYS
    require(not unknown, label + ": unsupported schema keywords " + ", ".join(sorted(unknown)))
    for key in ("properties", "$defs"):
        if key in schema:
            require(isinstance(schema[key], dict), label + ": " + key + " must be object")
            for name, child in schema[key].items():
                check_schema(child, label + "/" + key + "/" + name)
    if "items" in schema:
        check_schema(schema["items"], label + "/items")
    if "additionalProperties" in schema:
        require(isinstance(schema["additionalProperties"], bool),
                label + ": only boolean additionalProperties is supported")
    if "format" in schema:
        require(schema["format"] == "date-time", label + ": unsupported format")
    if "type" in schema:
        types = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
        require(all(t in {"object", "array", "string", "integer", "number", "boolean", "null"}
                    for t in types), label + ": unsupported type")
    if "pattern" in schema:
        try:
            re.compile(schema["pattern"])
        except re.error as error:
            raise SpecError(label + ": malformed schema pattern") from error


def is_type(value, kind):
    return {
        "object": isinstance(value, dict),
        "array": isinstance(value, list),
        "string": isinstance(value, str),
        "integer": isinstance(value, int) and not isinstance(value, bool),
        "number": isinstance(value, (int, float)) and not isinstance(value, bool)
                  and (not isinstance(value, float) or math.isfinite(value)),
        "boolean": isinstance(value, bool),
        "null": value is None,
    }[kind]


def validate_schema(value, schema, label):
    if "type" in schema:
        types = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
        require(any(is_type(value, kind) for kind in types), label + ": wrong type")
    if "const" in schema:
        require(value == schema["const"], label + ": wrong constant")
    if "enum" in schema:
        require(value in schema["enum"], label + ": unknown enum/status " + str(value))
    if isinstance(value, dict):
        for key in schema.get("required", []):
            require(key in value, label + ": missing required " + key)
        properties = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            require(not (set(value) - set(properties)),
                    label + ": unknown keys " + ", ".join(sorted(set(value) - set(properties))))
        for key, child in value.items():
            if key in properties:
                validate_schema(child, properties[key], label + "." + key)
    elif isinstance(value, list):
        require(len(value) >= schema.get("minItems", 0), label + ": too few items")
        require(len(value) <= schema.get("maxItems", 10000), label + ": too many items")
        if schema.get("uniqueItems"):
            encoded = [json.dumps(v, sort_keys=True, ensure_ascii=False) for v in value]
            require(len(encoded) == len(set(encoded)), label + ": duplicate array items")
        if "items" in schema:
            for index, child in enumerate(value):
                validate_schema(child, schema["items"], label + "[" + str(index) + "]")
    elif isinstance(value, str):
        require(len(value) >= schema.get("minLength", 0), label + ": empty/short string")
        require(len(value) <= schema.get("maxLength", MAX_FILE_BYTES), label + ": long string")
        if "pattern" in schema:
            require(re.search(schema["pattern"], value) is not None, label + ": invalid format")
        if schema.get("format") == "date-time":
            timestamp(value, label)
    elif isinstance(value, (int, float)) and not isinstance(value, bool):
        if "minimum" in schema:
            require(value >= schema["minimum"], label + ": below minimum")
        if "maximum" in schema:
            require(value <= schema["maximum"], label + ": above maximum")


def unique_by_id(entries, label):
    result = {}
    for entry in entries:
        ident = entry["id"]
        require(ident not in result, label + ": duplicate ID " + ident)
        result[ident] = entry
    return result


def refs_exist(refs, owners, label):
    for ident in refs:
        require(ident in owners, label + ": unresolved ID " + ident)


def no_cycles(items, field, label):
    seen, active = set(), set()
    def visit(ident):
        require(ident not in active, label + ": dependency cycle at " + ident)
        if ident in seen:
            return
        active.add(ident)
        for other in items[ident][field]:
            visit(other)
        active.remove(ident)
        seen.add(ident)
    for ident in sorted(items):
        visit(ident)


def markdown_anchors(text):
    anchors, counts = set(), {}
    for line in text.splitlines():
        match = re.match(r"^#{1,6}\s+(.+?)\s*#*\s*$", line)
        if not match:
            continue
        base = re.sub(r"[^\w\- ]", "", match.group(1).lower()).replace(" ", "-")
        count = counts.get(base, 0)
        counts[base] = count + 1
        anchors.add(base if not count else base + "-" + str(count))
    anchors.update(re.findall(r"^\[\^([^]]+)\]:", text, re.MULTILINE))
    return anchors


def json_ids(value):
    result = set()
    if isinstance(value, dict):
        if isinstance(value.get("id"), str):
            result.add(value["id"])
        for child in value.values():
            result.update(json_ids(child))
    elif isinstance(value, list):
        for child in value:
            result.update(json_ids(child))
    return result


def validate_link(link, origin, spec, repo):
    parsed = urlsplit(link)
    if parsed.scheme:
        require(parsed.scheme in {"https", "http", "urn", "mailto"},
                str(origin) + ": unsupported link scheme " + parsed.scheme)
        return
    require(not parsed.netloc, str(origin) + ": network-relative link forbidden")
    raw = unquote(parsed.path)
    require("\\" not in raw and "\x00" not in raw, str(origin) + ": invalid link path")
    target = (spec / raw.lstrip("/")) if raw.startswith("/") else (origin.parent / raw)
    if not raw:
        target = origin
    target = contained(target, repo)
    require(target.exists(), str(origin) + ": broken link " + link)
    if parsed.fragment:
        fragment = unquote(parsed.fragment)
        if target.suffix == ".md":
            require(fragment in markdown_anchors(read_text(target, repo)),
                    str(origin) + ": unresolved Markdown anchor " + link)
        elif target.suffix == ".json":
            require(fragment in json_ids(read_json(target, repo)),
                    str(origin) + ": unresolved registry anchor " + link)


def rendered_index(concepts):
    lines = ["---", 'okf_version: "0.2"', "---", "", "# Wtop specification", "",
             "Generated by tools/specctl.py index from authored concept metadata.",
             "Product behavior remains subject to its acceptance and evidence records.", ""]
    groups = {}
    for concept in concepts.values():
        path = concept["path"]
        group = path.split("/")[0] if "/" in path else "start"
        groups.setdefault(group, []).append(concept)
    for group in sorted(groups, key=lambda x: (x != "start", x)):
        lines += ["## " + group.replace("-", " ").title(), ""]
        for concept in sorted(groups[group], key=lambda c: c["path"]):
            meta = concept["meta"]
            lines.append("- [" + meta["title"] + "](" + concept["path"] + ") - "
                         + meta["description"] + " (" + meta["wtop"]["id"] + ")")
        lines.append("")
    lines += ["## Canonical registries", "",
              "- [Targets](catalog/targets.json) - ABI, CPU, OS and qualification.",
              "- [Commands](catalog/commands.json) - Invocation, arguments and implementation state.",
              "- [Metrics](catalog/metrics.json) - Stable metric IDs, units and definitions.",
              "- [Requirements](catalog/traceability.json) - Requirement to acceptance mapping.",
              "- [Acceptance](catalog/acceptance.json) - Independent oracles and execution state.",
              "- [Evidence](catalog/evidence.json) - Actual source-bound results only.",
              "- [Work units](work/units.json) - Dependency-ordered implementation.",
              "- [Sources](references/sources.json) - Provenance and observed limits.",
              "- [Update log](log.md) - Dated changes.", ""]
    return "\n".join(lines)


def verify_file_identity(identity, repo, label):
    raw = identity["path"]
    require(not Path(raw).is_absolute(), label + ": absolute evidence path forbidden")
    path = contained(repo / raw, repo)
    require(path.is_file(), label + ": missing evidence file " + raw)
    require(raw != "spec/catalog/evidence.json", label + ": evidence cannot hash itself")
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    require(digest.hexdigest() == identity["sha256"], label + ": hash mismatch " + raw)


def load_bundle(repo, require_index=True):
    repo = repo.resolve()
    spec = contained(repo / "spec", repo)
    front_schema = read_json(spec / "schemas/frontmatter.schema.json", repo)
    catalogs_schema = read_json(spec / "schemas/catalogs.schema.json", repo)
    check_schema(front_schema)
    check_schema(catalogs_schema)
    documents = sorted(spec.rglob("*.md"))
    require(len(documents) <= MAX_DOCUMENTS, "too many concept documents")
    concepts, paths = {}, set()
    for path in documents:
        relative = path.relative_to(spec).as_posix()
        text = read_text(path, repo)
        if path.name == "index.md":
            if path == spec / "index.md":
                meta, _ = parse_frontmatter(text, relative)
                require(meta == {"okf_version": "0.2"}, "root index permits only okf_version 0.2")
            else:
                require(not text.startswith("---\n"), relative + ": nested index has frontmatter")
            continue
        if path.name == "log.md":
            require(not text.startswith("---\n"), relative + ": log must not have concept frontmatter")
            headings = re.findall(r"^##\s+(.+)$", text, re.MULTILINE)
            require(bool(headings), relative + ": log needs dated headings")
            for heading in headings:
                try:
                    dt.date.fromisoformat(heading)
                except ValueError as error:
                    raise SpecError(relative + ": invalid log date") from error
            require(headings == sorted(headings, reverse=True), relative + ": log dates not newest first")
            continue
        meta, body = parse_frontmatter(text, relative)
        validate_schema(meta, front_schema, relative)
        ident = meta["wtop"]["id"]
        require(ident not in concepts, "duplicate concept ID " + ident)
        headings = re.findall(r"^### (WT-REQ-[A-Z0-9-]+)\s*$", body, re.MULTILINE)
        require(len(headings) == len(set(headings)), relative + ": duplicate requirement heading")
        require(set(headings) == set(meta["wtop"]["requirements"]),
                relative + ": declared requirement IDs do not match headings")
        require(body.startswith("# "), relative + ": missing concept heading")
        concepts[ident] = {"path":relative, "file":path, "meta":meta, "body":body}
        paths.add(relative)
    require(bool(concepts), "empty specification bundle")

    catalogs = {}
    for path, (name, field) in CATALOGS.items():
        data = read_json(spec / path, repo)
        expected = "../schemas/catalogs.schema.json#/$defs/" + name
        require(data.get("$schema") == expected, path + ": unexpected schema binding")
        validate_schema(data, catalogs_schema["$defs"][name], path)
        catalogs[name] = unique_by_id(data[field], name)
    reqs, cases = catalogs["traceability"], catalogs["acceptance"]
    evidence, units = catalogs["evidence"], catalogs["work"]
    sources = catalogs["sources"]
    declared = {}
    for ident, concept in concepts.items():
        meta, body = concept["meta"], concept["body"]
        deps = meta["wtop"]["depends_on"]
        refs_exist(deps, concepts, ident)
        require(ident not in deps, ident + ": self dependency")
        for rid in meta["wtop"]["requirements"]:
            require(rid not in declared, "duplicate requirement ID " + rid)
            declared[rid] = ident
        source_keys = [s["id"] for s in meta["sources"]]
        require(len(source_keys) == len(set(source_keys)), ident + ": duplicate source IDs")
        refs_exist(source_keys, sources, ident)
        for source in meta["sources"]:
            validate_link(source["resource"], concept["file"], spec, repo)
            require(source["resource"].endswith("#" + source["id"]),
                    ident + ": source ID/resource mismatch")
        footnotes = set(re.findall(r"\[\^([^]]+)\]", body))
        definitions = set(re.findall(r"^\[\^([^]]+)\]:", body, re.MULTILINE))
        require(footnotes <= definitions, ident + ": missing footnote definition")
        refs_exist(footnotes, {key: True for key in source_keys}, ident + " footnotes")
    no_cycles({i:c["meta"]["wtop"] for i,c in concepts.items()}, "depends_on", "concepts")
    require(set(reqs) == set(declared), "requirement registry does not match concept requirements")
    for rid, entry in reqs.items():
        require(entry["concept"] == declared[rid], rid + ": wrong concept owner")
        refs_exist(entry["acceptance"], cases, rid)
        for aid in entry["acceptance"]:
            require(rid in cases[aid]["requirements"], rid + ": acceptance reverse link missing")
    for aid, entry in cases.items():
        refs_exist(entry["requirements"], reqs, aid)
        refs_exist(entry["evidence"], evidence, aid)
        for rid in entry["requirements"]:
            require(aid in reqs[rid]["acceptance"], aid + ": requirement reverse link missing")
        if entry["status"] in {"passed", "failed", "inconclusive"}:
            require(bool(entry["evidence"]), aid + ": claimed result lacks evidence")
        if entry["status"] == "not_run":
            require(not entry["evidence"], aid + ": not_run case has execution evidence")
        if entry["status"] == "passed":
            require(any(evidence[e]["result"] == "passed"
                        and evidence[e]["kind"] == entry["method"]
                        and aid in evidence[e]["acceptance"] for e in entry["evidence"]),
                    aid + ": no matching passed evidence")
    for eid, entry in evidence.items():
        refs_exist(entry["acceptance"], cases, eid)
        refs_exist(entry["targets"], catalogs["targets"], eid)
        refs_exist(entry["commands"], catalogs["commands"], eid)
        require(timestamp(entry["ended_at"], eid) >= timestamp(entry["started_at"], eid),
                eid + ": reversed execution times")
        if entry["result"] == "passed" and entry["kind"] != "review":
            require(entry["exit_code"] == 0, eid + ": passed execution has nonzero/unknown exit")
        for identity in entry["inputs"] + [entry["log"]] + entry["artifacts"]:
            verify_file_identity(identity, repo, eid)
        if entry["kind"] in {"native", "artifact"}:
            require(bool(entry["artifacts"]), eid + ": native/artifact evidence lacks artifact identity")
    for uid, entry in units.items():
        refs_exist(entry["dependencies"], units, uid)
        refs_exist(entry["context"], concepts, uid)
        refs_exist(entry["acceptance"], cases, uid)
        refs_exist(entry["evidence"], evidence, uid)
        for path in entry["allowed_paths"]:
            require(not Path(path).is_absolute() and ".." not in Path(path).parts,
                    uid + ": escaping allowed path")
        if entry["status"] == "completed":
            require(bool(entry["evidence"]), uid + ": completed work lacks evidence")
            require(all(units[d]["status"] == "completed" for d in entry["dependencies"]),
                    uid + ": completed work has incomplete dependencies")
            require(all(cases[a]["status"] == "passed" for a in entry["acceptance"]),
                    uid + ": completed work has unpassed acceptance")
    no_cycles(units, "dependencies", "work")
    tokens, profiles = set(), set()
    for cid, entry in catalogs["commands"].items():
        refs_exist(entry["acceptance"], cases, cid)
        refs_exist(entry["evidence"], evidence, cid)
        for token in entry["tokens"]:
            require(token not in tokens, cid + ": duplicate command token " + token)
            tokens.add(token)
        if "minimum" in entry and "maximum" in entry:
            require(entry["minimum"] <= entry["maximum"], cid + ": reversed argument bounds")
        if entry["status"] == "implemented":
            require(any(evidence[e]["result"] == "passed" and cid in evidence[e]["commands"]
                        for e in entry["evidence"]),
                    cid + ": implemented command lacks evidence")
    for tid, entry in catalogs["targets"].items():
        require(entry["profile"] not in profiles, tid + ": duplicate artifact profile")
        profiles.add(entry["profile"])
        refs_exist(entry["evidence"], evidence, tid)
        if entry["qualification"] in {"runtime_tested", "qualified"}:
            require(any(evidence[e]["result"] == "passed" and evidence[e]["kind"] == "native"
                        and tid in evidence[e]["targets"]
                        for e in entry["evidence"]), tid + ": runtime claim lacks native evidence")
        if entry["artifact_status"] != "planned":
            require(any(tid in evidence[e]["targets"] for e in entry["evidence"]),
                    tid + ": artifact claim lacks target-bound evidence")
    for path in documents:
        # The strict local writer requires resolvable links. Upstream OKF's
        # permissive consumer rules are intentionally a different contract.
        if path == spec / "index.md" and not require_index:
            continue
        text = read_text(path, repo)
        for link in re.findall(r"(?<!!)\[[^\]\n]*\]\(([^\s)]+)\)", text):
            candidate = urlsplit(link)
            if (not require_index and not candidate.scheme
                    and (path.parent / candidate.path).resolve() == spec / "index.md"):
                continue
            validate_link(link, path, spec, repo)
    index = rendered_index(concepts)
    if require_index:
        require(read_text(spec / "index.md", repo) == index,
                "stale index: run python tools/specctl.py index --write")
    return {"repo":repo, "spec":spec, "concepts":concepts, "catalogs":catalogs, "index":index}


def status_view(bundle):
    c = bundle["catalogs"]
    def counts(items, field):
        result = {}
        for item in items.values():
            value = item[field]
            result[value] = result.get(value, 0) + 1
        return dict(sorted(result.items()))
    return {
        "schema":"wtop.spec-status/1",
        "concepts":len(bundle["concepts"]),
        "requirements":len(c["traceability"]),
        "acceptance":counts(c["acceptance"], "status"),
        "work":counts(c["work"], "status"),
        "commands":counts(c["commands"], "status"),
        "targets":counts(c["targets"], "qualification"),
        "evidence_records":len(c["evidence"]),
        "limitation":"Structural validation does not qualify native behavior or release support.",
    }


def next_view(bundle):
    units = bundle["catalogs"]["work"]
    active = [u for u in units.values() if u["status"] == "active"]
    ready = [u for u in units.values() if u["status"] == "planned"
             and all(units[d]["status"] == "completed" for d in u["dependencies"])]
    def compact(unit):
        return {key:unit[key] for key in ("id","title","status","context","deliverables","acceptance")}
    return {"schema":"wtop.next/1","active":[compact(u) for u in active],
            "ready":[compact(u) for u in ready],
            "note":"This is a dependency/status projection, not execution or a new permission grant."}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("validate","index","status","next"))
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--write", action="store_true", help="write deterministic index (index only)")
    args = parser.parse_args(argv)
    try:
        require(not args.write or args.command == "index", "--write is only valid for index")
        bundle = load_bundle(args.root, require_index=args.command != "index")
        if args.command == "index":
            if args.write:
                (bundle["spec"] / "index.md").write_text(bundle["index"], encoding="utf-8")
                print("Wrote spec/index.md from authored metadata.")
            else:
                sys.stdout.write(bundle["index"])
        elif args.command == "validate":
            c = bundle["catalogs"]
            print("Valid Wtop writer-profile structure: %d concepts, %d requirements, %d acceptance cases."
                  % (len(bundle["concepts"]),len(c["traceability"]),len(c["acceptance"])))
            print("No product/native/release qualification is created by this check.")
        else:
            result = status_view(bundle) if args.command == "status" else next_view(bundle)
            print(json.dumps(result, indent=2))
        return 0
    except (SpecError, OSError, KeyError, TypeError) as error:
        print("specctl: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
