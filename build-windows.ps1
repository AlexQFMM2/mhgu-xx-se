param(
    [string]$QtDir = 'C:\Qt\5.15.2\mingw81_64'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$QMake = Join-Path $QtDir 'bin\qmake.exe'
$Make = Join-Path $QtDir 'bin\mingw32-make.exe'
$Deploy = Join-Path $QtDir 'bin\windeployqt.exe'

foreach ($Tool in @($QMake, $Make, $Deploy)) {
    if (-not (Test-Path -LiteralPath $Tool)) {
        throw "Qt tool not found: $Tool"
    }
}

$BuildDir = Join-Path $ProjectRoot 'build-windows'
$DistDir = Join-Path $ProjectRoot 'dist'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
if (Test-Path -LiteralPath $DistDir) {
    Remove-Item -LiteralPath $DistDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

Push-Location $BuildDir
try {
    & $QMake (Join-Path $ProjectRoot 'MHGUSaveEditor.pro')
    if ($LASTEXITCODE -ne 0) { throw 'qmake failed' }
    & $Make -j2
    if ($LASTEXITCODE -ne 0) { throw 'mingw32-make failed' }
} finally {
    Pop-Location
}

$Executable = Join-Path $ProjectRoot 'bin\MHGUSaveEditor.exe'
if (-not (Test-Path -LiteralPath $Executable)) {
    throw "Executable not found after build: $Executable"
}
Copy-Item -LiteralPath $Executable -Destination $DistDir -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'data') -Destination $DistDir -Recurse -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'LICENSE') -Destination $DistDir -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'README.md') -Destination $DistDir -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'ATTRIBUTION.md') -Destination $DistDir -Force
& $Deploy --release --no-translations (Join-Path $DistDir 'MHGUSaveEditor.exe')
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed' }

Write-Host "Portable build created at $DistDir"
