param(
    [ValidateSet('CPU','CUDA')][string]$Backend = 'CPU',
    [string]$SourceDir = '',
    [string]$Generator = 'Visual Studio 17 2022',
    [string]$Toolset = ''
)
$ErrorActionPreference = 'Stop'
$asrRoot = Split-Path $PSScriptRoot -Parent
$repoRoot = Split-Path $asrRoot -Parent
if ($Backend -eq 'CUDA' -and !(Get-Command nvcc -ErrorAction SilentlyContinue)) {
    throw 'CUDA Toolkit not found. Install a supported Toolkit/MSVC/driver combination before requesting CUDA.'
}
$dependencies = Get-Content -LiteralPath (Join-Path $asrRoot 'dependencies.json') -Raw | ConvertFrom-Json
foreach ($model in $dependencies.models) {
    $source = Join-Path $asrRoot "models/$($model.file)"
    if (!(Test-Path -LiteralPath $source)) { throw "Missing $($model.file). Run ASR/tools/download-models.ps1 first." }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $model.sha256) { throw "SHA256 mismatch: $($model.file)" }
}
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vsPath = & $vswhere -latest -products '*' -property installationPath
$msbuild = Join-Path $vsPath 'MSBuild/Current/Bin/MSBuild.exe'
# Existing vendor .lib files require the VS 2026 STL. Keep the user's project
# toolset selections intact; override them only for this complete build.
& $msbuild (Join-Path $repoRoot 'screenTrans.sln') /t:Build /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /m:4 /nologo /v:minimal
if ($LASTEXITCODE) { throw 'Solution build failed' }
$destination = Join-Path $repoRoot 'x64/Release'
& (Join-Path $PSScriptRoot 'build.ps1') -Backend $Backend -SourceDir $SourceDir -Generator $Generator -Toolset $Toolset -DeployTo $destination
foreach ($model in $dependencies.models) {
    $source = Join-Path $asrRoot "models/$($model.file)"
    Copy-Item -LiteralPath $source -Destination (Join-Path $destination 'plugin')
}
foreach ($name in @('ffmpeg_x264.dll','libiconv-2.dll','zlib1.dll')) {
    $candidates = @((Join-Path $repoRoot "screenTrans/thirdparty/lib/$name"), (Join-Path $repoRoot "x64/Debug/$name"))
    $source = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ($source) { Copy-Item -LiteralPath $source -Destination $destination }
    elseif (!(Test-Path -LiteralPath (Join-Path $destination $name))) { throw "Missing client runtime: $name" }
}
Write-Output "Ready: $(Join-Path $destination 'ScreenTrans_Client.exe')"
