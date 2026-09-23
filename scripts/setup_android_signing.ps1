# One-time setup of the Android release signing key (see docs/ANDROID_BUILD.md,
# "Release signing"). Generates a random password and the keystore (or reuses
# the ones a previous run left), keeps both outside the repository, and walks
# through pasting the two GitHub secrets (plus the optional certificate
# variable) one at a time via the clipboard, so the password never has to be
# typed or shown.
#
#   powershell -ExecutionPolicy Bypass -File scripts\setup_android_signing.ps1

$ErrorActionPreference = 'Stop'

$backupDir = Join-Path $HOME 'TraceView-android-signing'
$keystore = Join-Path $backupDir 'traceview-release.jks'
$passwordFile = Join-Path $backupDir 'keystore-password.txt'

$keytool = Get-Command keytool -ErrorAction SilentlyContinue
if (-not $keytool) {
    if ($env:JAVA_HOME -and (Test-Path "$env:JAVA_HOME\bin\keytool.exe")) {
        $keytool = "$env:JAVA_HOME\bin\keytool.exe"
    } else {
        throw 'keytool not found. Install a JDK (e.g. Eclipse Adoptium) and try again.'
    }
}

# Re-running never replaces an existing key (that would break updates for
# everyone who installed a signed APK): it reuses it and just repeats the
# GitHub steps below.
$reuse = Test-Path $keystore
if ($reuse) {
    if (-not (Test-Path $passwordFile)) {
        throw "Found $keystore but not $passwordFile -- can't reuse the key without its password."
    }
    $password = (Get-Content -Path $passwordFile -Raw).Trim()
} else {
    # 32 random letters/digits. PKCS12 uses the same password for store and key.
    $alphabet = [char[]]'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789'
    $bytes = New-Object byte[] 32
    [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
    $password = -join ($bytes | ForEach-Object { $alphabet[$_ % $alphabet.Length] })

    New-Item -ItemType Directory -Force $backupDir | Out-Null
    Set-Content -Path $passwordFile -Value $password -Encoding ascii -NoNewline
}

# :env keeps the password off the command line.
$env:TRACEVIEW_KEYSTORE_PASSWORD = $password
$certFile = Join-Path $backupDir 'traceview-release-cert.der'
try {
    # keytool reports progress on stderr; don't let 'Stop' turn that into an
    # error. Failures are caught through $LASTEXITCODE instead.
    $ErrorActionPreference = 'Continue'
    if (-not $reuse) {
        & $keytool -genkeypair -keystore $keystore -alias traceview -keyalg RSA -keysize 4096 `
            -validity 10000 -dname 'CN=TraceView' `
            -storepass:env TRACEVIEW_KEYSTORE_PASSWORD -keypass:env TRACEVIEW_KEYSTORE_PASSWORD
        if ($LASTEXITCODE -ne 0) { throw 'keytool failed to create the keystore.' }
    }

    # Also proves the saved password opens the keystore. The hash below is
    # the same digest apksigner prints as "certificate SHA-256 digest";
    # `keytool -list -v` would print it too, but JDK 25 crashes formatting
    # that output under a pt_BR locale (MissingFormatArgumentException).
    & $keytool -exportcert -keystore $keystore -alias traceview -file $certFile `
        -storepass:env TRACEVIEW_KEYSTORE_PASSWORD
    if ($LASTEXITCODE -ne 0) { throw "keytool couldn't open $keystore with the saved password." }
} finally {
    $ErrorActionPreference = 'Stop'
    Remove-Item Env:\TRACEVIEW_KEYSTORE_PASSWORD -ErrorAction SilentlyContinue
}
$sha256 = (Get-FileHash $certFile -Algorithm SHA256).Hash.ToLower()
$base64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($keystore))

Write-Host ''
if ($reuse) { Write-Host "Reusing the existing key in: $backupDir" -ForegroundColor Green }
else { Write-Host "Keystore and password saved in: $backupDir" -ForegroundColor Green }
Write-Host 'Keep a copy of that folder somewhere safe (password manager, USB drive...).'
Write-Host 'Without it, future APKs can no longer update installed ones.' -ForegroundColor Yellow
Write-Host ''
Write-Host 'Open: https://github.com/AlisonTristao/TraceView/settings/secrets/actions'
Write-Host 'For each item below, click "New repository secret", type the Name, paste the value (Ctrl+V).'
Write-Host ''

Set-Clipboard -Value $base64
Write-Host '1) Name: ANDROID_KEYSTORE_BASE64   (value copied to the clipboard)' -ForegroundColor Cyan
Read-Host  '   Press Enter after saving it'

Set-Clipboard -Value $password
Write-Host '2) Name: ANDROID_KEYSTORE_PASSWORD (value copied to the clipboard)' -ForegroundColor Cyan
Read-Host  '   Press Enter after saving it'

Set-Clipboard -Value $sha256
Write-Host '3) Optional: switch to the "Variables" tab, "New repository variable"' -ForegroundColor Cyan
Write-Host '   Name: ANDROID_SIGNING_CERT_SHA256 (value copied to the clipboard)' -ForegroundColor Cyan
Read-Host  '   Press Enter when done'

Set-Clipboard -Value ' '
Write-Host 'Done. The next release tag will publish a signed APK.' -ForegroundColor Green
