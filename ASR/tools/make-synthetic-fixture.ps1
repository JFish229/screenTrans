param([string]$Destination = (Join-Path (Split-Path $PSScriptRoot -Parent) '.cache/synthetic'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$speaker = New-Object System.Speech.Synthesis.SpeechSynthesizer
try {
    $voice = $speaker.GetInstalledVoices() | Where-Object { $_.VoiceInfo.Culture.Name -eq 'zh-CN' } | Select-Object -First 1
    if (!$voice) { throw 'No offline Chinese SAPI voice installed' }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    $speaker.SelectVoice($voice.VoiceInfo.Name)
    $format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
    $speaker.SetOutputToWaveFile((Join-Path $Destination 'zh.wav'), $format)
    $text = ([char[]](22823,23478,22909,65292,20170,22825,25105,20204,35752,35770,36731,20250,35758,39033,30446,30340,35821,38899,35782,21035,12290,25105,20204,38656,35201,38477,20302,23383,24149,24310,36831,65292,21516,26102,20943,23569,38169,23383,21644,28431,23383,12290)) -join ''
    $speaker.Speak($text)
    $text | Set-Content -LiteralPath (Join-Path $Destination 'reference.txt') -Encoding utf8
    Write-Output "Synthetic diagnostic fixture (not meeting accuracy evidence): $Destination"
} finally { $speaker.Dispose() }
