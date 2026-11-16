param(
    [string]$Image = 'espressif/idf:v5.5.6',
    [string]$Volume = '',
    [string]$Board = 'waveshare-amoled-1.8',
    [switch]$WakePcmProbe
)
$ErrorActionPreference = 'Stop'
$assistantRoot = Split-Path -Parent $PSScriptRoot
$assistantProject = Join-Path $assistantRoot 'firmware\assistant_espidf'
. (Join-Path $PSScriptRoot 'board_profiles.ps1')
$assistantProfiles = Get-AssistantBoardProfiles -ProjectPath $assistantProject
if ($Board -cnotmatch '^[a-z0-9][a-z0-9.-]{0,63}$' -or -not $assistantProfiles.ContainsKey($Board)) { throw "板型 $Board 没有已登记的原生源码入口，请使用登记表中的小写ID。" }
$assistantProfile = $assistantProfiles[$Board]
$assistantLegacyFlag = if ($assistantProfile.legacy) { '1' } else { '0' }
$assistantTarget = $assistantProfile.target
if (-not $assistantProfile.legacy) {
    foreach ($assistantInput in @('idf_component.yml', 'sdkconfig.defaults')) {
        if (-not (Test-Path -LiteralPath (Join-Path $assistantProject "boards\$Board\$assistantInput"))) { throw "板型 $Board 缺少构建输入 $assistantInput。" }
    }
}
$assistantEvidence = Join-Path $assistantRoot 'evidence'
$assistantVolumeName = if ($Volume) { $Volume } elseif ($assistantProfile.legacy) { 'assistant-idf-sr247-v1' } else { "assistant-idf-$Board-v1" }
if ($assistantVolumeName -notmatch '^[a-zA-Z0-9][a-zA-Z0-9_.-]*$') { throw '构建 volume 名称无效。' }
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) { throw '需要正在运行的 Docker Desktop。' }
& docker image inspect $Image --format '{{.Id}}' | Out-Null
if ($LASTEXITCODE -ne 0) { throw "请先执行 docker pull $Image 安装官方构建环境。" }
$assistantSource = 'type=bind,source=' + $assistantProject.Replace('\','/') + ',target=/source,readonly'
$assistantOutput = 'type=bind,source=' + $assistantProject.Replace('\','/') + ',target=/output'
$assistantVolume = 'type=volume,source=' + $assistantVolumeName + ',target=/project'
$ErrorActionPreference = 'Continue'
& docker volume create $assistantVolumeName | Out-Null
if ($LASTEXITCODE -ne 0) { throw "构建 volume 创建失败：$assistantVolumeName" }
$assistantSync = @'
set -eu
if [ -f /project/.assistant-board ]; then
    [ "$(cat /project/.assistant-board)" = "$ASSISTANT_BUILD_BOARD" ] || { echo 'Build volume belongs to a different board; use a separate volume.'; exit 1; }
elif [ "$ASSISTANT_BUILD_LEGACY" != '1' ] && [ -f /project/sdkconfig ]; then
    echo 'Unidentified existing build volume; use a fresh board-specific volume.'; exit 1
fi
rm -rf /project/main
mkdir -p /project
cp -a /source/main /project/main
cp /source/CMakeLists.txt /source/partitions.csv /source/sdkconfig.defaults /source/native_board_profiles.json /source/board_profiles.cmake /project/
cp "/source/sdkconfig_target_$ASSISTANT_BUILD_TARGET.defaults" /project/
rm -f /project/sdkconfig_board.defaults
if [ "$ASSISTANT_BUILD_LEGACY" = '1' ]; then
    cp /source/dependencies.lock /project/dependencies.lock
else
    cp "/source/boards/$ASSISTANT_BUILD_BOARD/idf_component.yml" /project/main/idf_component.yml
    if [ -f "/source/boards/$ASSISTANT_BUILD_BOARD/partitions.csv" ]; then
        cp "/source/boards/$ASSISTANT_BUILD_BOARD/partitions.csv" /project/partitions.csv
    fi
    cp "/source/boards/$ASSISTANT_BUILD_BOARD/sdkconfig.defaults" /project/sdkconfig_board.defaults
    if [ -f "/source/boards/$ASSISTANT_BUILD_BOARD/dependencies.lock" ]; then
        cp "/source/boards/$ASSISTANT_BUILD_BOARD/dependencies.lock" /project/dependencies.lock
    fi
fi
printf '%s\n' "$ASSISTANT_BUILD_BOARD" > /project/.assistant-board
'@
& docker run --rm --mount $assistantSource --mount $assistantVolume --env "ASSISTANT_BUILD_BOARD=$Board" --env "ASSISTANT_BUILD_LEGACY=$assistantLegacyFlag" --env "ASSISTANT_BUILD_TARGET=$assistantTarget" ubuntu:24.04 sh -c $assistantSync
if ($LASTEXITCODE -ne 0) { throw '构建源文件同步失败。' }
$assistantPatch = Join-Path $assistantRoot 'tools\patch_waveshare_qspi.py'
$assistantProbeOption = if ($WakePcmProbe) { 'ON' } else { 'OFF' }
& docker run --rm --mount $assistantVolume --workdir /project $Image bash -c "idf.py -DIDF_TARGET=$assistantTarget -DASSISTANT_BOARD=$Board -DASSISTANT_WAKE_PCM_PROBE=$assistantProbeOption reconfigure"
if ($LASTEXITCODE -ne 0) { throw '固定依赖安装或 CMake 配置失败，未继续构建。' }
if ($assistantProfile.qspi_panel) {
    $assistantPanel = $assistantProfile.qspi_panel
    & docker run --rm --mount $assistantVolume --mount ('type=bind,source=' + $assistantPatch.Replace('\','/') + ',target=/qspi_patch.py,readonly') --entrypoint python $Image /qspi_patch.py /project --panel $assistantPanel --component $assistantProfile.bsp_component
    if ($LASTEXITCODE -ne 0) { throw 'QSPI显示驱动补丁检查失败，未继续构建。' }
}
New-Item -ItemType Directory -Path $assistantEvidence -Force | Out-Null
& docker run --rm --mount $assistantVolume --workdir /project $Image bash -c "idf.py -DIDF_TARGET=$assistantTarget -DASSISTANT_BOARD=$Board -DASSISTANT_WAKE_PCM_PROBE=$assistantProbeOption build" 2>&1 | Tee-Object -FilePath (Join-Path $assistantEvidence "native-idf-build-$assistantVolumeName.log")
if ($LASTEXITCODE -ne 0) { throw '固件编译失败，未执行烧录。' }
$assistantExport = @'
import json,os,shutil
from pathlib import Path
board=os.environ['ASSISTANT_BUILD_BOARD']
profile=json.loads(Path('/project/native_board_profiles.json').read_text())['profiles'][board]
build=Path('/project/build')
description=json.loads((build/'project_description.json').read_text())
if description['project_name'] != profile['project_name']:raise ValueError('Build project does not match the selected board')
out=Path('/output/build')
if not profile['legacy']:out=out/board
out.mkdir(parents=True,exist_ok=True)
manifest=json.loads((build/'flasher_args.json').read_text())
for relative in manifest['flash_files'].values():
    source=(build/relative).resolve()
    if not source.is_relative_to(build):raise ValueError('Invalid flash artifact path')
    target=out/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
for name in ['flasher_args.json','flash_args','project_description.json']:
    if (build/name).exists():shutil.copy2(build/name,out/name)
for name in ['sdkconfig','dependencies.lock']:
    shutil.copy2(Path('/project')/name,out/name)
for name in ['sdkconfig.defaults',f"sdkconfig_target_{profile['target']}.defaults",'sdkconfig_board.defaults']:
    if (Path('/project')/name).exists():shutil.copy2(Path('/project')/name,out/name)
if 'ASSISTANT_BOARD_TAIJI=1' in profile['compile_definitions']:
    for name in ['xiaozhi_taiji','st77916']:
        shutil.copytree('/project/main/third_party/'+name,out/'third_party'/name,dirs_exist_ok=True)
if any(flag in profile['compile_definitions'] for flag in ['ASSISTANT_BOARD_VOCAT=1','ASSISTANT_BOARD_MOJI2_C5=1']):
    for name in ['xiaozhi_vocat','st77916']:
        shutil.copytree('/project/main/third_party/'+name,out/'third_party'/name,dirs_exist_ok=True)
if 'ASSISTANT_BOARD_TCIRCLE=1' in profile['compile_definitions']:
    shutil.copytree('/project/main/third_party/tft_espi',out/'third_party/tft_espi',dirs_exist_ok=True)
if 'ASSISTANT_BOARD_LCD_185_FAMILY=1' in profile['compile_definitions']:
    for name in ['xiaozhi_st77916','st77916']:
        shutil.copytree('/project/main/third_party/'+name,out/'third_party'/name,dirs_exist_ok=True)
if 'ASSISTANT_BOARD_XINGZHI_085=1' in profile['compile_definitions']:
    shutil.copytree('/project/main/third_party/xiaozhi_nv3023',out/'third_party/xiaozhi_nv3023',dirs_exist_ok=True)
if any(flag in profile['compile_definitions'] for flag in ['ASSISTANT_BOARD_XINGZHI_085=1','ASSISTANT_BOARD_MAGICLICK_24=1']):
    shutil.copytree('/project/main/third_party/nv3023',out/'third_party/nv3023',dirs_exist_ok=True)
if any(flag in profile['compile_definitions'] for flag in ['ASSISTANT_BOARD_MVSRLORA=1','ASSISTANT_BOARD_CHD_LCD35=1','ASSISTANT_BOARD_WS_CAM_35=1']):
    shutil.copytree('/project/main/third_party/st7796',out/'third_party/st7796',dirs_exist_ok=True)
if any(flag in profile['compile_definitions'] for flag in ['ASSISTANT_BOARD_WS_CAM=1','ASSISTANT_BOARD_WS_RGB_C=1']):
    shutil.copytree('/project/main/third_party/ch32v003',out/'third_party/ch32v003',dirs_exist_ok=True)
if 'ASSISTANT_BOARD_LCDWIKI=1' in profile['compile_definitions']:
    for name in ['xiaozhi_lcdwiki','st77922']:
        shutil.copytree('/project/main/third_party/'+name,out/'third_party'/name,dirs_exist_ok=True)
if 'ASSISTANT_BOARD_LCD_EV_2=1' in profile['compile_definitions']:
    shutil.copytree('/project/main/third_party/gt1151',out/'third_party/gt1151',dirs_exist_ok=True)
if any(flag in profile['compile_definitions'] for flag in ['ASSISTANT_BOARD_SENSECAP=1','ASSISTANT_BOARD_LCD_146=1']):
    shutil.copytree('/project/main/third_party/spd2010',out/'third_party/spd2010',dirs_exist_ok=True)
if 'ASSISTANT_BOARD_WS35=1' in profile['compile_definitions']:
    shutil.copytree('/project/main/third_party/xiaozhi_ws35',out/'third_party/xiaozhi_ws35',dirs_exist_ok=True)
if any(flag in profile['compile_definitions'] for flag in ['ASSISTANT_BOARD_WS35B=1','ASSISTANT_BOARD_WS349=1']):
    shutil.copytree('/project/main/third_party/axs15231b',out/'third_party/axs15231b',dirs_exist_ok=True)
if any(flag in profile['compile_definitions'] for flag in ['ASSISTANT_BOARD_AMOLED_216=1','ASSISTANT_BOARD_AMOLED_175=1','ASSISTANT_BOARD_AMOLED_175C=1']):
    shutil.copytree('/project/main/third_party/waveshare_amoled216',out/'third_party/waveshare_amoled216',dirs_exist_ok=True)
if any(flag in profile['compile_definitions'] for flag in ['ASSISTANT_BOARD_BOX3=1','ASSISTANT_BOARD_BOX=1','ASSISTANT_BOARD_BOXLITE=1']):
    shutil.copytree('/project/main/third_party/espressif_box3',out/'third_party/espressif_box3',dirs_exist_ok=True)
    shutil.copytree('/project/main/third_party/espressif_ili9341',out/'third_party/espressif_ili9341',dirs_exist_ok=True)
    shutil.copytree('/project/main/third_party/gt911',out/'third_party/gt911',dirs_exist_ok=True)
if 'ASSISTANT_BOARD_CORES3=1' in profile['compile_definitions']:
    shutil.copytree('/project/main/third_party/espressif_cores3',out/'third_party/espressif_cores3',dirs_exist_ok=True)
    shutil.copytree('/project/main/third_party/espressif_ili9341',out/'third_party/espressif_ili9341',dirs_exist_ok=True)
if any(flag in profile['compile_definitions'] for flag in ['ASSISTANT_BOARD_SH8601=1','ASSISTANT_BOARD_AMOLED_206=1','ASSISTANT_BOARD_AMOLED_132=1']):
    shutil.copytree('/project/main/third_party/waveshare_sh8601',out/'third_party/waveshare_sh8601',dirs_exist_ok=True)
if 'ASSISTANT_BOARD_AMOLED_132=1' in profile['compile_definitions']:
    shutil.copytree('/project/main/third_party/kodediy_cst820',out/'third_party/kodediy_cst820',dirs_exist_ok=True)
(out/'board-target.json').write_text(json.dumps({'board':board,'project_name':profile['project_name'],'target':profile['target'],'flash_size_mb':profile['flash_size_mb']},indent=2)+'\n')
print(f'Firmware artifacts exported for {board}; use only this directory and its flasher_args.json.')
'@
& docker run --rm --mount $assistantVolume --mount $assistantOutput --env "ASSISTANT_BUILD_BOARD=$Board" $Image python -c $assistantExport
if ($LASTEXITCODE -ne 0) { throw '固件产物导出失败。' }
Write-Output "构建完成，板型 $Board；产物位于 firmware/assistant_espidf/build 下对应板型目录（1.8 沿用 build 根目录）；未执行烧录。"
