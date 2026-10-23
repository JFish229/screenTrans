param(
    [string]$Executable = (Join-Path $PSScriptRoot '../../x64/Release/ScreenTrans_Client.exe'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../.cache/traces'),
    [string]$Model = ''
)
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$env:FLEXMEET_ASR_TRACE_DIR = $OutputDirectory
if ($Model) { $env:FLEXMEET_ASR_MODEL = (Resolve-Path -LiteralPath $Model).Path }
$plugin = Join-Path (Split-Path $Executable -Parent) 'plugin'
$files = @($Executable, (Join-Path $plugin 'plugin_ASR.dll'), (Join-Path $plugin 'ggml-silero-v6.2.0.bin'))
$files += if ($env:FLEXMEET_ASR_MODEL) { $env:FLEXMEET_ASR_MODEL } else { Join-Path $plugin 'ggml-base.bin' }
$manifest = @{
    created = (Get-Date).ToString('o')
    files = @($files | Where-Object { Test-Path -LiteralPath $_ } | ForEach-Object { @{ path=$_; sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant() } })
    settings = @(Get-ChildItem Env:FLEXMEET_ASR_* | Select-Object Name,Value)
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory "manifest-$([DateTime]::Now.Ticks).json") -Encoding utf8
# Run synchronously so the console retains error and performance output.
& $Executable 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory "console-$([DateTime]::Now.Ticks).log")
