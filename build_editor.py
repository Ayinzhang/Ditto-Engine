import subprocess
import sys
import os

os.chdir(r"C:\Projects\Ditto-Engine")

# Run MSBuild
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
result = subprocess.run(cmd, capture_output=True, text=True, errors='ignore')

# Try to print stdout, ignore encoding errors
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
