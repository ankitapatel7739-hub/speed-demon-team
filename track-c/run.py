import importlib
import os
import subprocess
import sys

MODULES = {
    "a": "a_ingest",
    "b": "b_index",
    "d": "d_traverse"
}


def run():
    track = os.environ.get("TRACK", "").strip().lower()

    if track == "c":
        result = subprocess.run([
            "/app/c_join",
            "--data", "/data",
            "--out", "/out"
        ])
        return result.returncode

    if track not in MODULES:
        sys.stderr.write(f"Invalid TRACK: {track}\n")
        return 2

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    importlib.import_module(MODULES[track]).main("/data", "/out")
    return 0


if __name__ == "__main__":
    sys.exit(run())