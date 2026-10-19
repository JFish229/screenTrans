param([string]$Destination = (Join-Path (Split-Path $PSScriptRoot -Parent) 'models'), [ValidateSet('huggingface.co','hf-mirror.com')][string]$HostName = 'huggingface.co')
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$models = @(
    @{ Name='ggml-base.bin'; Url="https://$HostName/ggerganov/whisper.cpp/resolve/5359861c739e955e79d9a303bcbc70fb988958b1/ggml-base.bin"; Hash='60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe' },
    @{ Name='ggml-silero-v6.2.0.bin'; Url="https://$HostName/ggml-org/whisper-vad/resolve/9ffd54a1e1ee413ddf265af9913beaf518d1639b/ggml-silero-v6.2.0.bin"; Hash='2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987' }
)
$manifest = foreach ($model in $models) {
    $target = Join-Path $Destination $model.Name
    if (!(Test-Path -LiteralPath $target)) {
        & curl.exe --fail --location --retry 2 --output "$target.part" $model.Url
        if ($LASTEXITCODE) { throw "Download failed: $($model.Name)" }
        if ((Get-FileHash -LiteralPath "$target.part" -Algorithm SHA256).Hash.ToLowerInvariant() -ne $model.Hash) { throw "SHA256 mismatch: $($model.Name)" }
        Move-Item -LiteralPath "$target.part" -Destination $target
    }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $model.Hash) { throw "SHA256 mismatch: $($model.Name)" }
    @{ name=$model.Name; url=$model.Url; bytes=(Get-Item -LiteralPath $target).Length; sha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Destination 'manifest.json') -Encoding utf8
Write-Output "Models and SHA256 manifest: $Destination"
