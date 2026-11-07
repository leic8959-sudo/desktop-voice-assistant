# 米家 M40：本地电脑与 ngrok 测试

牛牛已确认：米家扫拖机器人 M40，中国大陆账号。此方案是临时测试环境，商用不要求用户部署电脑或 Home Assistant。

当前接入设备：M40 与小米空调 `xiaomi.airc.h10h00`。两台均已通过本地和 ngrok 公网接口的只读查询；真实动作仍需单独验收。下文早期仅M40的记录保留为历史状态，空调详情见文末。

链路：助手设备 → 现有电脑 8765 → 本机 Home Assistant 8123 → 米家云 → M40。ngrok 只转发助手的 8765，用于外网访问和测试；米家云端控制本身不需要公网回调。Home Assistant 仅绑定本机回环地址，不对外穿透。

## 1. 由牛牛安装测试依赖

初始检查：Docker 可用；没有 Home Assistant 镜像；未在 PATH 和常见安装目录找到 ngrok；现有助手服务监听 8765。随后牛牛明确授权代理安装依赖，已执行以下命令；安装和启动的实际完成状态见本轮交付报告。重建环境时可使用以下命令。

```powershell
cd 'I:\new find\desktop_voice_assistant'
winget install --id 9MVS1J51GMK6 --source msstore --exact
docker compose -f home_test/compose.yaml up -d
```

ngrok 官网推荐 Microsoft Store 包。若已有旧版3.3.1导致 winget 提示“已安装、无更新”，在上述商店安装命令末尾加 `--force`，让包管理器安装商店包；该选项不修改 Defender，也不恢复隔离文件。

第二条会拉取 Home Assistant stable 镜像并启动独立容器。配置和账号信息保存在被忽略的 `runtime/home-assistant`，不会进入公开文件。stable 为测试用浮动标签，正式交付需锁定验证后的镜像摘要。首次启动后，浏览器打开 http://127.0.0.1:8123，创建本地管理账号，将业务时区设为 Asia/Shanghai。不要开启免登录或 trusted_networks。

## 2. 安装小米官方集成并完成授权

集成安装脚本（本轮代理已按牛牛授权执行；已有安装时不要重复执行）：

```powershell
powershell -NoProfile -File tools/install_xiaomi_home.ps1
```

脚本下载小米官方仓库 v0.5.0 对应的固定提交 `75b3acb6325a3a8d295688b13904737d6738a6a5`，保留许可证，复制集成后只重启本测试容器；目标已存在时停止，避免覆盖已有集成。首次启动前可传 `-SkipRestart`，本轮使用该参数先安装源码。Home Assistant 随后可能下载集成声明的 Python 依赖。

Home Assistant → 设置 → 设备与服务 → 添加集成 → Xiaomi Home → 地区选中国大陆 → 在小米官方页面自行登录授权 → 选择包含 M40 的家庭。账号密码不填写到助手，也不发送到聊天。

授权先使用这台电脑浏览器完成。官方集成的回调是 `http://homeassistant.local:8123`，不是任意 ngrok 域名。若完成小米登录后该主机名无法解析，可将浏览器回调地址中的 `homeassistant.local` 换成 `127.0.0.1`，保留端口、路径和所有参数后访问；不要公开回调地址，它含一次性授权信息。如果仍失败，保留错误页面排查，不修改官方应用身份或绕过授权。

官方文档支持云端控制，但具体 M40 能力仍待本机授权后验证。不要从“App 可控”直接推定所有接口可控。若 M40 仅生成传感器或没有 vacuum 实体，先核对集成实际暴露能力，不硬编码型号、服务号或动作号。

## 3. 只读接通助手

在 Home Assistant 的 M40 设备页面找到实际生成的 `vacuum.*` 实体 ID。模型名称和设备信息也可在该设备页面核对，不要求米家 App 显示内部型号。

在 Home Assistant 用户个人资料的安全页面创建长期访问令牌，仅在本地填写配置；不要发到聊天。编辑现有 `config.local.json` 的下列三项，其余键和已有配置保留：

```json
{
  "home_assistant_url": "http://127.0.0.1:8123",
  "home_assistant_token": "填写本地创建的令牌，不要使用小米密码或助手device_token",
  "home_entities": {"M40": "vacuum.替换为实际实体ID"}
}
```

令牌继承 Home Assistant 用户权限；本次是单户测试，助手只允许访问明确配置的实体，不将此模型当作商业多用户隔离。小米授权令牌由官方集成保管，助手不直接读取。

