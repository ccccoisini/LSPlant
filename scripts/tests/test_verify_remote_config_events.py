"""检查配置验收的完整快照匹配，防止把其他应用、旧状态或安装日志记为更新成功。"""

import pathlib
import subprocess
import tempfile
import unittest


LIBRARY = pathlib.Path(__file__).resolve().parents[1] / "verify_remote_config_events.sh"
ANDROID_ID = "1111111111111111"
GAID = "11111111-1111-1111-1111-111111111111"


def event(marker="REMOTE_CONFIG_UPDATED", group="settings.com.example.target",
          package="com.example.target", process="com.example.target", enabled="true",
          android_id=ANDROID_ID, gaid=GAID):
    return (f"10-05 08:00:00.000 123 456 I HookTemplate.Config: {marker} group={group} "
            f"enabled={enabled} android_id={android_id} gaid={gaid} "
            f"package={package} process={process} feature=remote-config hook=-\n")


class RemoteConfigEventTest(unittest.TestCase):
    def check_events(self, events, expected_message):
        with tempfile.TemporaryDirectory() as directory:
            log = pathlib.Path(directory) / "events.log"
            log.write_text(events, encoding="utf-8")
            result = subprocess.run(
                ["bash", "-c", 'source "$1"; verify_remote_config_snapshot "$2" "$3" "$4" "$5" "$6" "$7" "$8"',
                 "verify", str(LIBRARY), str(log), "REMOTE_CONFIG_UPDATED", "settings.com.example.target",
                 "com.example.target", "true", ANDROID_ID, GAID],
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(0 if expected_message.startswith("PASS") else 1, result.returncode, result.stderr)
            self.assertEqual(expected_message, result.stdout.strip())

    def test_complete_update_passes(self):
        self.check_events(event(), "PASS remote_preferences=REMOTE_CONFIG_UPDATED")

    def test_chinese_descriptions_preserve_machine_field_matching(self):
        chinese_event = event().rstrip("\n") + " 配置数据发生变化：标识替换开关(enabled)：关闭 → 开启\n"
        self.check_events(chinese_event, "PASS remote_preferences=REMOTE_CONFIG_UPDATED")

    def test_future_text_field_description_cannot_override_machine_values(self):
        chinese_event = event().rstrip("\n") + " 配置数据发生变化：提示文本(message)：旧文本 → enabled=false gaid=other group=other\n"
        self.check_events(chinese_event, "PASS remote_preferences=REMOTE_CONFIG_UPDATED")

    def test_chinese_description_cannot_fill_missing_machine_fields(self):
        incomplete = event().replace(f"android_id={ANDROID_ID} ", "")
        incomplete = incomplete.rstrip("\n") + f" 配置数据发生变化：提示文本(message)：android_id={ANDROID_ID}\n"
        self.check_events(incomplete, "FAIL remote_preferences=UNEXPECTED_SNAPSHOT")

    def test_loaded_snapshot_cannot_prove_update(self):
        self.check_events(event(marker="REMOTE_CONFIG_LOADED"), "FAIL remote_preferences=SNAPSHOT_NOT_OBSERVED")

    def test_other_group_is_not_accepted(self):
        self.check_events(event(group="settings.com.example.target.other"), "FAIL remote_preferences=SNAPSHOT_NOT_OBSERVED")

    def test_other_package_is_not_accepted(self):
        self.check_events(event(package="com.example.target.other"), "FAIL remote_preferences=SNAPSHOT_NOT_OBSERVED")

    def test_worker_event_cannot_replace_main_process_update(self):
        self.check_events(event(process="com.example.target:worker"), "FAIL remote_preferences=SNAPSHOT_NOT_OBSERVED")

    def test_latest_snapshot_must_match(self):
        self.check_events(event() + event(enabled="false"), "FAIL remote_preferences=UNEXPECTED_SNAPSHOT")

    def test_all_fields_must_match(self):
        self.check_events(event(gaid="00000000-0000-0000-0000-000000000000"), "FAIL remote_preferences=UNEXPECTED_SNAPSHOT")

    def test_unrelated_later_event_does_not_replace_target_snapshot(self):
        self.check_events(event() + event(group="settings.com.example.other", enabled="false"),
                          "PASS remote_preferences=REMOTE_CONFIG_UPDATED")

    def test_read_failure_or_hook_installation_is_not_an_update(self):
        self.check_events("REMOTE_CONFIG_UNAVAILABLE stage=READ\nGAID_HOOK_INSTALLED\n",
                          "FAIL remote_preferences=SNAPSHOT_NOT_OBSERVED")


if __name__ == "__main__":
    unittest.main()
