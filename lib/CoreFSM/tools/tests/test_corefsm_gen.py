#!/usr/bin/env python3
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest


TOOLS_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = Path(__file__).resolve().parents[4]
FIXTURES = Path(__file__).resolve().parent / "fixtures"
sys.path.insert(0, str(TOOLS_DIR))

import corefsm_gen as gen  # noqa: E402
import wokwi2corefsm as legacy  # noqa: E402


def write(path, content):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


class GeneratorTests(unittest.TestCase):
    def test_csv_emits_native_safe_and_mcp_tables(self):
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp)
            write(project / "corefsm.json", json.dumps({
                "source": {"format": "csv", "path": "hardware.csv"},
                "nodes": [{"id": "main", "board": "nano"}],
                "backends": [{
                    "node": "main", "id": "EXP1", "driver": "MCP23017",
                    "bus": "Wire", "address": "0x20",
                }],
            }))
            write(project / "hardware.csv", """\
node,name,role,target,pullup,active_low,debounce_ms,filter,safe
main,Marcha,DI,gpio.2,true,,20,,
main,Final_Remoto,DI,EXP1.3,true,,5,,
main,Piloto,DO,gpio.13,,false,,,false
main,Valvula_Segura,DO,gpio.12,,true,,,true
main,Rele_Remoto,DO,EXP1.7,,true,,,true
main,Consigna,AI,A0,,,,3,
""")
            output = project / "include" / "HardwareConfig.h"
            self.assertEqual(0, gen.generate(project_dir=str(project), quiet=True))
            text = output.read_text(encoding="utf-8")

            self.assertIn("#include <io/Mcp23017Backend.h>", text)
            self.assertIn("CFSM_TABLE_BACKEND", text)
            self.assertIn("ROW(Mcp23017Backend, EXP1, Wire, 0x20)", text)
            self.assertIn("CFSM_TABLE_DI_BACKEND", text)
            self.assertRegex(text, r"ROW\(EXP1,\s+3,\s+Final_Remoto")
            self.assertIn("CFSM_TABLE_DO_SAFE", text)
            self.assertRegex(text, r"ROW\(\s+12,\s+Valvula_Segura.*true\s*,\s*true")
            self.assertIn("CFSM_TABLE_DO_BACKEND", text)
            self.assertRegex(text, r"ROW\(EXP1,\s+7,\s+Rele_Remoto.*true\s*,\s*true")
            self.assertRegex(text, r"ROW\(\s+A0,\s+Consigna.*3\s*\)")

    def test_explicit_json_supports_nested_nodes_and_requires_selection(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "hardware.json"
            write(path, json.dumps({
                "nodes": [
                    {"id": "frontal", "board": "nano", "signals": [
                        {"name": "Led_Frontal", "role": "DO",
                         "target": "gpio.13", "safe": False},
                    ]},
                    {"id": "trasero", "board": "esp32", "signals": [
                        {"name": "Sensor_Trasero", "role": "DI",
                         "target": "gpio.4", "pullup": True,
                         "debounce_ms": 5},
                    ]},
                ],
                "backends": [],
            }))
            with self.assertRaisesRegex(gen.ConfigError, "varios nodos"):
                gen.parse_json_source(str(path), {}, None)

            model = gen.parse_json_source(str(path), {}, "trasero")
            self.assertEqual("trasero", model.node)
            self.assertEqual("esp32", model.board)
            self.assertEqual(["Sensor_Trasero"], [s.name for s in model.signals])

    def test_csv_multiple_nodes_requires_node_and_rejects_untagged_rows(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "hardware.csv"
            write(path, """\
node,name,role,target,pullup,active_low,debounce_ms,filter,safe
a,Entrada_A,DI,gpio.2,true,,20,,
b,Entrada_B,DI,gpio.3,true,,20,,
""")
            with self.assertRaisesRegex(gen.ConfigError, "varios nodos"):
                gen.parse_csv_source(str(path), {}, None)
            model = gen.parse_csv_source(str(path), {}, "b")
            self.assertEqual(["Entrada_B"], [s.name for s in model.signals])

    def test_auto_source_priority_and_explicit_corefsm_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp)
            write(project / "hardware.csv", """\
node,name,role,target,pullup,active_low,debounce_ms,filter,safe
main,Desde_Csv,DI,gpio.2,true,,20,,
""")
            write(project / "hardware.json", json.dumps({
                "board": "nano", "signals": [
                    {"name": "Desde_Json", "role": "DO", "target": "gpio.13"}
                ]
            }))
            write(project / "diagram.json", json.dumps({
                "parts": [{"type": "wokwi-arduino-nano", "id": "nano"}],
                "connections": [],
            }))

            path, source_format, _node = gen.resolve_project_source(
                str(project), {}, None, None)
            self.assertEqual(project / "hardware.csv", Path(path))
            self.assertEqual("csv", source_format)

            config = {"source": {"format": "json", "path": "hardware.json"}}
            path, source_format, _node = gen.resolve_project_source(
                str(project), config, None, None)
            self.assertEqual(project / "hardware.json", Path(path))
            self.assertEqual("json", source_format)

    def test_strict_validation_rejects_bad_values_and_conflicts(self):
        cases = {
            "booleano": "main,S1,DI,gpio.2,quizas,,20,,\n",
            "role": "main,S1,XX,gpio.2,,,,,\n",
            "backend no declarado": "main,S1,DI,EXP1.3,true,,20,,\n",
            "MCP23017 solo tiene canales": "main,S1,DI,EXP1.16,true,,20,,\n",
        }
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp)
            base_config = {
                "nodes": [{"id": "main", "board": "nano"}],
                "backends": [{"node": "main", "id": "EXP1",
                              "driver": "MCP23017", "bus": "Wire",
                              "address": "0x20"}],
            }
            header = "node,name,role,target,pullup,active_low,debounce_ms,filter,safe\n"
            for expected, row in cases.items():
                with self.subTest(expected=expected):
                    config = {} if expected == "backend no declarado" else base_config
                    write(project / "hardware.csv", header + row)
                    with self.assertRaisesRegex(gen.ConfigError, expected):
                        gen.parse_csv_source(
                            str(project / "hardware.csv"), config, "main")

            write(project / "hardware.csv", header +
                  "main,S1,DI,gpio.2,true,,20,,\n" +
                  "main,S2,DO,gpio.2,,false,,,false\n")
            with self.assertRaisesRegex(gen.ConfigError, "usado tambien"):
                gen.parse_csv_source(
                    str(project / "hardware.csv"), base_config, "main")

    def test_wokwi_with_multiple_boards_requires_selection(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "diagram.json"
            write(path, json.dumps({
                "parts": [
                    {"type": "wokwi-arduino-nano", "id": "nano"},
                    {"type": "wokwi-arduino-uno", "id": "uno"},
                ],
                "connections": [],
            }))
            with self.assertRaisesRegex(gen.ConfigError, "varias placas"):
                gen.parse_wokwi_source(str(path), {}, None)
            self.assertEqual(
                "uno", gen.parse_wokwi_source(str(path), {}, "uno").node)

    def test_legacy_wrapper_matches_golden_output(self):
        fixture = FIXTURES / "wokwi_single"
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "HardwareConfig.h"
            self.assertEqual(0, legacy.run(
                str(fixture / "diagram.json"), str(output), quiet=True))
            self.assertEqual(
                (fixture / "HardwareConfig.golden.h").read_text(encoding="utf-8"),
                output.read_text(encoding="utf-8"))

    def test_unchanged_output_is_not_rewritten(self):
        fixture = FIXTURES / "wokwi_single"
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "HardwareConfig.h"
            legacy.run(str(fixture / "diagram.json"), str(output), quiet=True)
            first = output.stat().st_mtime_ns
            time.sleep(0.01)
            legacy.run(str(fixture / "diagram.json"), str(output), quiet=True)
            self.assertEqual(first, output.stat().st_mtime_ns)


class NewProjectTests(unittest.TestCase):
    def _create(self, base, name, source=None, extra=None):
        command = [
            sys.executable, str(TOOLS_DIR / "nuevo_proyecto.py"), name,
            "--dir", str(base),
        ]
        if source:
            command += ["--fuente", source]
        command += list(extra or [])
        return subprocess.run(
            command, cwd=str(REPO_ROOT), text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def test_every_source_creates_a_compilable_hardware_header(self):
        expected_source = {
            "csv": "hardware.csv", "json": "hardware.json",
            "wokwi": "diagram.json", "manual": None,
        }
        with tempfile.TemporaryDirectory() as tmp:
            for source in ("csv", "json", "wokwi", "manual"):
                with self.subTest(source=source):
                    result = self._create(Path(tmp), "project_" + source, source)
                    self.assertEqual(0, result.returncode, result.stderr + result.stdout)
                    project = Path(tmp) / ("project_" + source)
                    self.assertTrue((project / "include" / "HardwareConfig.h").is_file())
                    main = (project / "src" / "main.cpp").read_text(encoding="utf-8")
                    self.assertIn("HW.setSafetyInterlock(manager.isEmergencyStop())", main)
                    ini = (project / "platformio.ini").read_text(encoding="utf-8")
                    if source == "manual":
                        self.assertNotIn("extra_scripts", ini)
                    else:
                        self.assertIn("corefsm_gen.py", ini)
                        self.assertTrue((project / expected_source[source]).is_file())

    def test_default_and_deprecated_alias_both_create_csv_projects(self):
        with tempfile.TemporaryDirectory() as tmp:
            default_result = self._create(Path(tmp), "default_source")
            self.assertEqual(0, default_result.returncode,
                             default_result.stderr + default_result.stdout)
            self.assertTrue((Path(tmp) / "default_source" / "hardware.csv").is_file())

            alias_result = self._create(
                Path(tmp), "old_alias", extra=["--sin-wokwi"])
            self.assertEqual(0, alias_result.returncode,
                             alias_result.stderr + alias_result.stdout)
            self.assertIn("obsoleto", alias_result.stderr)
            self.assertTrue((Path(tmp) / "old_alias" / "hardware.csv").is_file())

    def test_custom_directory_uses_paths_relative_to_the_generated_project(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp) / "una" / "carpeta" / "profunda"
            result = self._create(base, "custom_paths", "csv")
            self.assertEqual(0, result.returncode, result.stderr + result.stdout)

            project = base / "custom_paths"
            ini = (project / "platformio.ini").read_text(encoding="utf-8")
            values = {}
            for line in ini.splitlines():
                if "=" in line and not line.lstrip().startswith(";"):
                    key, value = line.split("=", 1)
                    values[key.strip()] = value.strip()

            lib_path = Path(values["lib_extra_dirs"])
            if not lib_path.is_absolute():
                lib_path = project / lib_path
            self.assertEqual((REPO_ROOT / "lib").resolve(), lib_path.resolve())

            hook_path = Path(values["extra_scripts"].removeprefix("pre:"))
            if not hook_path.is_absolute():
                hook_path = project / hook_path
            self.assertEqual(
                (TOOLS_DIR / "corefsm_gen.py").resolve(), hook_path.resolve())

            readme = (project / "README.md").read_text(encoding="utf-8")
            guide = os.path.relpath(
                REPO_ROOT / "lib" / "CoreFSM" / "README.md", project)
            self.assertIn(guide.replace("\\", "/"), readme)

    def test_esp32_wokwi_uses_component_pin_names_and_binary_firmware(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self._create(
                Path(tmp), "esp32_wokwi", "wokwi",
                extra=["--placa", "esp32"])
            self.assertEqual(0, result.returncode, result.stderr + result.stdout)

            project = Path(tmp) / "esp32_wokwi"
            diagram = json.loads(
                (project / "diagram.json").read_text(encoding="utf-8"))
            endpoints = [endpoint for wire in diagram["connections"]
                         for endpoint in wire[:2]]
            self.assertIn("esp:4", endpoints)
            self.assertIn("esp:2", endpoints)
            self.assertNotIn("esp:D4", endpoints)
            self.assertNotIn("esp:D2", endpoints)

            wokwi = (project / "wokwi.toml").read_text(encoding="utf-8")
            self.assertIn("firmware.bin", wokwi)
            self.assertNotIn("firmware.hex", wokwi)


if __name__ == "__main__":
    unittest.main()
