param(
    [string]$Destination = (Join-Path (Split-Path $PSScriptRoot -Parent) 'models'),
    [ValidateSet('huggingface.co','hf-mirror.com')][string]$HostName = 'huggingface.co',
    [string[]]$Candidates = @()
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$dependencies = Get-Content -LiteralPath (Join-Path (Split-Path $PSScriptRoot -Parent) 'dependencies.json') -Raw | ConvertFrom-Json
$models = @($dependencies.models)
foreach ($name in $Candidates) {
    $candidate = @($dependencies.optional_models | Where-Object { $_.file -eq $name })
    if ($candidate.Count -ne 1) { throw "Unknown candidate: $name" }
    $models += $candidate
}
$manifest = foreach ($model in $models) {
    $target = Join-Path $Destination $model.file
    $url = "https://$HostName/$($model.repository)/resolve/$($model.revision)/$($model.file)"
    if (!(Test-Path -LiteralPath $target)) {
        & curl.exe --fail --location --retry 2 --output "$target.part" $url
        if ($LASTEXITCODE) { throw "Download failed: $($model.file)" }
        if ((Get-FileHash -LiteralPath "$target.part" -Algorithm SHA256).Hash.ToLowerInvariant() -ne $model.sha256) { throw "SHA256 mismatch: $($model.file)" }
        Move-Item -LiteralPath "$target.part" -Destination $target
    }
    if ((Get-Item -LiteralPath $target).Length -ne $model.bytes -or (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $model.sha256) { throw "Size/SHA256 mismatch: $($model.file)" }
    @{ name=$model.file; url=$url; bytes=$model.bytes; sha256=$model.sha256 }
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Destination 'manifest.json') -Encoding utf8
Write-Output "Verified models: $Destination"