当前运行的助手不会自动加载新源码或配置。准备完成后需重启助手进程，短暂中断当前语音连接；不重启或刷写硬件固件。重启命令为 `powershell -NoProfile -File start.ps1`。已有 8765 服务运行时，先停止其原启动进程，不同时启动第二份，也不盲目结束其他 Python 进程。

打开 http://localhost:8765，用原有 device_token 认证 → 家居刷新。先核对设备名称和清扫状态；电量只在平台提供时显示。刷新不发送动作，不启动云端语音。接口也可通过带 Bearer 认证的 `GET /api/home` 读取。

## 4. ngrok 外网测试

安装后如找不到命令，重开 PowerShell。在 ngrok 官方控制台获取自己的 authtoken，在本机完成配置，不发送到聊天：

```powershell
$assistantNgrokExecutable = Join-Path (Get-AppxPackage -Name 'ngrok.ngrok').InstallLocation 'ngrok.exe'
# 本轮runtime/ngrok-store.yml已配置账号令牌，不要再填写或公开令牌。
& $assistantNgrokExecutable config check --config "$PWD\runtime\ngrok-store.yml"
& $assistantNgrokExecutable http http://127.0.0.1:8765 --config "$PWD\runtime\ngrok-store.yml"
```

打开输出的 HTTPS 地址，填助手 device_token。不要添加 `--host-header=rewrite`，否则助手的同源检查可能拒绝请求。ngrok 不等于米家授权，也不等于家电已执行；它只提供 HTTPS/WSS 转发。不要转发 8123 或关闭现有认证。

外网只读验证先检查：页面可访问、无令牌请求 `/api/home` 返回401、认证后可读取 M40。先用手机切换蜂窝网络访问，不必马上改写设备的 NVS 地址或固件。设备若要直接走 ngrok，另行确认其 WSS/证书支持后再设置。

## 5. 动作验收

本轮准备代码不会自动控制真实家电。牛牛明确发起单次指令或点击对应按钮时，才向配置的 M40 发送动作。只支持平台 `supported_features` 实际报告的清扫、暂停、回充；未报告支持时拒绝执行。

1. 在适宜位置由牛牛发起清扫，确认实际移动，并记录平台状态。
2. 发起暂停，观察实体和机器是否停止。
3. 发起回充，区分“回充中”和“已到充电座”。
4. 设备离线或请求超时后，先刷新状态，不自动重试动作。

首次动作可以先通过面板验证，避免同时引入付费语音模型的不确定性；面板通过后再测试“让M40回充”等语音。平台报告状态和现场观察分别记录，不把HTTP成功当作物理完成。M40不一定提供电量、地图、分房间清扫等相同能力；本轮不加入地图或房间清扫。

## 许可与当前状态

小米官方集成及相关云API的许可证限定个人非商业 Home Assistant 使用，不自动覆盖商用研发或商业交付。本环境搭建和模拟验证不代表已获得该权限；实际使用需符合其许可，商业用途仍需小米另行许可。

- 小米官方源码：https://github.com/XiaoMi/ha_xiaomi_home
- 许可证：https://github.com/XiaoMi/ha_xiaomi_home/blob/v0.5.0/LICENSE.md
- HA扫地机器人能力：https://developers.home-assistant.io/docs/core/entity/vacuum/
- ngrok：https://ngrok.com/docs/getting-started/

实际状态以本轮交付报告为准：安装、账号授权、穿透联通、M40读取、真实动作和语音验收分别记录，不能互相替代。

## 2026-10-29 本轮实际结果

