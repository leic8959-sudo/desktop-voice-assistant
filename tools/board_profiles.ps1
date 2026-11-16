function Get-AssistantBoardProfiles {
    param([string]$ProjectPath)
    $assistantRegistry = Get-Content -LiteralPath (Join-Path $ProjectPath 'native_board_profiles.json') -Raw | ConvertFrom-Json
    if ($assistantRegistry.schema_version -ne 1 -or -not $assistantRegistry.profiles) { throw '板型登记表格式不支持。' }
    $assistantProfiles = @{}
    $assistantProjects = @{}
    $assistantVariants = @{}
    foreach ($assistantEntry in $assistantRegistry.profiles.PSObject.Properties) {
        $assistantProfile = $assistantEntry.Value
        if ($assistantEntry.Name -cnotmatch '^[a-z0-9][a-z0-9.-]{0,63}$' -or
            $assistantProfile.project_name -cnotmatch '^[a-z][a-z0-9_]{0,30}$' -or
            $assistantProfile.bsp_component -cnotmatch '^[a-z0-9_-]+$' -or
            $assistantProfile.target -notin @('esp32s3', 'esp32p4', 'esp32s31', 'esp32c5') -or $assistantProjects.ContainsKey($assistantProfile.project_name)) {
            throw '板型ID、应用身份、组件或芯片目标无效／重复。'
        }
        if ($assistantProfile.flash_size_mb -notin @(4, 8, 16, 32)) { throw '板型Flash容量无效。' }
        if (@($assistantProfile.compile_definitions).Count -lt 1 -or
            @($assistantProfile.compile_definitions | Where-Object { $_ -cnotmatch '^ASSISTANT_BOARD_[A-Z0-9_]+=1$' }).Count -gt 0 -or
            $assistantProfile.legacy -isnot [bool] -or $assistantProfile.qspi_panel -notin @($null, 'co5300', 'sh8601')) {
            throw '板型实现宏或构建选项无效。'
        }
        if ($assistantProfile.legacy) {
            if ($null -ne $assistantProfile.upstream_variant -or -not $assistantProfile.legacy_reason) { throw '历史兼容入口不能冒充上游变体。' }
        } else {
            if ($assistantProfile.upstream_variant -cnotmatch '^[a-z0-9.-]+$' -or $assistantVariants.ContainsKey($assistantProfile.upstream_variant)) { throw '上游变体映射无效／重复。' }
            $assistantVariants[$assistantProfile.upstream_variant] = $true
        }
        $assistantProfiles[$assistantEntry.Name] = $assistantProfile
        $assistantProjects[$assistantProfile.project_name] = $true
    }
    if ($assistantProfiles.Count -lt 1 -or $assistantProfiles.Count -gt 256) { throw '板型登记表条目数量无效。' }
    return $assistantProfiles
}
