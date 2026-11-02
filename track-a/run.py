import os
import subprocess
import sys

def main():
    data_dir = "/data"
    out_dir = "/out"

    binary = "/app/a_ingest"

    result = subprocess.run(
        [binary, "--data", data_dir, "--out", out_dir]
    )

    return result.returncode


if __name__ == "__main__":
    sys.exit(main())