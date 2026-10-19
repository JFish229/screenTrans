param(
    [ValidateSet('CPU','CUDA','Bundled')][string]$Backend = 'CPU',
    [string]$SourceDir = '',
    [string]$CMake = 'cmake',
    [string]$Generator = 'Visual Studio 17 2022',
    [string]$Toolset = '',
    [string]$DeployTo = ''
)
$ErrorActionPreference = 'Stop'
$asrRoot = Split-Path $PSScriptRoot -Parent
$buildRoot = Join-Path $asrRoot "build/$Backend"
if ($Backend -eq 'Bundled' -and !$PSBoundParameters.ContainsKey('Generator')) {
    $Generator = 'Visual Studio 18 2026'
}
if ($CMake -eq 'cmake' -and !(Get-Command cmake -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vsPath = & $vswhere -latest -products '*' -property installationPath
    $CMake = Join-Path $vsPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
}
$configure = @('-S', $asrRoot, '-B', $buildRoot, '-G', $Generator, '-A', 'x64',
    '-DBUILD_TESTING=ON', '-DGGML_NATIVE=OFF', "-DGGML_CUDA=$($Backend -eq 'CUDA')", "-DASR_PREBUILT_WHISPER=$($Backend -eq 'Bundled')")
if ($SourceDir) { $configure += "-DASR_WHISPER_SOURCE=$SourceDir" }
if ($Toolset) { $configure += @('-T', $Toolset) }
if ($Backend -eq 'CUDA' -and !(Get-Command nvcc -ErrorAction SilentlyContinue)) {
    throw 'CUDA Toolkit (nvcc) not found. The CUDA version in nvidia-smi is not an installed Toolkit. Install a Toolkit supported by your driver AND MSVC first, or build CPU/Bundled.'
}
& $CMake @configure
if ($LASTEXITCODE) { throw 'CMake configure failed' }
& $CMake --build $buildRoot --config Release --parallel 8
if ($LASTEXITCODE) { throw 'Build failed' }
& ([System.IO.Path]::Combine((Split-Path (Get-Command $CMake).Source), 'ctest.exe')) --test-dir $buildRoot -C Release --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
if ($DeployTo) {
    $pluginDir = Join-Path $DeployTo 'plugin'
    New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
    Copy-Item -LiteralPath (Join-Path $buildRoot 'Release/plugin/plugin_ASR.dll') -Destination $pluginDir
    Write-Output "Deployed ASR DLL to $pluginDir"
}
