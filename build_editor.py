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
result = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='ignore')
print(result.stdout)
if result.stderr:
    print(result.stderr, file=sys.stderr)

print(f"\nBuild {'succeeded' if result.returncode == 0 else 'failed'} with exit code {result.returncode}")
sys.exit(result.returncode)
