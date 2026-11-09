"""Run all hardware-independent ESP32 tests (Python 3 + g++)."""

import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[2]
    compiler = os.environ.get("CXX") or shutil.which("g++")
    if not compiler and Path("C:/Strawberry/c/bin/g++.exe").is_file():
        compiler = "C:/Strawberry/c/bin/g++.exe"
    if not compiler:
        print("Install g++ or set CXX to its executable path.", file=sys.stderr)
        return 1
    build = root / "build" / "tests" / "environment"
    build.mkdir(parents=True, exist_ok=True)
    source = root / "firmware" / "esp32" / "pet_care_station"
    json_include = Path(os.environ.get("ARDUINOJSON_INCLUDE", str(
        Path.home() / "Documents/Arduino/libraries/ArduinoJson/src")))
    if not (json_include / "ArduinoJson.h").is_file():
        print("Install ArduinoJson 7.4.2; set ARDUINOJSON_INCLUDE to its src directory.", file=sys.stderr)
        return 1
    suites = [("environment_state_test", [source / "environment_state.cpp"]),
              ("cloud_policy_test", []),
              ("live_telemetry_test", [source / "environment_state.cpp", source / "live_telemetry.cpp"]),
              ("bridge_control_test", [source / "bridge_control.cpp"]),
              ("rpc_protocol_test", [source / "rpc_protocol.cpp"]),
              ("rpc_session_test", []),
              ("bridge_integration_test", [source / "bridge_control.cpp"]),
              ("uart_driver_test", [])]
    for name, extra in suites:
        executable = build / (name + (".exe" if os.name == "nt" else ""))
        command = [compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                   "-I", str(source), str(root / "tests/esp32" / (name + ".cpp"))]
        if name == "rpc_protocol_test":
            command += ["-I", str(json_include)]
        if name in ("uart_driver_test", "bridge_integration_test"):
            command += ["-I", str(root / "tests/esp32/fakes"),
                        "-I", str(root / "firmware/esp32/libraries/PetStationUart/src")]
        command += [str(path) for path in extra] + ["-o", str(executable)]
        result = subprocess.run(command, cwd=root, check=False)
        if result.returncode:
            return result.returncode
        result = subprocess.run([str(executable)], cwd=root, check=False)
        if result.returncode:
            return result.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