- Home Assistant 2026.10.4 已运行，官方 Xiaomi Home v0.5.0 已安装，牛牛已完成中国大陆账号授权。
- 识别型号 `xiaomi.vacuum.d110ch`，名称为米家扫拖机器人 M40；助手允许列表仅加入这一台机器人。
- 通过 Home Assistant 和助手 `GET /api/home` 均实际读到 `docked`，在线；平台报告支持清扫和回充，未报告暂停，vacuum 实体未提供电量。因此界面只显示实际支持的动作，不承诺电量或暂停可用。
- 本地配置已更新，助手已重载，终端已重连。没有向真实机器人发送动作，没有调用付费语音会话。
- `python -m unittest discover -s tests -v`：23项通过；`node --check web/app.js`、`docker compose -f home_test/compose.yaml config --quiet`、PowerShell安装脚本解析均通过。
- ngrok 3.3.1 已由 winget 安装，账号令牌已保存到标准用户配置；该版本实际连接返回 `ERR_NGROK_121`，账号要求至少3.28.0。
- 官方 ngrok 3.39.13 包下载完成后，被 Windows Defender 隔离，检测名 `Trojan:Win32/Kepavll!rfn`。文件已无法读取，因此没有完成SHA-256校验或安装新版；没有恢复隔离或修改安全防护。此检测尚未确定是否误报，需要牛牛核实处理。
- 公网隧道尚未建立，外网认证检查和外网M40读取尚未执行。本机助手页面 http://localhost:8765 可继续使用。
- 页面自动操作因工具无法可靠识别Chrome当前地址而终止，账号登录和授权由牛牛完成。
- 未执行Git提交、推送或正式发布。

不含凭据的结果记录：`evidence/m40-local-ngrok-20261029.json`。

## 2026-10-29 商店版复核：穿透已接通

前一节“公网隧道尚未建立”为初轮状态，现已通过微软商店版解决：

- 实际安装命令：`winget install --id 9MVS1J51GMK6 --source msstore --exact --force --accept-package-agreements --accept-source-agreements --disable-interactivity`，成功。
- 当前商店包版本 `3.39.11.0`，运行输出 `ngrok version 3.39.11-msix-stable`；签名类型为 Store。包内exe未单独附带Authenticode签名，不将其描述为单独签名的exe。
- `Start-MpScan -ScanType CustomScan -ScanPath <商店包目录>` 完成，包和进程仍正常存在；没有发现该商店包路径对应的新增隔离记录，Defender杀毒和实时防护保持开启。仅证明本次检查通过，不保证未来永远不会被隔离。
- 官网推荐商店包：https://ngrok.com/downloads/windows 。旧的直接下载包仍保留隔离，不认定此前告警已被证明是误报。
- 商店版配置目录存在应用隔离，本轮通过显式 `--config` 使用被忽略的 `runtime/ngrok-store.yml`，复用已有账号令牌，不重复索要账号信息。
- HTTPS入口：https://nonoscine-sonny-sortably.ngrok-free.dev ，转发本机8765。首页/health实际返回200；`GET /api/home` 无认证返回401，带现有助手令牌返回200，真实读到M40在线、`docked`、支持清扫和回充。
- ngrok启动后曾遇到短暂公网连接中断；后续curl和aiohttp请求已通过。没有关闭TLS校验，没有修改Host，没有恢复隔离或添加排除项。
- 没有修改硬件的服务器地址，设备仍连接本地桥接；公网HTTP查询已验证，公网WebSocket语音、真实家电动作和正式商用部署尚未验证。

公网复核记录：`evidence/ngrok-store-check-20261029.json`。原有旧版3.3.1包未卸载；复现命令显式选择商店版，以免误用旧版。

## 空调识别与控制适配

原因：米家官方集成已导入“小米空调巨省电 1匹 一级 适用米家速冷静”，型号 `xiaomi.airc.h10h00`；助手原先只允许M40，且代码没有支持 `climate` 类型。因此原先在助手中看不到空调，与米家App绑定和ngrok无关。

已补充 `climate` 状态读取、面板模式选择与温度输入、`control_climate` 语音工具、API分发。配置只加入这台空调，别名“空调”，未导入同家庭的电暖器和风扇灯。模式、温度上下限及步长使用设备实际报告值；用户只说“开机”时先明确模式，不替用户选择制冷或制热。设置温度不会附带开机或切换模式参数。

2026-10-29 21:48:35（Asia/Shanghai）实际只读结果：空调在线、关闭；室温22.9°，设定温度27°；支持制冷、制热、除湿、送风、关闭，设定范围16～31°，步长0.5°。页面温度沿用平台单位。M40仍在线、在充电座。本地和公网 `GET /api/home` 均返回200并包含两台设备，硬件已重连。

实际验证：`python -m unittest discover -s tests -v` 共32项通过（含空调边界、API与语音工具分发，使用模拟平台）；`node --check web/app.js` 通过。真实平台只读取状态，没有开关空调、改变模式或温度，没有付费语音调用。真实写入、模型实际调用新工具的效果与平台状态反馈仍需按用户单次明确指令验收。

结果记录：`evidence/climate-integration-20261029.json`。未执行Git提交、推送或正式发布。
