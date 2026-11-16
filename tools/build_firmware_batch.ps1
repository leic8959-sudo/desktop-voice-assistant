param(
    [ValidateSet('all', 'B1', 'B2', 'B3', 'B4', 'B5')]
    [string]$Batch = 'B1',
    [switch]$Execute,
    [switch]$SourceProfilesOnly
)
$ErrorActionPreference = 'Stop'
$assistantRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'board_profiles.ps1')
$assistantProfiles = Get-AssistantBoardProfiles -ProjectPath (Join-Path $assistantRoot 'firmware\assistant_espidf')
$assistantMatrixPath = Join-Path $assistantRoot 'firmware\assistant_espidf\board_matrix_v2.5.2.json'
$assistantMatrix = Get-Content -LiteralPath $assistantMatrixPath -Raw | ConvertFrom-Json
if ($assistantMatrix.schema_version -ne 2 -or $assistantMatrix.source_sha -ne '49b4380b3faa0ceadd28a3e78aaf0f311c46c57e') {
    throw '板型矩阵格式或上游提交不符，请先核对固定来源。'
}
$assistantVariants = @($assistantMatrix.release_variants | Where-Object { $Batch -eq 'all' -or $_.batch -eq $Batch })
if ($assistantVariants.Count -eq 0) { throw "批次 $Batch 没有登记变体。" }
foreach ($assistantVariant in $assistantVariants | Where-Object { $_.status -eq 'source_profile_present' }) {
    if (-not $assistantVariant.native_profile -or -not $assistantProfiles.ContainsKey($assistantVariant.native_profile) -or
        $assistantProfiles[$assistantVariant.native_profile].upstream_variant -cne $assistantVariant.full_name) {
        throw '批次矩阵与原生板型登记映射不一致，请刷新矩阵；未启动任何构建。'
    }
}
$assistantReadyProfiles = @($assistantVariants | Where-Object { $_.status -eq 'source_profile_present' -and $_.native_profile } | Select-Object -ExpandProperty native_profile -Unique)
$assistantPending = @($assistantVariants | Where-Object { $_.status -ne 'source_profile_present' -or -not $_.native_profile })
$assistantLimited = @($assistantVariants | Where-Object { $_.source_feature_scope -eq 'known_required_feature_gaps' })
Write-Output "批次 $($Batch)：登记 $($assistantVariants.Count) 个变体，现有源码入口 $($assistantReadyProfiles.Count) 个，尚缺入口 $($assistantPending.Count) 个。未做依赖解析、编译或实机验收。"
Write-Output "已有源码中 $($assistantLimited.Count) 个入口明确缺少本轮必需功能，仍待完整适配；其余入口也未因此获得功能或实机验收。"
foreach ($assistantVariant in $assistantVariants) {
    [pscustomobject]@{
        Variant = $assistantVariant.full_name
        Target = $assistantVariant.target
        SourceProfile = $assistantVariant.native_profile
        Status = $assistantVariant.status
        FeatureScope = $assistantVariant.source_feature_scope
        RequiredFeatureGaps = @($assistantVariant.required_terminal_feature_gaps) -join ', '
    }
}
if (-not $Execute) {
    Write-Output '仅输出批次计划。-SourceProfilesOnly只处理已登记入口；存在明确必需功能缺口时，完整终端批次仍拒绝执行。'
    return
}
if (@($assistantMatrix.source_feature_audit_inputs).Count -ne 9 -or
    @($assistantVariants | Where-Object { $_.native_profile -and $_.source_feature_scope -notin @('known_required_feature_gaps', 'no_known_omissions_in_selectors') }).Count -gt 0) {
    throw '缺少当前源码的功能缺口审计字段，请刷新矩阵；未启动构建。'
}
$assistantRegistryPath = Join-Path $assistantRoot 'firmware/assistant_espidf/native_board_profiles.json'
if ((Get-FileHash -LiteralPath $assistantRegistryPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $assistantMatrix.sources.native_registry.sha256) {
    throw '板型登记源码已变化，请刷新矩阵中的功能缺口审计；未启动构建。'
}
foreach ($assistantInput in $assistantMatrix.source_feature_audit_inputs) {
    $assistantInputPath = Join-Path $assistantRoot $assistantInput.path
    if (-not (Test-Path -LiteralPath $assistantInputPath) -or
        (Get-FileHash -LiteralPath $assistantInputPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $assistantInput.sha256) {
        throw '功能缺口审计的源码已变化，请刷新矩阵；未启动构建。'
    }
}
if ($assistantLimited.Count -gt 0) {
    throw "完整终端批次含明确缺功能的候选：$($assistantLimited.full_name -join ', ')。保留实验源码，但不能以源码存在替代完整适配；未启动构建。"
}
if ($assistantPending.Count -gt 0 -and -not $SourceProfilesOnly) {
    throw "批次尚有 $($assistantPending.Count) 个未适配变体，未启动任何构建。不会静默跳过。"
}
if ($assistantReadyProfiles.Count -eq 0) { throw '所选批次没有可调用的源码入口，未启动构建。' }
# Use the native registry; an upstream inventory row cannot
# introduce an arbitrary shell command or turn an unimplemented board into one.
foreach ($assistantProfile in $assistantReadyProfiles) {
    if (-not $assistantProfiles.ContainsKey($assistantProfile)) {
        throw "未知源码入口 $assistantProfile，未启动任何构建。"
    }
}
if ($assistantPending.Count -gt 0) {
    Write-Output "已明确选择只构建源码入口；其余 $($assistantPending.Count) 个变体继续保持未适配。"
}
foreach ($assistantProfile in $assistantReadyProfiles) {
    & (Join-Path $PSScriptRoot 'build_firmware.ps1') -Board $assistantProfile
    if (-not $?) { throw "板型 $assistantProfile 构建失败，停止本批。" }
}
Write-Output '本批源码入口构建命令已结束；矩阵状态需依据产物和验收证据另行更新，不自动标记全部适配。'
