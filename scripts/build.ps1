param([ValidateSet('Debug','Release')][string]$Configuration='Release',[switch]$Package)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Set-Location $root
$vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'Visual Studio C++ x64 tools are required.'}
$dev=Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1'
& $dev -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$nativeCmake=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
if(Test-Path (Join-Path $nativeCmake 'cmcldeps.exe')){$env:PATH="$nativeCmake;$env:PATH"}
if(!$env:QT_ROOT_DIR){$env:QT_ROOT_DIR=Join-Path $root '.tools\Qt\6.8.3\msvc2022_64'}
if(!(Test-Path "$env:QT_ROOT_DIR\bin\qmake.exe")){throw 'Set QT_ROOT_DIR to the shared MSVC Qt 6 kit.'}
$env:PATH="$env:QT_ROOT_DIR\bin;$env:PATH"
$preset='windows-msvc-'+$Configuration.ToLower()
& cmake --preset $preset
if($LASTEXITCODE){throw 'Configure failed'}
& cmake --build --preset $preset --parallel 4
if($LASTEXITCODE){throw 'Build failed'}
& ctest --preset $preset
if($LASTEXITCODE){throw 'Tests failed'}
if($Package){
 if($Configuration -ne 'Release'){throw 'Package requires Release configuration'}
 & cpack --config build/Release/CPackConfig.cmake -G NSIS
 if($LASTEXITCODE){throw 'CPack failed'}
 $installer=Get-Item -LiteralPath "$root\release\PathWeave-Setup-x64.exe"
 $sha=(Get-FileHash -LiteralPath $installer.FullName -Algorithm SHA256).Hash
 Write-Output "PathWeave 1.1.0 | $($installer.FullName) | $($installer.Length) bytes | SHA256 $sha"
}
