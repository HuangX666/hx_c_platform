"""Host regressions for the Android test deployment, without a device."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace


spec = importlib.util.spec_from_file_location(
    "android_runner", Path(__file__).parents[1] / "tools/verify_android.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class AndroidDeployment(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        self.build = self.root / "build"
        self.build.mkdir()
        self.remote = "/data/local/tmp/hx-test-regression"

    def tearDown(self):
        self.temporary.cleanup()

    def artifact(self, relative, data=b"\x7fELF-fixture"):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def test_sdk_under_source_and_variant_sonames(self):
        executable = self.artifact("build/bin/test_tls")
        core = self.artifact("build/libhx_c_platform.so")
        adapter = self.artifact("build/libhx_archive_7zip.so")
        fixture = self.artifact("examples/input.txt", b"fixture")
        manifest = []
        for name in ("crypto", "ssl"):
            actual = self.artifact(f"sdk/lib/lib{name}-hx.so")
            alias = actual.with_name(f"lib{name}.so")
            alias.symlink_to(actual.name)
            manifest.append(str(alias))
        (self.build / "hx_openssl_runtime_Release.txt").write_text(
            "\n".join(manifest), encoding="utf-8")
        tests = [{"name": "tls", "command": [str(executable), str(fixture)],
                  "properties": {"WORKING_DIRECTORY": str(self.build)}}]

        with patch.object(runner, "ROOT", self.root):
            files, folders, commands = runner.deployment(
                tests, self.build, self.remote)

        for name in ("libssl-hx.so", "libcrypto-hx.so", core.name,
                     adapter.name):
            self.assertIn(self.remote + "/bin/" + name, files)
        self.assertNotIn(self.remote + "/bin/libcrypto.so", files)
        self.assertNotIn(self.remote + "/bin/libssl.so", files)
        self.assertEqual(commands[0][2], self.remote)
        self.assertIn(self.remote, folders)
        self.assertEqual(commands[0][1][1],
                         self.remote + "/source/examples/input.txt")

    def test_missing_selected_runtime_is_a_deployment_error(self):
        (self.build / "hx_openssl_runtime_Release.txt").write_text(
            str(self.root / "missing/libssl.so"), encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "Missing selected runtime"):
            runner.runtime_files(self.build)

    def test_features_follow_generated_macros(self):
        config = self.artifact("build/private/hx_os_config.h", b"""
#define HX_OS_ENABLE_CRYPTO 1
#define HX_OS_HAVE_OPENSSL 1
#define HX_OS_HAVE_TLS 1
#define HX_OS_HAVE_ARCHIVE 0
#define HX_OS_HAVE_CJSON 1
""")
        features = runner.configured_features(self.build)
        self.assertTrue(features["CRYPTO"])
        self.assertTrue(features["TLS"])
        self.assertTrue(features["CJSON"])
        self.assertFalse(features["ARCHIVE"])
        config.write_text("#define HX_OS_HAVE_OPENSSL 1\n", encoding="utf-8")
        self.assertFalse(runner.configured_features(self.build)["CRYPTO"])

    def test_ctest_native_discovery_and_missing_binary(self):
        executable = self.artifact("build/bin/test_stream")
        host = self.artifact("host.py", b"print('host only')")
        data = {"tests": [
            {"name": "stream", "command": [str(executable)],
             "properties": [{"name": "TIMEOUT", "value": 30}]},
            {"name": "host", "command": [str(host)]},
        ]}
        args = SimpleNamespace(ctest="ctest", suite=None)
        with patch.object(runner.subprocess, "check_output",
                          return_value=json.dumps(data)):
            selected, omitted = runner.native_tests(args, self.build)
            self.assertEqual([t["name"] for t in selected], ["stream"])
            self.assertEqual(selected[0]["properties"]["TIMEOUT"], 30)
            self.assertIn("host", omitted)
            executable.unlink()
            with self.assertRaisesRegex(RuntimeError,
                                        "Missing configured test"):
                runner.native_tests(args, self.build)


if __name__ == "__main__":
    unittest.main()
