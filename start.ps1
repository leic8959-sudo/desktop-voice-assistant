param([string]$ConfigPath = (Join-Path $PSScriptRoot 'config.local.json'))
$ErrorActionPreference = 'Stop'
$assistantPython = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $assistantPython)) { $assistantPython = 'python' }
if (-not (Test-Path -LiteralPath $ConfigPath)) { throw '请先运行 python tools/init_config.py --lan-ip 电脑局域网IP 并填写 config.local.json。' }
Push-Location -LiteralPath $PSScriptRoot
try {
    & $assistantPython -c 'import aiohttp, av'
    if ($LASTEXITCODE -ne 0) { throw '缺少依赖，请由用户按 README 安装。脚本不会自行安装。' }
    & $assistantPython 'gateway.py' '--config' $ConfigPath
    if ($LASTEXITCODE -ne 0) { throw '桥接程序退出异常，请查看 runtime/gateway.log。' }
} finally { Pop-Location }
