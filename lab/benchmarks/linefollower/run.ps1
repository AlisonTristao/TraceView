# Builds (Release) and runs the TraceLab line-follower benchmark.
#
#   .\lab\benchmarks\linefollower\run.ps1                       # qLPV MPC, seed 1112, real time
#   .\lab\benchmarks\linefollower\run.ps1 --controller gpc
#   .\lab\benchmarks\linefollower\run.ps1 --seed 42 --track-type circle --fast --log run.csv
#
# Every argument is passed to tracelab-linefollower.exe. Keys while running:
# P / O = perturbation +/- 5 %, R = reset perturbation, Esc = quit.

param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $AppArgs)

$ErrorActionPreference = "Stop"
$repo = Resolve-Path "$PSScriptRoot\..\..\.."
$qt = if ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } else { "C:/Qt/6.9.2/mingw_64" }
$env:Path = "$qt\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;$env:Path"
$build = "$repo\build-lab-release"

if (-not (Test-Path "$build\CMakeCache.txt")) {
    cmake -S $repo -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe `
        -DCMAKE_PREFIX_PATH=$qt -DTRACEVIEW_BUILD_TESTS=OFF -DTRACEVIEW_BUILD_TOOLS=OFF
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
cmake --build $build --target tracelab-linefollower
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& "$build\lab\tracelab-linefollower\tracelab-linefollower.exe" @AppArgs
