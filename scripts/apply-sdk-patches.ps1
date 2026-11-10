# Apply/revert the exact SDK patch set locked in deps.lock.json (working tree only).
param([switch]$Check, [switch]$Revert,
    [string]$SourceDir, [string]$LockFile)
$ErrorActionPreference = 'Stop'
if ($Check -and $Revert) { throw 'Choose -Check or -Revert' }
$root = Split-Path $PSScriptRoot -Parent
if (-not $LockFile) { $LockFile = Join-Path $root 'deps.lock.json' }
$root = Split-Path (Resolve-Path -LiteralPath $LockFile).Path -Parent
$sdk = if ($SourceDir) { $SourceDir } else { Join-Path $root 'external/ps2recomp' }
$entry = (Get-Content -Raw -LiteralPath $LockFile | ConvertFrom-Json).ps2recomp
function GitSdk([string[]]$GitArgs) {
    & git -C $sdk @GitArgs
    if ($LASTEXITCODE -ne 0) { throw "git $GitArgs failed" }
}
function FileHash([string]$Path) {
    # Patch inputs are UTF-8 text; hash LF so Git's CRLF checkout is portable.
    $bytes = [Text.Encoding]::UTF8.GetBytes([IO.File]::ReadAllText($Path).Replace("`r`n", "`n"))
    $sha = [Security.Cryptography.SHA256]::Create()
    try { [BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
if ((GitSdk @('rev-parse', 'HEAD')) -ne $entry.commit) { throw 'PS2Recomp pin mismatch' }
$base = @{}
$final = @{}
$paths = @()
foreach ($patch in $entry.patches) {
    $path = Join-Path $root $patch.file
    if ((FileHash $path) -ne $patch.sha256) {
        throw "Patch hash mismatch: $($patch.file)"
    }
    $paths += $path
    foreach ($f in $patch.base_files.PSObject.Properties) {
        if (-not $base.ContainsKey($f.Name)) { $base[$f.Name] = $f.Value }
    }
    foreach ($f in $patch.files.PSObject.Properties) { $final[$f.Name] = $f.Value }
}
foreach ($changed in @(GitSdk @('diff', 'HEAD', '--name-only'))) {
    if (-not $final.ContainsKey($changed)) { throw "Unrecorded SDK change: $changed" }
}
function Matches($expected) {
    foreach ($name in $expected.Keys) {
        $path = Join-Path $sdk $name
        $actual = if (Test-Path -LiteralPath $path) {
            FileHash $path
        } else { '' }
        if ($actual -ne $expected[$name]) { return $false }
    }
    return $true
}
if ($Check) {
    if (-not (Matches $final)) { throw 'SDK patches missing or modified' }
    Write-Host 'SDK patches verified'
    exit 0
}
$wanted = if ($Revert) { $base } else { $final }
$before = if ($Revert) { $final } else { $base }
if (Matches $wanted) { Write-Host 'SDK already matches requested state'; exit 0 }
if (-not (Matches $before)) { throw 'Unexpected/partial SDK state; preserving files' }
if ($paths.Count) {
    $flags = @('apply')
    if ($Revert) { [array]::Reverse($paths); $flags += '--reverse' }
    GitSdk ($flags + @('--check') + $paths)
    GitSdk ($flags + $paths)
}
if (-not (Matches $wanted)) { throw 'SDK result hash mismatch' }
Write-Host 'SDK patch state updated and verified'
