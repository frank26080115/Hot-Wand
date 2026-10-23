"""Offline companion entry point; records exact test-to-family evidence."""
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]


class EvidenceResult(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.evidence = {}
        self.failed_ids = set()
        self.skipped_ids = set()

    def addFailure(self, test, err):
        self.failed_ids.add(test.id())
        super().addFailure(test, err)

    def addError(self, test, err):
        self.failed_ids.add(test.id())
        super().addError(test, err)

    def addSubTest(self, test, subtest, err):
        if err:
            self.failed_ids.add(test.id())
        super().addSubTest(test, subtest, err)

    def addSkip(self, test, reason):
        self.skipped_ids.add(test.id())
        super().addSkip(test, reason)

    def stopTest(self, test):
        method = getattr(test, test._testMethodName, None)
        for family in getattr(method, "functional_families", ()):
            self.evidence.setdefault(family, []).append({"test": test.id(),
                "status": "BLOCKED" if test.id() in self.skipped_ids else "FAIL" if test.id() in self.failed_ids else "PASS"})
        super().stopTest(test)


def main(argv=None):
    arguments = sys.argv[1:] if argv is None else argv
    suite = unittest.defaultTestLoader.discover(str(ROOT / "test/functional"))
    def contracts(node):
        if isinstance(node, unittest.TestSuite):
            return [item for child in node for item in contracts(child)]
        method = getattr(node, node._testMethodName, None)
        return [(node.id(), family) for family in getattr(method, "functional_families", ())]
    expected = contracts(suite)
    result = unittest.TextTestRunner(verbosity=2, resultclass=EvidenceResult).run(suite)
    # setUpClass failure prevents stopTest() for its methods. Missing checks
    # must remain ERROR even when another method in that family passed.
    for identifier, family in expected:
        if not any(check["test"] == identifier for check in result.evidence.get(family, [])):
            result.evidence.setdefault(family, []).append({"test": identifier, "status": "ERROR"})
    Path(arguments[0]).write_text(json.dumps(result.evidence, indent=2) + "\n", encoding="utf-8", newline="\r\n")
    return 0 if result.wasSuccessful() and not result.skipped_ids else 1


if __name__ == "__main__":
    sys.exit(main())
