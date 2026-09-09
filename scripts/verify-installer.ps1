param([string]$Installer)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Set-Location $root
if(!$Installer){$Installer=Join-Path $root 'release\PathWeave-Setup-x64.exe'}
if(!(Test-Path -LiteralPath $Installer)){throw "Missing installer: $Installer"}
$runId=Get-Date -Format 'yyyyMMdd-HHmmss'
$testRoot=Join-Path $root "artifacts\installation-$runId"
$installDir=Join-Path $testRoot 'app'
$dataDir=Join-Path $testRoot 'data'
$checks=Join-Path $root 'artifacts\installed-checks'
New-Item -ItemType Directory -Force $testRoot,$checks | Out-Null
$resolvedRoot=[IO.Path]::GetFullPath($testRoot)
if(!$resolvedRoot.StartsWith([IO.Path]::GetFullPath((Join-Path $root 'artifacts'))+[IO.Path]::DirectorySeparatorChar)){throw 'Test target is outside the workspace artifacts directory'}
# NSIS /D must be last and unquoted. The entire ArgumentList string preserves spaces.
$install=Start-Process -FilePath $Installer -ArgumentList "/S /D=$installDir" -PassThru -Wait -WindowStyle Hidden
if($install.ExitCode -ne 0){throw "Installer exit: $($install.ExitCode)"}
$exe=Join-Path $installDir 'PathWeave.exe'
if(!(Test-Path -LiteralPath $exe)){throw 'Installed executable missing'}
if(!(Test-Path -LiteralPath (Join-Path $installDir 'Qt6Core.dll'))){throw 'Shared Qt runtime missing'}
# Eliminate development Qt and MinGW DLL paths to verify deployed runtime closure.
$previousPath=$env:PATH
$env:PATH="$env:WINDIR\system32;$env:WINDIR"
$env:QT_QPA_PLATFORM='windows'
try {
 $smoke=Start-Process -FilePath $exe -ArgumentList "--smoke-test --data-dir `"$dataDir`" --artifact-dir `"$checks`"" -Wait -PassThru -WindowStyle Hidden
 if($smoke.ExitCode -ne 0){throw "Installed smoke failed: $($smoke.ExitCode). See $checks"}
 $restart=Start-Process -FilePath $exe -ArgumentList "--verify-data --data-dir `"$dataDir`" --artifact-dir `"$checks`"" -Wait -PassThru -WindowStyle Hidden
 if($restart.ExitCode -ne 0){throw 'Installed restart persistence verification failed'}
} finally {$env:PATH=$previousPath}
$uninstaller=Join-Path $installDir 'Uninstall.exe'
if(!(Test-Path -LiteralPath $uninstaller)){$uninstaller=(Get-ChildItem -LiteralPath $installDir -Filter '*uninstall*.exe' | Select-Object -First 1).FullName}
if(!$uninstaller){throw 'Uninstaller missing'}
# _?= disables NSIS self-copy so Wait observes actual uninstall completion.
$uninstall=Start-Process -FilePath $uninstaller -ArgumentList "/S _?=$installDir" -Wait -PassThru -WindowStyle Hidden
if($uninstall.ExitCode -ne 0){throw "Uninstall exit: $($uninstall.ExitCode)"}
if(Test-Path -LiteralPath $exe){throw 'Application executable still present after uninstall'}
if(!(Test-Path -LiteralPath (Join-Path $dataDir 'pathweave.sqlite3'))){throw 'Uninstaller removed career data unexpectedly'}
$file=Get-Item -LiteralPath $Installer
$hash=(Get-FileHash -LiteralPath $Installer -Algorithm SHA256).Hash.ToLowerInvariant()
$record=[ordered]@{version='1.1.0';installer=$file.FullName;size_bytes=$file.Length;sha256=$hash;install_exit=$install.ExitCode;installed_executable=$exe;installed_smoke_exit=$smoke.ExitCode;restart_exit=$restart.ExitCode;uninstall_exit=$uninstall.ExitCode;executable_removed=$true;user_data_preserved=$true;timestamp=(Get-Date).ToString('o');signed=$false}
$record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'artifacts\installer-verification.json') -Encoding utf8
"$hash  PathWeave-Setup-x64.exe" | Set-Content -LiteralPath (Join-Path $root 'release\SHA256SUMS.txt') -Encoding ascii
$record | ConvertTo-Json
