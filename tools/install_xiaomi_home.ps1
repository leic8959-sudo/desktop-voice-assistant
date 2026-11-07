param([switch]$SkipRestart)
$ErrorActionPreference = 'Stop'
$assistantRoot = Split-Path -Parent $PSScriptRoot
$assistantConfig = Join-Path $assistantRoot 'runtime\home-assistant'
$assistantDestination = Join-Path $assistantConfig 'custom_components\xiaomi_home'
if (Test-Path -LiteralPath $assistantDestination) { throw 'xiaomi_home 已存在，停止安装以免覆盖。' }
New-Item -ItemType Directory -Path $assistantConfig -Force | Out-Null
$assistantRevision = '75b3acb6325a3a8d295688b13904737d6738a6a5'
$assistantDownload = Join-Path $assistantRoot ('runtime\xiaomi-home-install-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $assistantDownload | Out-Null
$assistantArchive = Join-Path $assistantDownload 'source.zip'
Invoke-WebRequest -Uri "https://api.github.com/repos/XiaoMi/ha_xiaomi_home/zipball/$assistantRevision" -Headers @{'User-Agent'='desktop-assistant-home-test'} -OutFile $assistantArchive -TimeoutSec 60 -UseBasicParsing
Expand-Archive -LiteralPath $assistantArchive -DestinationPath (Join-Path $assistantDownload 'source')
$assistantSource = @(Get-ChildItem -LiteralPath (Join-Path $assistantDownload 'source') -Directory)
if ($assistantSource.Count -ne 1) { throw '下载源码目录异常，未安装。' }
$assistantComponent = Join-Path $assistantSource[0].FullName 'custom_components\xiaomi_home'
if (-not (Test-Path -LiteralPath (Join-Path $assistantComponent 'manifest.json'))) { throw '缺少集成清单，未安装。' }
New-Item -ItemType Directory -Path (Split-Path -Parent $assistantDestination) -Force | Out-Null
Copy-Item -LiteralPath $assistantComponent -Destination $assistantDestination -Recurse
Copy-Item -LiteralPath (Join-Path $assistantSource[0].FullName 'LICENSE.md') -Destination (Join-Path $assistantDestination 'LICENSE.md')
Write-Host "已安装 Xiaomi Home v0.5.0，固定源码 $assistantRevision。"
Write-Host '源码和下载包保留于 runtime；当前安装不代表已获得商业使用授权。'
if (-not $SkipRestart) {
    & docker compose -f (Join-Path $assistantRoot 'home_test\compose.yaml') restart home_assistant
    if ($LASTEXITCODE -ne 0) { throw '集成已复制，但 Home Assistant 重启失败。请检查 Docker 状态。' }
}
