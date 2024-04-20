#! /usr/bin/env python3

import os
import sys
import subprocess

from pathlib import Path

# Set them globally once, cause env vars are evil.
BUILD_TYPE = os.environ.get("BUILD_TYPE", "--default-library=both")
BUILD_DEBUG = os.environ.get("BUILD_GST_DEBUG", "-Dgstreamer:gst_debug=true")
PROJECT_ROOT = os.environ.get("CI_PROJECT_DIR", os.getcwd())
ARTIFACTS = os.environ.get("CI_ARTIFACTS_URL", "NOT_RUNNING_ON_CI/")


def determine_linux_ref_file() -> Path:
    ci_dir = Path(PROJECT_ROOT, "ci/data/fedora/")

    if BUILD_TYPE in ("--default-library=shared", "--default-library=both"):
        btype = "dynamic"
    else:
        btype = "static"

    path = Path(ci_dir, f"{btype}.txt")

    return path


def determine_ref_file() -> Path:
    if sys.platform == "win32":
        ci_dir = Path(PROJECT_ROOT, "ci/data/windows")
        path = Path(ci_dir, "plugins.txt")
    elif sys.platform == "linux":
        path = determine_linux_ref_file()
    else:
        print(f"Unsupported platform: {sys.platform}, exiting")
        sys.exit(123)

    return path


def save_inspect_output() -> None:
    suffix = ""
    if sys.platform == "win32":
        suffix = ".exe"

    plugins_path = determine_ref_file()

    with plugins_path.open(mode="w", encoding="utf-8") as inspect:
        res = subprocess.run(
            [f"gst-inspect-1.0{suffix}"], capture_output=True, check=True, text=True
        )

        # Remove the total line
        # We don't want to have to update the refs every time a feature is added
        output = res.stdout.splitlines()[:-1]

        if BUILD_DEBUG == "-Dgstreamer:gst_debug=true":
            # This is the only element that differs from the nodebug
            # build and thus it's not worth to duplicate the reference
            # files just for this
            output.remove("coretracers:  log (GstTracerFactory)")

        inspect.write("\n".join(output))

        res = subprocess.run(
            [f"gst-inspect-1.0{suffix}", "-b"],
            capture_output=True,
            check=True,
            text=True,
        )

        output = res.stdout.splitlines()[:-1]
        inspect.write("\n".join(output))


def diff_files() -> None:
    diffs_dir = Path(PROJECT_ROOT, "diffs/")
    os.makedirs(diffs_dir, exist_ok=True)

    diff_name = Path(diffs_dir, "job_plugins.diff")

    with diff_name.open(mode="w", encoding="utf-8") as diff:
        result = subprocess.run(["git", "diff", "--quiet", "-b"], check=False)

        if result != 0:
            subprocess.run(["git", "diff", "-u", "-b"], stdout=diff, check=False)
            print("\033[91mYou have a diff in the files. Please update with:\033[0m")
            print(f"     $ curl {ARTIFACTS}diffs/{diff_name.name} | git apply -")
            print(
                "(note that it might take a few minutes for artefacts to be available on the server)\n"
            )
            sys.exit(result.returncode)


def main():
    save_inspect_output()
    diff_files()


if __name__ == "__main__":
    main()
