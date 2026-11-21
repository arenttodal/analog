"""Windows CI diagnosis: which TEST_CASE in a test file does MSVC fail to compile?

Usage: python compile_each_case.py <MSBuild.exe> <project.vcxproj> <test file>

Rewrites the file (temporarily) as its shared part - everything before the first TEST_CASE -
plus one TEST_CASE at a time, compiles it alone with MSBuild (SelectedFiles) under a time
limit, prints how each case did, and restores the file. A case that stalls is named, so the
construct MSVC chokes on can be fixed instead of the whole file being guessed at.
"""
import subprocess
import sys
import time

LIMIT_SECONDS = 240


def compile_alone(msbuild, project, source):
    command = [msbuild, project, "/nologo", "/nr:false", "/v:minimal", "/t:ClCompile",
               "/p:Configuration=Release", "/p:Platform=x64", "/p:SelectedFiles=" + source]
    started = time.time()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    try:
        output, _ = process.communicate(timeout=LIMIT_SECONDS)
        return ("compiled" if process.returncode == 0 else "error"), time.time() - started, output
    except subprocess.TimeoutExpired:
        subprocess.run(["taskkill", "/T", "/F", "/PID", str(process.pid)], capture_output=True)
        subprocess.run(["taskkill", "/F", "/IM", "cl.exe"], capture_output=True)
        return "STALLED", time.time() - started, ""


def main():
    msbuild, project, source = sys.argv[1:4]
    with open(source, encoding="utf-8") as f:
        original = f.read()
    lines = original.splitlines(keepends=True)
    starts = [i for i, line in enumerate(lines) if line.startswith("TEST_CASE (")]
    if not starts:
        print("no TEST_CASE in", source)
        return 0
    shared = "".join(lines[:starts[0]])
    stalled = []
    try:
        for k, start in enumerate(starts):
            end = starts[k + 1] if k + 1 < len(starts) else len(lines)
            case = "".join(lines[start:end])
            with open(source, "w", encoding="utf-8") as f:
                f.write(shared + case)
            result, seconds, output = compile_alone(msbuild, project, source)
            print(f"  line {start + 1}: {result} ({seconds:.0f} s)  {lines[start].strip()[:110]}", flush=True)
            if result == "error":
                print("\n".join(output.splitlines()[-8:]))
            if result == "STALLED":
                stalled.append(start + 1)
    finally:
        with open(source, "w", encoding="utf-8") as f:
            f.write(original)
    print("stalled at TEST_CASE lines:", stalled if stalled else "none (the shared part alone, or only the whole file)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
