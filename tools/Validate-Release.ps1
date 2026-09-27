param(
  [Parameter(Mandatory=$true)][string]$ReleaseOut,
  [Parameter(Mandatory=$true)][string]$VersionFile
)
$ErrorActionPreference = "Stop"
$v = Get-Content $VersionFile -Raw | ConvertFrom-Json
$launcherVersion = [string]$v.launcherVersion
$modVersion = [string]$v.modVersion
$launcher = @(Get-ChildItem $ReleaseOut -Filter "FactionOverhaulLauncher-*.zip" -File)
$runtime = @(Get-ChildItem $ReleaseOut -Filter "FactionOverhaulRuntime-*.zip" -File)
if ($launcher.Count -ne 1) { throw "Expected exactly one launcher ZIP, found $($launcher.Count)" }
if ($runtime.Count -ne 1) { throw "Expected exactly one runtime ZIP, found $($runtime.Count)" }
if ($launcher[0].Name -ne "FactionOverhaulLauncher-$launcherVersion.zip") { throw "Launcher ZIP/version mismatch: $($launcher[0].Name) vs $launcherVersion" }
if ($runtime[0].Name -ne "FactionOverhaulRuntime-$modVersion.zip") { throw "Runtime ZIP/version mismatch: $($runtime[0].Name) vs $modVersion" }
$manifestPath = Join-Path $ReleaseOut "update.json"
if (!(Test-Path $manifestPath)) { throw "Missing generated update.json" }
$manifestText = Get-Content $manifestPath -Raw
$launcherHash = (Get-FileHash $launcher[0].FullName -Algorithm SHA256).Hash.ToLowerInvariant()
$runtimeHash = (Get-FileHash $runtime[0].FullName -Algorithm SHA256).Hash.ToLowerInvariant()
foreach ($required in @($launcherVersion,$modVersion,$launcher[0].Name,$runtime[0].Name,$launcherHash,$runtimeHash)) {
  if ($manifestText.ToLowerInvariant() -notlike "*$($required.ToLowerInvariant())*") { throw "update.json missing expected value: $required" }
}
[pscustomobject]@{
  launcherVersion=$launcherVersion; modVersion=$modVersion;
  launcherFile=$launcher[0].Name; launcherSha256=$launcherHash;
  runtimeFile=$runtime[0].Name; runtimeSha256=$runtimeHash
} | ConvertTo-Json | Set-Content (Join-Path $ReleaseOut "validation.json") -Encoding UTF8
Write-Host "Release validation PASS"
