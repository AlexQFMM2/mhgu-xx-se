param(
    [Parameter(Mandatory = $true)]
    [string]$DexDir,

    [Parameter(Mandatory = $true)]
    [string]$OutDir
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$DexDir = [System.IO.Path]::GetFullPath($DexDir)
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
$DexExe = Join-Path $DexDir "MHXX Dex.exe"
$Csc = Join-Path $env:WINDIR "Microsoft.NET\Framework\v4.0.30319\csc.exe"

if (-not (Test-Path -LiteralPath $DexExe -PathType Leaf)) {
    throw "MHXX Dex.exe was not found: $DexExe"
}
if (-not (Test-Path -LiteralPath $Csc -PathType Leaf)) {
    throw "32-bit .NET Framework compiler was not found: $Csc"
}

$BuildDir = Join-Path ([System.IO.Path]::GetTempPath()) ("mhxx-dex-dump-" + [Guid]::NewGuid().ToString("N"))
$RunnerExe = Join-Path $BuildDir "MHXXDexDump.exe"
New-Item -ItemType Directory -Path $BuildDir | Out-Null

try {
    & $Csc /nologo /platform:x86 /target:exe /r:System.dll /r:System.Core.dll /r:System.Data.dll /out:$RunnerExe (Join-Path $ScriptDir "Runner.cs")
    if ($LASTEXITCODE -ne 0) {
        throw "C# compilation failed with exit code $LASTEXITCODE"
    }

    & $RunnerExe $DexDir $OutDir
    if ($LASTEXITCODE -ne 0) {
        throw "Dex dump failed with exit code $LASTEXITCODE"
    }
}
finally {
    if (Test-Path -LiteralPath $BuildDir) {
        Remove-Item -LiteralPath $BuildDir -Recurse -Force
    }
}
