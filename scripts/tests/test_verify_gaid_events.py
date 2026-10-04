"""检查验收脚本实际使用的 GAID 判定函数，防止仅安装或缺类被记为通过。"""

import pathlib
import subprocess
import tempfile
import unittest


LIBRARY = pathlib.Path(__file__).resolve().parents[1] / "verify_gaid_events.sh"


class GaidEventTest(unittest.TestCase):
    def check_events(self, events, required, expected_code, expected_message):
        with tempfile.TemporaryDirectory() as directory:
            log = pathlib.Path(directory) / "events.log"
            log.write_text(events, encoding="utf-8")
            result = subprocess.run(
                ["bash", "-c", 'source "$1"; verify_gaid_events "$2" "$3"',
                 "verify", str(LIBRARY), str(log), "true" if required else "false"],
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(expected_code, result.returncode, result.stderr)
            self.assertEqual(expected_message, result.stdout.strip())

    def test_required_installation_only_fails(self):
        self.check_events("GAID_HOOK_INSTALLED\n", True, 1, "FAIL gaid=NOT_APPLIED")

    def test_required_missing_class_fails(self):
        self.check_events("GAID_HOOK_SKIPPED reason=CLASS_NOT_FOUND\n", True, 1, "FAIL gaid=NOT_APPLIED")

    def test_optional_missing_class_is_skipped(self):
        self.check_events("GAID_HOOK_SKIPPED reason=CLASS_NOT_FOUND\n", False, 0,
                          "SKIP gaid=CLASS_OR_METHOD_NOT_FOUND")

    def test_optional_installation_only_is_unverified(self):
        self.check_events("GAID_HOOK_INSTALLED\n", False, 0, "NOT_VERIFIED gaid=NO_APPLIED_EVENT")

    def test_empty_log_is_unverified(self):
        self.check_events("", False, 0, "NOT_VERIFIED gaid=NO_APPLIED_EVENT")

    def test_required_empty_log_fails(self):
        self.check_events("", True, 1, "FAIL gaid=NOT_APPLIED")

    def test_applied_event_passes(self):
        self.check_events("GAID_HOOK_INSTALLED\nGAID_HOOK_APPLIED\n", True, 0, "PASS gaid=APPLIED")

    def test_installation_failure_takes_precedence_over_applied(self):
        self.check_events("GAID_HOOK_FAILED\nGAID_HOOK_APPLIED\n", False, 1, "FAIL gaid=INSTALL_FAILED")


if __name__ == "__main__":
    unittest.main()
