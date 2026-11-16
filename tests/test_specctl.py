"""Adversarial checks for the local specification validator, not product claims."""

import hashlib
import importlib.util
import json
import shutil
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
MODULE = importlib.util.spec_from_file_location("wtop_specctl", REPO / "tools/specctl.py")
specctl = importlib.util.module_from_spec(MODULE)
MODULE.loader.exec_module(specctl)


class SpecificationValidationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="wtop-spec-test-")
        self.root = Path(self.temp.name)
        shutil.copytree(REPO / "spec", self.root / "spec")

    def tearDown(self):
        self.temp.cleanup()

    def edit_json(self, path, change):
        file = self.root / "spec" / path
        data = json.loads(file.read_text(encoding="utf-8"))
        change(data)
        file.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")

    def assert_invalid(self, fragment):
        with self.assertRaisesRegex(specctl.SpecError, fragment):
            specctl.load_bundle(self.root)

    def test_validation_does_not_create_product_passes_or_advance_work(self):
        before = (self.root / "spec/catalog/acceptance.json").read_bytes()
        work_before = (self.root / "spec/work/units.json").read_bytes()
        bundle = specctl.load_bundle(self.root)
        status = specctl.status_view(bundle)
        self.assertEqual(sum(status["acceptance"].values()),
                         len(bundle["catalogs"]["acceptance"]))
        self.assertEqual((self.root / "spec/catalog/acceptance.json").read_bytes(), before)
        self.assertEqual((self.root / "spec/work/units.json").read_bytes(), work_before)
        for unit in specctl.next_view(bundle)["ready"]:
            source = bundle["catalogs"]["work"][unit["id"]]
            self.assertTrue(all(bundle["catalogs"]["work"][dep]["status"] == "completed"
                                for dep in source["dependencies"]))

    def test_duplicate_frontmatter_key_is_rejected(self):
        path = self.root / "spec/START-HERE.md"
        text = path.read_text(encoding="utf-8")
        path.write_text(text.replace('type: "Wtop Guide"', 'type: "Wtop Guide"\ntype: "Other"', 1),
                        encoding="utf-8")
        self.assert_invalid("duplicate metadata")

    def test_yaml_alias_is_outside_safe_writer_subset(self):
        path = self.root / "spec/START-HERE.md"
        text = path.read_text(encoding="utf-8")
        path.write_text(text.replace('title: "Start here"', "title: *shared", 1), encoding="utf-8")
        self.assert_invalid("invalid JSON")

    def test_unknown_safe_extension_is_preserved(self):
        path = self.root / "spec/START-HERE.md"
        text = path.read_text(encoding="utf-8")
        path.write_text(text.replace("---\n", '---\nx_future: {"note": "preserved"}\n', 1),
                        encoding="utf-8")
        bundle = specctl.load_bundle(self.root)
        self.assertEqual(bundle["concepts"]["WT-000"]["meta"]["x_future"], {"note":"preserved"})

    def test_duplicate_json_key_is_rejected(self):
        path = self.root / "spec/catalog/commands.json"
        text = path.read_text(encoding="utf-8")
        path.write_text(text.replace('"commands": [', '"commands": [], "commands": [', 1),
                        encoding="utf-8")
        self.assert_invalid("duplicate JSON key")

    def test_unknown_status_is_rejected(self):
        self.edit_json("catalog/commands.json", lambda d:d["commands"][0].update(status="probably_done"))
        self.assert_invalid("unknown enum/status")

    def test_duplicate_registry_id_is_rejected(self):
        self.edit_json("catalog/targets.json", lambda d:d["targets"].append(dict(d["targets"][0])))
        self.assert_invalid("duplicate ID")

    def test_unresolved_requirement_reference_is_rejected(self):
        self.edit_json("catalog/traceability.json",
                       lambda d:d["requirements"][0].update(acceptance=["WT-AC-MISSING"]))
        self.assert_invalid("unresolved ID")

    def test_requirement_owner_mismatch_is_rejected(self):
        self.edit_json("catalog/traceability.json",
                       lambda d:d["requirements"][0].update(concept="WT-002"))
        self.assert_invalid("wrong concept owner")

    def test_work_dependency_cycle_is_rejected(self):
        self.edit_json("work/units.json",
                       lambda d:d["units"][0].update(dependencies=["WT-W010"]))
        self.assert_invalid("dependency cycle")

    def test_broken_local_link_is_rejected(self):
        path = self.root / "spec/START-HERE.md"
        with path.open("a", encoding="utf-8") as handle:
            handle.write("\n[Missing](not-present.md)\n")
        self.assert_invalid("missing or escaping path")

    def test_path_escape_is_rejected_even_when_destination_exists(self):
        outside = self.root.parent / (self.root.name + "-outside.txt")
        outside.write_text("not part of the project", encoding="utf-8")
        try:
            path = self.root / "spec/START-HERE.md"
            with path.open("a", encoding="utf-8") as handle:
                handle.write("\n[Escape](../../" + outside.name + ")\n")
            self.assert_invalid("missing or escaping path")
        finally:
            outside.unlink()

    def test_symlink_escape_is_rejected(self):
        outside = self.root.parent / (self.root.name + "-outside.txt")
        outside.write_text("outside", encoding="utf-8")
        link = self.root / "spec/linked.txt"
        try:
            try:
                link.symlink_to(outside)
            except (OSError, NotImplementedError):
                self.skipTest("host cannot create symlinks")
            path = self.root / "spec/START-HERE.md"
            with path.open("a", encoding="utf-8") as handle:
                handle.write("\n[Escape](linked.txt)\n")
            self.assert_invalid("missing or escaping path")
        finally:
            outside.unlink()

    def test_nonexistent_registry_anchor_is_rejected(self):
        path = self.root / "spec/START-HERE.md"
        with path.open("a", encoding="utf-8") as handle:
            handle.write("\n[Unknown source](references/sources.json#absent)\n")
        self.assert_invalid("unresolved registry anchor")

    def test_pass_without_evidence_is_rejected(self):
        self.edit_json("catalog/acceptance.json",
                       lambda d:d["cases"][0].update(status="passed"))
        self.assert_invalid("claimed result lacks evidence")

    def test_native_support_without_native_evidence_is_rejected(self):
        self.edit_json("catalog/targets.json",
                       lambda d:d["targets"][0].update(qualification="qualified"))
        self.assert_invalid("runtime claim lacks native evidence")

    def test_completed_work_without_evidence_is_rejected(self):
        self.edit_json("work/units.json",
                       lambda d:d["units"][0].update(status="completed"))
        self.assert_invalid("completed work lacks evidence")

    def test_stale_index_is_rejected_and_regeneration_is_deterministic(self):
        path = self.root / "spec/index.md"
        path.write_text(path.read_text(encoding="utf-8") + "\nStale.\n", encoding="utf-8")
        self.assert_invalid("stale index")
        first = specctl.load_bundle(self.root, require_index=False)["index"]
        second = specctl.load_bundle(self.root, require_index=False)["index"]
        self.assertEqual(first, second)
        path.write_text(first, encoding="utf-8")
        specctl.load_bundle(self.root)

    def test_unsupported_schema_keyword_cannot_be_silently_ignored(self):
        self.edit_json("schemas/frontmatter.schema.json", lambda d:d.update({"not":{}}))
        self.assert_invalid("unsupported schema keywords")

    def test_nonfinite_and_deep_json_are_rejected(self):
        with self.assertRaisesRegex(specctl.SpecError, "nonfinite"):
            specctl.json_value('{"value": NaN}', "fixture")
        with self.assertRaisesRegex(specctl.SpecError, "depth"):
            specctl.json_value("[" * 34 + "0" + "]" * 34, "fixture")

    def test_exact_evidence_identity_detects_mutation(self):
        file = self.root / "proof.txt"
        original = b"original test transcript\n"
        file.write_bytes(original)
        identity = {"path":"proof.txt", "sha256":hashlib.sha256(original).hexdigest()}
        specctl.verify_file_identity(identity, self.root, "fixture")
        file.write_bytes(b"changed transcript\n")
        with self.assertRaisesRegex(specctl.SpecError, "hash mismatch"):
            specctl.verify_file_identity(identity, self.root, "fixture")

    def fixture_evidence(self):
        data = b"synthetic validator fixture; no product or native qualification\n"
        file = self.root / "fixture-record.txt"
        file.write_bytes(data)
        identity = {"path":"fixture-record.txt", "sha256":hashlib.sha256(data).hexdigest()}
        return {
            "id":"WT-EV-FIXTURE","kind":"native","result":"passed",
            "subject":"validator fixture only","source_revision":"0"*40,
            "environment":"synthetic test","command":"fixture",
            "started_at":"2026-11-16T00:00:00Z","ended_at":"2026-11-16T00:00:01Z",
            "exit_code":0,"inputs":[identity],"log":identity,"artifacts":[identity],
            "acceptance":["WT-AC-020-03"],"limitations":["Not native proof; validator fixture"],
            "targets":["WT-TGT-WIN32-MODERN"],"commands":["WT-CMD-SNAPSHOT"],
        }

    def test_native_record_cannot_qualify_another_target(self):
        record = self.fixture_evidence()
        self.edit_json("catalog/evidence.json", lambda d:d["records"].append(record))
        self.edit_json("catalog/targets.json",
                       lambda d:d["targets"][0].update(qualification="qualified",
                                                        evidence=["WT-EV-FIXTURE"]))
        self.assert_invalid("runtime claim lacks native evidence")

    def test_record_for_another_command_cannot_promote_help(self):
        record = self.fixture_evidence()
        self.edit_json("catalog/evidence.json", lambda d:d["records"].append(record))
        def promote(data):
            entry = next(c for c in data["commands"] if c["id"] == "WT-CMD-HELP")
            entry.update(status="implemented", evidence=["WT-EV-FIXTURE"])
        self.edit_json("catalog/commands.json", promote)
        self.assert_invalid("implemented command lacks evidence")

    def test_wrong_boolean_type_does_not_satisfy_integer_schema(self):
        with self.assertRaisesRegex(specctl.SpecError, "wrong type"):
            specctl.validate_schema(True, {"type":"integer"}, "fixture")


if __name__ == "__main__":
    unittest.main()
