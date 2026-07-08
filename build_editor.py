import os
import subprocess
import sys
from pathlib import Path

os.chdir(Path(__file__).resolve().parent)


def normalize_process_path_environment():
    path_keys = [key for key in os.environ.keys() if key.lower() == "path"]
    if len(path_keys) <= 1:
        return

    preferred = "Path" if "Path" in os.environ else path_keys[0]
    path_value = os.environ.get(preferred) or os.environ.get(path_keys[0], "")
    for key in path_keys:
        if key != preferred:
            os.environ.pop(key, None)
    os.environ[preferred] = path_value


def msbuild_environment():
    env = {}
    path_value = None
    for key, value in os.environ.items():
        if key.lower() == "path":
            if path_value is None or key == "Path":
                path_value = value
            continue
        env[key] = value
    if path_value is not None:
        env["Path"] = path_value
    return env


normalize_process_path_environment()


cmd = [
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
    "Ditto.sln",
    "/p:Configuration=Debug",
    "/p:Platform=x64",
    "/t:Ditto",
    "/m:1",
    "/v:minimal",
    "/nologo"
]

print("Building Ditto project...")
result = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", env=msbuild_environment())


try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.stderr.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

if result.stdout:
    try:
        print(result.stdout)
    except UnicodeEncodeError:
        print(result.stdout.encode('ascii', 'replace').decode('ascii'))
if result.stderr:
    try:
        print(result.stderr, file=sys.stderr)
    except UnicodeEncodeError:
        print(result.stderr.encode('ascii', 'replace').decode('ascii'), file=sys.stderr)

print(f"\nBuild {'succeeded' if result.returncode == 0 else 'failed'} with exit code {result.returncode}")
sys.exit(result.returncode)
