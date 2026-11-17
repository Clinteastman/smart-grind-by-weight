"""Keep the firmware variants, their Kconfig options and the build tools in step."""
from pathlib import Path
import importlib.util
import os
import re
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


grinder = load_module("grinder", ROOT / "tools" / "grinder.py")
build_info = load_module("build_info", ROOT / "tools" / "build-scripts" / "build_info.py")
KCONFIG = (ROOT / "src" / "Kconfig.projbuild").read_text()
DECLARED = set(re.findall(r"^\s*config (SMART_GRIND_\w+)", KCONFIG, re.M))


class FirmwareVariantTest(unittest.TestCase):
    def test_every_overlay_exists_and_sets_declared_options(self):
        for variant, settings in grinder.FIRMWARE_VARIANTS.items():
            if not settings["overlay"]:
                continue
            with self.subTest(variant):
                overlay = (ROOT / settings["overlay"]).read_text()
                used = set(re.findall(r"^CONFIG_(SMART_GRIND_\w+)=", overlay, re.M))
                self.assertTrue(used <= DECLARED, used - DECLARED)

    def test_v2_overlay_selects_the_v2_board(self):
        overlay = (ROOT / grinder.FIRMWARE_VARIANTS["v2"]["overlay"]).read_text().splitlines()
        self.assertIn("CONFIG_SMART_GRIND_BOARD_V2=y", overlay)

    def test_headers_read_only_declared_options(self):
        for header in ("hardware.h", "debug.h"):
            with self.subTest(header):
                text = (ROOT / "src" / "config" / header).read_text()
                used = set(re.findall(r"CONFIG_(SMART_GRIND_\w+)", text))
                self.assertTrue(used, f"{header} reads no Smart Grind option")
                self.assertTrue(used <= DECLARED, used - DECLARED)

    def test_hardware_revision_follows_the_board_option(self):
        text = (ROOT / "src" / "config" / "hardware.h").read_text()
        self.assertRegex(text, r"#ifdef CONFIG_SMART_GRIND_BOARD_V2\s*\n#define HW_DISPLAY_VARIANT_V2 1")

    def test_only_v1_and_v2_images_are_archived(self):
        archives = {variant: settings["archive"] for variant, settings in grinder.FIRMWARE_VARIANTS.items()}
        self.assertEqual(archives["v1"], "waveshare-164-v1")
        self.assertEqual(archives["v2"], "waveshare-164-v2")
        self.assertIsNone(archives["debug"])
        self.assertIsNone(archives["mock"])


class IdfCommandTest(unittest.TestCase):
    def test_variant_builds_in_its_own_directory_with_layered_defaults(self):
        tool = grinder.GrinderTool()
        environment = {"IDF_PATH": "/opt/esp-idf", "IDF_PYTHON_ENV_PATH": "/opt/python-env"}
        with mock.patch.dict(os.environ, environment), mock.patch.object(grinder.platform, "system",
                                                                         return_value="Linux"):
            command = tool._idf_command("v2", "build")
        build_dir = ROOT / "build" / "v2"
        self.assertEqual(command, [
            "/opt/python-env/bin/python", "/opt/esp-idf/tools/idf.py",
            "-C", str(ROOT), "-B", str(build_dir),
            "-D", f"SDKCONFIG={build_dir / 'sdkconfig'}",
            "-D", "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.v2",
            "build",
        ])

    def test_no_command_without_an_esp_idf_environment(self):
        tool = grinder.GrinderTool()
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertIsNone(tool._idf_command("v1", "build"))


class ToolchainVersionTest(unittest.TestCase):
    def test_ci_release_manifest_and_hint_name_one_esp_idf_version(self):
        manifest = (ROOT / "src" / "idf_component.yml").read_text()
        minimum = re.search(r'idf:\s*\n\s*version: ">=([\d.]+)"', manifest).group(1)
        for workflow in ("firmware.yml", "release.yml"):
            with self.subTest(workflow):
                text = (ROOT / ".github" / "workflows" / workflow).read_text()
                self.assertEqual(re.findall(r"esp_idf_version: (\S+)", text), [f"v{minimum}"])
        self.assertIn(f"activate_idf_v{minimum}.sh", grinder.IDF_ACTIVATION_HINT)


class BuildInfoTest(unittest.TestCase):
    def run_build_info(self, folder, environment):
        header = Path(folder) / "git_info.h"
        counter = Path(folder) / ".build_number"
        with mock.patch.object(build_info, "HEADER_PATH", header), \
                mock.patch.object(build_info, "BUILD_NUMBER_PATH", counter), \
                mock.patch.object(build_info, "git_info", return_value=("abc1234", "main")), \
                mock.patch.dict(os.environ, environment, clear=True):
            self.assertEqual(build_info.main(), 0)
        return header, counter

    def test_ci_build_number_wins_and_unchanged_header_is_kept(self):
        with tempfile.TemporaryDirectory() as folder:
            header, _ = self.run_build_info(folder, {"SMART_GRIND_BUILD_NUMBER": "42"})
            text = header.read_text()
            self.assertIn("#define BUILD_NUMBER 42", text)
            self.assertIn('#define GIT_COMMIT_ID "abc1234"', text)
            stamp = header.stat().st_mtime_ns
            self.run_build_info(folder, {"SMART_GRIND_BUILD_NUMBER": "42"})
            self.assertEqual(header.stat().st_mtime_ns, stamp, "an unchanged header was rewritten")

    def test_local_counter_is_reused_unless_asked_to_advance(self):
        with tempfile.TemporaryDirectory() as folder:
            header, counter = self.run_build_info(folder, {})
            self.assertIn("#define BUILD_NUMBER 1", header.read_text())
            self.run_build_info(folder, {"SMART_GRIND_INCREMENT_BUILD": "1"})
            self.assertEqual(counter.read_text(), "2")
            self.assertIn("#define BUILD_NUMBER 2", header.read_text())


if __name__ == "__main__":
    unittest.main()
