param([string]$Image = 'espressif/idf:v5.5.2', [string]$Volume = 'assistant-idf-sr247-v1', [switch]$WakePcmProbe)
$ErrorActionPreference = 'Stop'
$assistantRoot = Split-Path -Parent $PSScriptRoot
$assistantProject = Join-Path $assistantRoot 'firmware\assistant_espidf'
$assistantEvidence = Join-Path $assistantRoot 'evidence'
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) { throw '需要正在运行的 Docker Desktop。' }
& docker image inspect $Image --format '{{.Id}}' | Out-Null
if ($LASTEXITCODE -ne 0) { throw "请先执行 docker pull $Image 安装官方构建环境。" }
$assistantSource = 'type=bind,source=' + $assistantProject.Replace('\','/') + ',target=/source,readonly'
$assistantOutput = 'type=bind,source=' + $assistantProject.Replace('\','/') + ',target=/output'
$assistantVolume = 'type=volume,source=' + $Volume + ',target=/project'
$ErrorActionPreference = 'Continue'
& docker volume create $Volume | Out-Null
if ($LASTEXITCODE -ne 0) { throw "构建 volume 创建失败：$Volume" }
& docker run --rm --mount $assistantSource --mount $assistantVolume ubuntu:24.04 sh -c 'rm -rf /project/main; mkdir -p /project; cp -a /source/main /project/main; cp /source/CMakeLists.txt /source/partitions.csv /source/sdkconfig.defaults /project/'
if ($LASTEXITCODE -ne 0) { throw '构建源文件同步失败。' }
$assistantPatch = Join-Path $assistantRoot 'tools\patch_waveshare_qspi.py'
$assistantProbeOption = if ($WakePcmProbe) { 'ON' } else { 'OFF' }
& docker run --rm --mount $assistantVolume --workdir /project $Image bash -c "idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_WAKE_PCM_PROBE=$assistantProbeOption reconfigure"
if ($LASTEXITCODE -ne 0) { throw '固定依赖安装或 CMake 配置失败，未继续构建。' }
& docker run --rm --mount $assistantVolume --mount ('type=bind,source=' + $assistantPatch.Replace('\','/') + ',target=/qspi_patch.py,readonly') --entrypoint python $Image /qspi_patch.py /project
if ($LASTEXITCODE -ne 0) { throw 'QSPI显示驱动补丁检查失败，未继续构建。' }
& docker run --rm --mount $assistantVolume --workdir /project $Image bash -c "idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_WAKE_PCM_PROBE=$assistantProbeOption build" 2>&1 | Tee-Object -FilePath (Join-Path $assistantEvidence "native-idf-build-$Volume.log")
if ($LASTEXITCODE -ne 0) { throw '固件编译失败，未执行烧录。' }
$assistantExport = @'
import json,shutil
from pathlib import Path
build=Path('/project/build');out=Path('/output/build');out.mkdir(exist_ok=True)
manifest=json.loads((build/'flasher_args.json').read_text())
for relative in manifest['flash_files'].values():
    source=(build/relative).resolve()
    if not source.is_relative_to(build):raise ValueError('Invalid flash artifact path')
    target=out/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
for name in ['flasher_args.json','flash_args','project_description.json']:
    if (build/name).exists():shutil.copy2(build/name,out/name)
for name in ['sdkconfig','dependencies.lock']:
    shutil.copy2(Path('/project')/name,Path('/output')/name)
print('Firmware artifacts exported; use flasher_args.json for flashing.')
'@
& docker run --rm --mount $assistantVolume --mount $assistantOutput $Image python -c $assistantExport
if ($LASTEXITCODE -ne 0) { throw '固件产物导出失败。' }
Write-Output '构建完成，产物已导出到 firmware/assistant_espidf/build；未执行烧录。'
