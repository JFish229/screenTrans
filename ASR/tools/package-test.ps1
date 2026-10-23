param([string]$Destination = (Join-Path $PSScriptRoot '../.cache/packages'))
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$release = Join-Path $repo 'x64/Release'
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$stage = Join-Path $Destination ('FlexMeet-v2-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path (Join-Path $stage 'plugin') | Out-Null
foreach ($name in @('ScreenTrans_Client.exe','ScreenTrans_Server.exe','ffmpeg_x264.dll','libiconv-2.dll','zlib1.dll')) {
    Copy-Item -LiteralPath (Join-Path $release $name) -Destination $stage
}
foreach ($name in @('plugin_ASR.dll','ggml-base.bin','ggml-silero-v6.2.0.bin')) {
    Copy-Item -LiteralPath (Join-Path $release "plugin/$name") -Destination (Join-Path $stage 'plugin')
}
Copy-Item -LiteralPath (Join-Path $repo 'ASR/verification/TESTING-v2.md') -Destination (Join-Path $stage 'TESTING.md')
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE.txt') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repo 'licenses') -Destination $stage -Recurse
$whisperLicense=Join-Path $repo 'ASR/.cache/source/whisper.cpp-6e4ab854f67f743900934a703d5603419384c961/LICENSE'
if (Test-Path -LiteralPath $whisperLicense) { Copy-Item -LiteralPath $whisperLicense -Destination (Join-Path $stage 'licenses/whisper-LICENSE') }
$files = Get-ChildItem -LiteralPath $stage -File -Recurse
$manifest = foreach ($file in $files) { @{ path=[IO.Path]::GetRelativePath($stage,$file.FullName); bytes=$file.Length; sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() } }
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'manifest.json') -Encoding utf8
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath "$stage.zip" -CompressionLevel Fastest
Write-Output "Package: $stage.zip"
