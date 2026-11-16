param(
    [Parameter(Mandatory = $true)][string]$SdkPath,
    [Parameter(Mandatory = $true)][string]$ToolsPath,
    [Parameter(Mandatory = $true)][string]$PythonEnvironment,
    [string]$Board = 'waveshare-amoled-1.8'
)
$ErrorActionPreference = 'Stop'
$assistantRoot = Split-Path -Parent $PSScriptRoot
$assistantProject = Join-Path $assistantRoot 'firmware\assistant_espidf'
. (Join-Path $PSScriptRoot 'board_profiles.ps1')
$assistantProfiles = Get-AssistantBoardProfiles -ProjectPath $assistantProject
if ($Board -cnotmatch '^[a-z0-9][a-z0-9.-]{0,63}$' -or -not $assistantProfiles.ContainsKey($Board)) { throw '板型未登记，未启动构建。' }
$assistantPython = Join-Path $PythonEnvironment 'Scripts\python.exe'
$assistantIdf = Join-Path $SdkPath 'tools\idf.py'
if (-not (Test-Path -LiteralPath $assistantPython) -or -not (Test-Path -LiteralPath $assistantIdf)) { throw '既有IDF Python或SDK不存在；脚本不会安装。' }
$assistantToolManifest = Get-Content -LiteralPath (Join-Path $SdkPath 'tools\tools.json') -Raw | ConvertFrom-Json
$assistantCompilerTool = if ($assistantProfiles[$Board].target -eq 'esp32c5') { 'riscv32-esp-elf' } else { 'xtensa-esp-elf' }
$assistantCompilerExecutable = if ($assistantProfiles[$Board].target -eq 'esp32c5') { 'riscv32-esp-elf-gcc.exe' } else { 'xtensa-esp32s3-elf-gcc.exe' }
$assistantRomExecutable = if ($assistantProfiles[$Board].target -eq 'esp32c5') { 'esp32c5_rev0_rom.elf' } else { 'esp32s3_rev0_rom.elf' }
$assistantToolVersions = @{}
foreach ($assistantToolName in @('cmake', 'ninja', $assistantCompilerTool, 'esp-rom-elfs')) {
    $assistantTool = @($assistantToolManifest.tools | Where-Object name -eq $assistantToolName)
    $assistantVersion = @($assistantTool.versions | Where-Object status -eq 'recommended' | Select-Object -ExpandProperty name)
    if ($assistantVersion.Count -ne 1 -or $assistantVersion[0] -cnotmatch '^[a-zA-Z0-9_.-]+$') { throw "SDK未声明唯一工具版本：$assistantToolName。" }
    $assistantToolVersions[$assistantToolName] = $assistantVersion[0]
}
$assistantCmake = Join-Path $ToolsPath "tools\cmake\$($assistantToolVersions['cmake'])\bin"
$assistantNinja = Join-Path $ToolsPath "tools\ninja\$($assistantToolVersions['ninja'])"
$assistantCompiler = Join-Path $ToolsPath "tools\$assistantCompilerTool\$($assistantToolVersions[$assistantCompilerTool])\$assistantCompilerTool\bin"
$assistantRom = Join-Path $ToolsPath "tools\esp-rom-elfs\$($assistantToolVersions['esp-rom-elfs'])"
foreach ($assistantRequired in @((Join-Path $assistantCmake 'cmake.exe'), (Join-Path $assistantNinja 'ninja.exe'), (Join-Path $assistantCompiler $assistantCompilerExecutable), (Join-Path $assistantRom $assistantRomExecutable))) {
    if (-not (Test-Path -LiteralPath $assistantRequired)) { throw "缺少SDK要求的已安装工具：$assistantRequired；脚本不会下载。" }
}
$assistantStage = Join-Path ([IO.Path]::GetTempPath()) ('xy_cached_' + [guid]::NewGuid().ToString('N').Substring(0, 12))
& $assistantPython -B (Join-Path $PSScriptRoot 'prepare_cached_firmware_build.py') --profile $Board --destination $assistantStage --idf-path $SdkPath
if ($LASTEXITCODE -ne 0) { throw '缓存构建输入准备失败，未启动编译。' }
$assistantEnvironmentNames = @('IDF_PATH', 'IDF_TOOLS_PATH', 'IDF_PYTHON_ENV_PATH', 'ESP_ROM_ELF_DIR', 'ESP_IDF_VERSION', 'PYTHONDONTWRITEBYTECODE', 'PYTHONPATH', 'PATH')
$assistantOldEnvironment = @{}
foreach ($assistantName in $assistantEnvironmentNames) { $assistantOldEnvironment[$assistantName] = [Environment]::GetEnvironmentVariable($assistantName, 'Process') }
$assistantVersionSource = Get-Content -LiteralPath (Join-Path $SdkPath 'tools\cmake\version.cmake') -Raw
$assistantVersionParts = foreach ($assistantPart in @('MAJOR', 'MINOR', 'PATCH')) {
    $assistantMatch = [regex]::Match($assistantVersionSource, "set\(IDF_VERSION_$assistantPart\s+(\d+)\)")
    if (-not $assistantMatch.Success) { throw 'SDK版本文件无效。' }
    $assistantMatch.Groups[1].Value
}
try {
    $env:IDF_PATH = $SdkPath
    $env:IDF_TOOLS_PATH = $ToolsPath
    $env:IDF_PYTHON_ENV_PATH = $PythonEnvironment
    $env:ESP_ROM_ELF_DIR = $assistantRom
    $env:ESP_IDF_VERSION = $assistantVersionParts -join '.'
    $env:PYTHONDONTWRITEBYTECODE = '1'
    $env:PYTHONPATH = Join-Path $assistantStage 'offline_guard'
    $env:PATH = (Join-Path $PythonEnvironment 'Scripts') + ";$assistantCmake;$assistantNinja;$assistantCompiler;" + $env:PATH
    Push-Location -LiteralPath $assistantStage
    try {
        if ($assistantProfiles[$Board].qspi_panel) {
            $assistantBsp = $assistantProfiles[$Board].bsp_component
            $assistantBspDirectory = Join-Path $assistantStage "vendor\waveshare__$assistantBsp"
            & $assistantPython -B (Join-Path $PSScriptRoot 'patch_waveshare_qspi.py') $assistantStage --panel $assistantProfiles[$Board].qspi_panel --component $assistantBsp --component-directory $assistantBspDirectory
            if ($LASTEXITCODE -ne 0) { throw '缓存QSPI源码补丁检查失败，未启动编译。' }
        }
        Write-Output "只使用现有SDK和组件，在 $assistantStage 隔离构建；不安装、烧录或发布。"
        & $assistantPython -B $assistantIdf "-DIDF_TARGET=$($assistantProfiles[$Board].target)" "-DASSISTANT_BOARD=$Board" build *> cached-build.log
        $assistantBuildExit = $LASTEXITCODE
        Get-Content -LiteralPath cached-build.log -Tail 45
        if ($assistantBuildExit -ne 0) { throw "缓存构建失败，退出码$assistantBuildExit；原始日志：$assistantStage\cached-build.log" }
        Write-Output "缓存构建命令退出0，产物仍留在隔离目录；这不代表干净安装、其他板型或实机验收通过：$assistantStage"
    } finally { Pop-Location }
} finally {
    foreach ($assistantName in $assistantEnvironmentNames) { [Environment]::SetEnvironmentVariable($assistantName, $assistantOldEnvironment[$assistantName], 'Process') }
}
