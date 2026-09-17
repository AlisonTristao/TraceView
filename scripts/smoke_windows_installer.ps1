param(
    [Parameter(Mandatory = $true)]
    [string]$Installer
)

$ErrorActionPreference = "Stop"

$tempRoot = $env:RUNNER_TEMP
if (-not $tempRoot) {
    $tempRoot = $env:TEMP
}
$installDir = Join-Path $tempRoot "traceview-smoke"
if (Test-Path $installDir) {
    Remove-Item $installDir -Recurse -Force
}

$installerPath = (Resolve-Path $Installer).Path
$install = Start-Process -FilePath $installerPath `
    -ArgumentList @("/S", "/D=$installDir") `
    -Wait -PassThru
if ($install.ExitCode -ne 0) {
    throw "NSIS installer exited with code $($install.ExitCode)"
}

$executable = Join-Path $installDir "TraceView.exe"
if (-not (Test-Path $executable)) {
    throw "Installed executable not found at $executable"
}

$application = Start-Process -FilePath $executable -WorkingDirectory $installDir -PassThru
try {
    Wait-Process -Id $application.Id -Timeout 15 -ErrorAction SilentlyContinue | Out-Null
    $application.Refresh()
    if ($application.HasExited) {
        throw "TraceView.exe exited during startup with code $($application.ExitCode)"
    }

    Write-Host "Windows installer smoke passed (installation and startup)"
}
finally {
    $application.Refresh()
    if (-not $application.HasExited) {
        Stop-Process -Id $application.Id -Force
    }
}