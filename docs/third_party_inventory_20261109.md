# 第三方许可与资源来源清单（开源准备第一阶段）

日期：2026-11-09，Asia/Shanghai。关联：[评估与执行路线](open_source_readiness_20261109.md)。这是静态依赖／来源初表，不是完整 SBOM、法律授权结论或已经完成的镜像审计。

## 1. 阅读方式与分发边界

- **仓库分发**：以 `git ls-files` 中已有内容为准；只写依赖名／下载脚本不等于已经把依赖源码放进仓库。
- **镜像／固件分发**：安装进镜像的运行库、系统包及链接进固件的库／模型，也需要遵守各自许可。不得以“源码仓库没有该二进制”跳过未来镜像／固件义务。
- **外部服务**：调用API并不自动获得模型、音色、音乐、服务结果或品牌的再分发权。代码许可、服务条款、账号授权和数据使用分别核对。
- 本表官方URL在本轮以公开HTTP读取，固定版本页优先；`main` 或其他参考版本不冒充实际锁定。第三方下载包不落盘、不安装；本地只读非敏感清单／许可／源码。最终发行前仍需按实际构建产物和平台补齐通知、摘要和传递依赖。

## 2. Python、容器与 Web

| 组件／资源 | 当前声明或固定版本 | 许可与本轮证据 | 当前仓库／未来分发 | 待确认与最小处理 |
| --- | --- | --- | --- | --- |
| 本项目原创 Python／Web／固件 | HEAD `397788bc5e20e6908bcbdbb31fabc768eff89f32` | 根目录未指定整体LICENSE；不能推定默认MIT | 源码在库；未来包／镜像／固件可能含它 | 核对来源／贡献权利后由牛牛决定；建议比较Apache-2.0与MIT，不覆盖第三方许可 |
| CPython | README历史机器3.12.9；Docker `python:3.12-slim`，非不可变版本 | PSF及历史／捆绑组件条款；[3.12.9官方LICENSE](https://raw.githubusercontent.com/python/cpython/v3.12.9/LICENSE)已读取，仅作历史解释器参考 | 不随源码带解释器；镜像包含Python及基础系统 | 固定实际镜像digest、Python补丁版、Debian系统包及其通知，不能把历史3.12.9当未来镜像版本 |
| aiohttp | `3.12.15`，requirements固定 | [官方tag原文](https://raw.githubusercontent.com/aio-libs/aiohttp/v3.12.15/LICENSE.txt)为Apache-2.0；[PyPI固定版本元数据](https://pypi.org/pypi/aiohttp/3.12.15/json)标 **Apache-2.0 AND MIT**，说明不能只列顶层Apache | 未vendor；用户安装；未来镜像包含 | 获取实际wheel的license／vendor通知，厘清MIT部分；本轮未下载wheel。猜测LICENSE-MIT URL为404，不能因此忽略MIT |
| PyAV／av | `19.0.2`，requirements固定 | [官方tag LICENSE](https://raw.githubusercontent.com/PyAV-Org/PyAV/v19.0.2/LICENSE.txt) BSD-3-Clause；[PyPI](https://pypi.org/pypi/av/19.0.2/json)同；[版本安装说明](https://raw.githubusercontent.com/PyAV-Org/PyAV/v19.0.2/docs/overview/installation.rst)要求Python≥3.12 | 未vendor；用户安装；未来镜像包含 | 保留版权／免责及二进制再分发声明；不能把PyAV许可等同底层FFmpeg |
| FFmpeg及编解码库 | 该PyAV版本官方说明wheel捆绑FFmpeg；源码编译可链接FFmpeg 8.x；项目未锁实际wheel／链接库 | [FFmpeg官方法律页](https://ffmpeg.org/legal.html)已读取：许可受构建配置和组件影响，涉及LGPL／可选GPL等 | 不以独立FFmpeg文件在库；安装wheel、镜像和解码服务可携带库 | 锁具体平台wheel、实际库版本、构建配置、许可证／源码提供要求；不能笼统写“BSD，任意打包” |
| aiohttp必需传递依赖 | 元数据列 `aiohappyeyeballs>=2.5.0`、`aiosignal>=1.4.0`、`attrs>=17.3.0`、`frozenlist>=1.1.1`、`multidict>=4.5,<7`、`propcache>=0.2.0`、`yarl>=1.17.0,<2`；再有其自身传递项 | 本项目没有解析锁，实际版本及其官方许可待确认，不按本机已装值推断 | 用户安装／未来镜像间接分发；不在源码库vendor | 在支持的Python／OS上冻结含hash的依赖树，逐包查固定版license；不是只检查requirements两行就完成 |
| 条件／可选Python依赖 | aiohttp元数据 `async-timeout` 仅Python<3.11；speedups额外项有aiodns、Brotli／brotlicffi；当前requirements未请求extras | 条件不等于实际安装，不纳入未经观察的产物清单 | 取决于解析平台／extras | 若首发统一3.12，勿误列async-timeout必装；实际产物若含则补其版本与通知 |
| 工具／验收Python依赖 | `generate_device_font.py`导入fontTools；AEC工具导入numpy；历史命令涉及pytest；requirements未声明这些 | 版本未固定，官方固定版原文未逐项核对 | 工具源码在库，依赖本体未vendor；不应塞进基础运行必需依赖 | 建可选开发／字体／声学清单并查许可；不自动安装、不以当前机器全局包作为前置 |
| 时区数据 | `ZoneInfo("Asia/Shanghai")`；requirements无tzdata | Linux可能由系统提供；干净Windows常需tzdata；版本与实际来源待确认 | 未来系统镜像或可选Python包 | 基础安装先复现并补来源／版本；不能认定所有Windows安装都自带IANA库 |
| 浏览器页面 | 原生HTML／CSS／JS、AudioWorklet；无基础前端package manifest／打包框架 | 本轮未发现需随基础页面分发的外部JS库；这不是对全部图标／文案来源的完整审计 | `web/`在库；未来镜像静态服务它 | 核对独立拷贝片段、图形和文案来源；不用为开源另引一套前端框架 |
| Web系统字体 | CSS引用system-ui、Microsoft YaHei、PingFang SC等 | 字体名称引用不等于获得字体文件再分发权 | 未发现Web随包的ttf／woff；使用系统已有字体 | 保留系统回退；不从Windows／macOS提取商业字体打包 |
| 浏览器开发工具 | `tests/*.mjs`可从PLAYWRIGHT_MODULE_PATH或模块解析寻找Playwright；无项目统一锁 | 版本／浏览器二进制／许可集合未固定 | 测试源码在库；依赖与浏览器本体未vendor | 固定受支持开发路径；不要把Codex或本机工具目录设为陌生用户必需项 |
| Docker基础与构建辅助 | `python:3.12-slim`、`espressif/idf:v5.5.2`、`ubuntu:24.04`均缺digest | 各基础镜像含多种软件许可，不能继承成一个Python或ESP-IDF许可证 | 不在源码库；构建时获取；发布镜像／固件时相关运行产物需核查 | 生成镜像层／系统包清单、许可证与源码义务；构建辅助镜像与实际交付镜像分别列 |

## 3. ESP32 固件、组件和模型

版本以 `firmware/assistant_espidf/dependencies.lock` 为本轮声明基准，**未执行重建以证明其被实际使用**。`tools/build_firmware.ps1`在新volume没有输入该lock，是复现缺口。managed_components被Git忽略，部分本地许可仅作补充；不能要求新克隆者靠本机下载目录查许可。

官方组件链接模式为 `https://components.espressif.com/components/{namespace}/{name}/versions/{version}/license`。下表链接均定向读取；dl_fft的license页404后改查固定提交。

| 固定组件 | 固定版本 | 官方原文及结论 | 分发／义务与待确认 |
| --- | --- | --- | --- |
| ESP-IDF | 5.5.2；main声明≥5.5.0 | [官方tag LICENSE](https://raw.githubusercontent.com/espressif/esp-idf/v5.5.2/LICENSE)，Apache-2.0顶层 | SDK不在库；固件链接其组件，含各子组件独立许可（例如TLS／系统库）；须按实际构建收集，不能只附顶层LICENSE |
| ESP-SR | 2.4.7 | [官方固定版license页](https://components.espressif.com/components/espressif/esp-sr/versions/2.4.7/license)：ESPRESSIF MIT License | 仅授权用于乐鑫产品的条件下使用等；保留版权／许可／免责。SDK未vendor，未来固件会含相关库／模型，逐项核查模型与预编译库附加声明 |
| esp_codec_dev | 1.5.11 | [官方原文](https://components.espressif.com/components/espressif/esp_codec_dev/versions/1.5.11/license)，Apache-2.0 | 本地zl38063设备目录另含Microsemi MIT许可；实际链接与否待核，通知不能只看顶层 |
| esp_lvgl_port | 2.9.0 | [官方原文](https://components.espressif.com/components/espressif/esp_lvgl_port/versions/2.9.0/license)，Apache-2.0 | 未来固件保留适用通知；源码下载包本轮不随库分发 |
| esp_websocket_client | 1.8.0 | [官方原文](https://components.espressif.com/components/espressif/esp_websocket_client/versions/1.8.0/license)，Apache-2.0 | 同上 |
| Waveshare esp32_s3_touch_amoled_1_8 | 2.0.3 | [官方原文](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_8/versions/2.0.3/license)，Apache-2.0 | `tools/patch_waveshare_qspi.py`有本地补丁；发行要记录补丁和修改声明，不把该组件许可扩展到所有Waveshare示例／商标 |
| LVGL | 9.6.0~1 | [官方原文](https://components.espressif.com/components/lvgl/lvgl/versions/9.6.0~1/license)，MIT | 本地树包含独立库及字体通知；按启用配置识别实际链接资源，不能统归MIT |
| cJSON | 1.7.19~2 | [官方原文](https://components.espressif.com/components/espressif/cjson/versions/1.7.19~2/license)，MIT | 复制／实质部分须保留版权许可 |
| cmake_utilities | 0.5.3 | [官方原文](https://components.espressif.com/components/espressif/cmake_utilities/versions/0.5.3/license)，Apache-2.0 | 构建工具与固件运行代码分列 |
| dl_fft | 0.8.0 | 官方license版本页404；本地manifest指向esp-dl固定commit `1ab1e264d4270bfad6a45760ea19273dca0cedfc`；[官方固定提交manifest](https://raw.githubusercontent.com/espressif/esp-dl/1ab1e264d4270bfad6a45760ea19273dca0cedfc/tools/dl_fft/idf_component.yml)标MIT | 本地源文件头还有Apache-2.0；不是“全包已确认MIT”。需收集固定包的文件级通知／上游说明 |
| esp-dsp | 1.8.0 | [官方原文](https://components.espressif.com/components/espressif/esp-dsp/versions/1.8.0/license)，Apache-2.0 | 按实际固件保留适用许可／NOTICE |
| esp_io_expander | 1.2.1 | [官方原文](https://components.espressif.com/components/espressif/esp_io_expander/versions/1.2.1/license)，Apache-2.0 | 同上 |
| esp_io_expander_tca9554 | 2.0.3 | [官方原文](https://components.espressif.com/components/espressif/esp_io_expander_tca9554/versions/2.0.3/license)，Apache-2.0 | 同上 |
| esp_lcd_co5300 | 2.2.0 | [官方原文](https://components.espressif.com/components/espressif/esp_lcd_co5300/versions/2.2.0/license)，Apache-2.0 | 同上 |
| esp_lcd_panel_io_additions | 1.0.1~1 | [官方原文](https://components.espressif.com/components/espressif/esp_lcd_panel_io_additions/versions/1.0.1~1/license)，Apache-2.0 | 同上 |
| esp_lcd_touch | 1.2.1 | [官方原文](https://components.espressif.com/components/espressif/esp_lcd_touch/versions/1.2.1/license)，Apache-2.0 | 同上 |
| esp_lcd_touch_cst816s | 1.1.2 | [官方原文](https://components.espressif.com/components/espressif/esp_lcd_touch_cst816s/versions/1.1.2/license)，Apache-2.0 | 同上 |
| esp_lcd_touch_ft5x06 | 1.1.1 | [官方原文](https://components.espressif.com/components/espressif/esp_lcd_touch_ft5x06/versions/1.1.1/license)，Apache-2.0 | 同上 |

lock中LVGL还声明条件依赖FreeType、libjpeg-turbo、libpng、lz4，Waveshare有IDF≥6时的usb条件项。它们未作为本轮已解析lock组件出现；不能仅因被声明就说实际已编入，也不能在未来启用功能后漏查。

### ESP-SR 的原文限制

固定版本原文包含：

> Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case, it is free of charge ...

以及版权与许可通知保留要求。应如实命名为带乐鑫产品适用条件的ESPRESSIF MIT License。GitHub猜测 `v2.4.7/LICENSE` 返回404，官方组件版本页面读取成功；本轮结论以该版本页和本地同名许可交叉核对为准。

`wake_word.cpp`首选mn6_cn，回退mn5q8_cn；`sdkconfig.defaults`启用量化MultiNet5及Flash模型。模型分区文件本身未入库，实际发布将用哪个模型、资源摘要、模型获取路径和逐件许可仍需确定。不把许可页视为所有额外语音模型／训练数据的统一授权。

### 小智参考来源与工具

`tools/prepare_sources.py`明确下载 `78/xiaozhi-esp32` tag **v2.5.2** 的板型固件与源文件至被忽略的目录。[官方tag LICENSE](https://raw.githubusercontent.com/78/xiaozhi-esp32/v2.5.2/LICENSE)为MIT，包含Shenzhen Xinzhi Future Technology及贡献者版权。本仓库保留兼容协议和调研文档，不代表重新分发了整个小智源码树；实际借用／修改的代码片段、模型、语音与图标需按文件追溯并保留通知。上游固件镜像也不能因顶层MIT就忽略其中组件。

esptool／烧录GUI／下载的SDK未在源码仓库；`flash_native.py`引用本机esptool.exe。首发若附烧录工具，须先固定官方来源／版本、校验摘要和各自许可；本轮不承诺这些未随库工具已获再分发许可。

## 4. 字体、音频与其他资源

| 资源 | 实际来源／版本证据 | 在库与未来分发 | 许可／待确认 |
| --- | --- | --- | --- |
| NotoSansCJKsc-Regular.otf | `assets/fonts/`受Git跟踪；SOURCE文件是上游下载说明，LICENSE.md是简短来源描述，完整许可在`OFL.txt`；未固定字体上游tag／commit | OTF本身在库，未来源码归档会带；不应被整体许可覆盖 | 本地OFL为SIL OFL1.1、Adobe版权和Reserved Font Name “Source”；[官方Sans LICENSE](https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/LICENSE)读取成功但main非固定来源。补该具体文件上游版本／摘要／修改说明；保留OFL，遵守保留名称及字体不可单独出售等条款 |
| 固件生成字体ui_font.c／h | `tools/generate_device_font.py`从上述OTF通过lv_font_conv转位图；`device_font_coverage.json`有生成报告 | C字体在库并链接入未来固件 | 作为字体派生资源保留OFL和来源；现生成文件头主要为生成命令，需确保独立源码／二进制发行都带完整字体通知；不用“生成代码”规避字体许可 |
| LVGL内置Source Han Sans等字体 | sdkconfig开启`LV_FONT_SOURCE_HAN_SANS_SC_16_CJK`；本地LVGL树有SourceHanSansSC、DejaVu、FontAwesome通知 | 组件树被忽略；实际固件可能含内置字体 | 查实际链接map／配置及对应字体许可，未启用的字体不冒充实际分发；FontAwesome、DejaVu等不能因LVGL MIT而忽略自身条款 |
| 字体转换工具 | `tools/font_converter/package.json`范围^1.5.3；lock固定**lv_font_conv 1.5.3**；[官方npm版本元数据](https://registry.npmjs.org/lv_font_conv/1.5.3)为MIT，gitHead `899ea1128d2e82bb015a319c8a7d18a82359ab3a` | package与lock在库；node_modules不在库；非网关必需 | 固定版LICENSE原文的猜测tag URL404；本轮仅版本元数据及lock结论，发行工具时补原文，不标为已逐字核查 |
| “我在。”唤醒应答 | `assets/wake_ack/ack.pcm`和manifest在Git；模型qwen3.8-omni-flash-realtime、音色Katerina；SHA256 `5ad743be668bc5ab5fd9e8629dcdca5bcae8254148174a2fed02caef634c656b`；生成时间2026-11-05T23:53:56.402510+00:00；生成脚本调用已有云端Realtime | 当前源码归档会带音频，COPY全目录的镜像也可能带 | 有生成元数据，不等于有音色／输出再分发许可；官方服务条款、输出授权和必要标识待确认。未确认前从拟公开分发物移除或换有明确授权的资源；本轮未生成新音频 |
| 诊断音频数组wake_probe_samples.h | 固件源码已跟踪；头注释称temporary deterministic audio diagnostic，存在probe_positive等数组；宏控制启用 | 即使默认未编译，源文件公开仍分发其内容 | 没有从注释确定其合成／真人来源、说话人授权或再分发依据；本轮不试听／重建。P0核来源，未清前从公开候选排除，不能靠扩展名绕开 |
| 真人录音／设备捕获／私人日志 | `.gitignore`排除wav／mp3／evidence／runtime；本轮未读取 | 不应作为公开测试夹具或镜像内容 | 忽略规则不能证明全历史无敏感数据；全历史／二进制／数组及归档审查是后续P0 |
| 雨声／用户音源／音乐 | 现有桌面媒体与互联网点歌模块，部分生成或用户自备资源／外部流 | 不把用户上传音源和运行缓存纳入发布；未来实际附带资产需逐项清单 | 自备／订阅／30秒试听权限不等于公开再分发；生成资源也需记录算法／输入来源；不打包QQ曲目或用户音乐 |
| UI图形／外部文档图像 | `web/`存在CSS／SVG式界面表现，历史docs有截图引用 | 本轮未看到独立受Git跟踪的png／jpg／svg资源清单；不代表所有内联图形原创 | 公开前核对内联图标／截图／文本引用和个人信息；不把无独立图片文件当作已审计 |

字体工具lock还固定以下传递包，以下为lock许可证元数据，**尚未逐包读取固定版官方原文**：

| 包 | 版本 | lock标注 |
| --- | --- | --- |
| argparse | 2.0.1 | Python-2.0 |
| bit-buffer | 0.2.5 | MIT |
| debug | 4.3.4 | MIT |
| make-error | 1.3.6 | ISC |
| mkdirp | 1.0.4 | MIT |
| ms | 2.1.2 | MIT |
| opentype.js | 1.3.4 | MIT |
| pngjs | 6.0.0 | MIT |
| string.prototype.codepointat | 0.2.1 | MIT |
| tiny-inflate | 1.0.3 | MIT |

这些是可选生成工具的依赖，不应增加浏览器基础安装的运行成本；若未来发布开发镜像，则检查其实际存在版本并随镜像给出通知。

## 5. 家居依赖与小米限制

| 项目 | 固定版本与官方来源 | 分发状态 | 许可与结论 |
| --- | --- | --- | --- |
| 通用Home Assistant客户端 | 本项目`home_control.py`、builtin，通过HA正式REST／WS；未vendor HA Python包 | 自有客户端源码在库；用户自备HA是独立服务 | 客户端原创代码许可待选；调用标准HA不自动授权任意受限厂商集成／API |
| Home Assistant Core | `home_test/compose.yaml`为`ghcr.io/home-assistant/home-assistant:stable`，未固定实际版本／digest | Compose引用在库，HA源码／镜像不随源码归档 | [官方2026.11.0 LICENSE](https://raw.githubusercontent.com/home-assistant/core/2026.11.0/LICENSE.md)读取为Apache-2.0，仅作参考版，**不是本地stable实际版本证据**；实际分发镜像需锁digest并查全部组件 |
| Xiaomi Home Integration | 安装脚本固定commit **75b3acb6325a3a8d295688b13904737d6738a6a5**，脚本标v0.5.0；[该commit官方LICENSE.md](https://raw.githubusercontent.com/XiaoMi/ha_xiaomi_home/75b3acb6325a3a8d295688b13904737d6738a6a5/LICENSE.md)已读取 | 本项目仅含下载／复制安装脚本，集成本体位于被忽略runtime；未来COPY上下文若未隔离仍可能带入 | 个人、有限、非排他、不可转让／转授权，只限非商业Home Assistant用途；另有软件开发用途限制、保留通知与商标限制；不是标准开源许可证 |
| 小米云服务／账号授权 | 由HA集成及本人账号使用，非助手通用开源许可 | 不分发账号／授权；不操作第三方账号 | 服务／API使用范围另核；不因用户自己安装、单独进程或“家庭使用”自动宣称集成方案可公开／商业交付 |
| ngrok临时测试 | 历史记录使用，未作为基础运行依赖固定版本 | 不随库带客户端；历史URL不是发行入口 | 隧道服务条款／可用性独立；Alpha不默认需要公网隧道，不沿用旧临时域名作为安装承诺 |

小米固定原文关键段落（非本项目改写）：

> 小米在此授予您一项个人的、有限的、非排他的、不可转让的、不可转授权的、免费的权利，仅限于您为非商业性目的使用 Home Assistant 而复制、使用、修改、分发授权作品。为避免疑义，本许可证未授权您将授权作品用于任何其他用途，包括但不限于开发应用程序（APP）、Web 服务以及其他形式的软件等。

原文还要求重新分发无论源码／目标代码、修改与否都保留版权标识、免责声明和许可证副本；未经另行书面许可不得用“小米”“米家”等字样宣传相关软件／硬件。授权作品包括集成及相关米家云服务API接口、源代码和目标代码。

因此，拆成插件、外置HA、让用户自行下载，都不能改变授权作品本身的用途限制；这些部署方式可以影响是否直接分发，但不能据此推断使用目的已获许可。对本助手具体接法的适用性应依据是否使用受限代码／API、真实用途及必要书面授权判断。标准公开基线建议先不携带该集成和默认安装预设，也不承诺“米家能力可随本助手自由再分发”。通用HA能力可选择有明确许可的集成做验收，不把本轮原型的既有米家记录当公开许可依据。

## 6. 音乐、模型与其他服务

| 项目 | 实际版本／接法与来源 | 分发与费用／权利边界 | 待确认 |
| --- | --- | --- | --- |
| Music Assistant Server | `docker-compose.music.yml`固定**2.10.9**但无digest；[官方tag LICENSE](https://raw.githubusercontent.com/music-assistant/server/2.10.9/LICENSE)为Apache-2.0 | 服务独立容器，未来镜像／运行依赖的其他包和音源插件各自许可；开源服务器不授权音乐内容 | 锁镜像digest和QQ等实际音源提供方／依赖版本，核该版本全部通知 |
| 自定义MA播放器 | `tools/music_assistant_player/`源码在库，Compose挂入MA Python3.14包目录；来源／改写片段需追溯 | 可能与MA源码接口形成适配，当前整体原创许可未定 | 不因调用Apache项目就推定所有适配源码来源已闭合；固定接口兼容与版权说明 |
| QQ等音源 | 经本人账号授权和MA社区适配，不是requirements固定包 | 账号／会员／地区／试听分别约束；短播放成功不等于整曲权利或许可通过 | 真实适配版本、服务条款、缓存／代理流和内容再分发权限待核；不打包音源账号与曲目 |
| 阿里百炼／DashScope | qwen Realtime、qwen文本、paraformer-realtime-v2、cosyvoice-v2及音色；通过HTTP／WS调用，非下载模型权重 | API key本人提供，调用可能收费；模型／音色／生成音频的许可独立于网关源码 | 本轮未调用付费API；账号地区、模型可用性、价格、输入输出使用及合成音再分发条款需在实际发行前核官方现行条款 |
| DeepSeek | HTTP chat completions，示例deepseek-flash | 替换推理，现Chat管道仍依赖阿里语音；源码开源不等于模型服务免费 | 当前模型ID、可用性／费用／服务条款待核；无真实key验收，不宣传任意兼容服务即插即用 |
| Google／Microsoft | Gmail／Calendar／Graph，aiohttp直连及OAuth／续期接口 | 无相应第三方SDK vendor；邮件与日程是用户数据，OAuth许可不等于代码许可 | OAuth应用配置、最小scope、敏感权限审核、撤销与服务政策；本轮不访问账号 |
| 飞书／Slack／Notion／GitHub／Todoist／高德 | `integrations/`使用正式API／Webhook，HTTP层已有显式超时 | API客户端与外部服务条款分别核；用户token／Webhook不可公开，结果可能有缓存／展示限制 | 固定API版本、配额／价格、展示归属及写入权限；Todoist `/rest/v2`现行支持状态待核，不因源码存在就称已兼容 |

服务名称和模型ID用于识别接口，不代表商标、品牌背书或营销授权。免费额度、开源实现和个人账号各有独立边界。

## 7. 从初表到可发行清单

发布基线A2的最小交付应是：按源码包／网关镜像／可选固件／开发工具四类实际分发物，记录组件名、固定版本／commit／digest、来源URL、文件摘要、许可证原文、修改情况、通知位置、限制与处置。未分发的外部服务也保留数据／费用／授权说明，但不要伪造其为可随包再许可的软件。

需优先关闭的未决项：小米用途／宣传限制、已入库合成音与诊断数组来源、原创许可与借用片段、PyAV实际wheel及FFmpeg配置、Python传递锁和系统包、ESP-SR模型／子组件、字体确切上游版本及二进制通知。余项按是否出现在首发分发物决定门禁，不无限扩展到未使用功能。

本轮未复制第三方许可到新LICENSE／NOTICE文件，未更改安装脚本或打包规则，未安装／构建／上传任何依赖或产物。官方访问失败的猜测URL已明确记录；待核实项不写为“已合规”。

## 8. 后续板级来源补记（2026-11-10）

Jiuchuan S3新增的 `firmware/assistant_espidf/main/jiuchuan_panel_commands.h` 保留固定小智提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 中 `main/boards/jiuchuan-s3/esp_lcd_panel_gc9301.c` 的48条有效初始化命令／数据及延时。来源文件明确Apache-2.0，数据头保留Espressif2021–2024版权及“2024 Your Name”原始占位声明；条款和改编说明分别位于嵌套 `main/third_party/xiaozhi_jiuchuan/LICENSE`／README，未改变整体项目许可。该许可文件取自本机已安装ESP-IDF的Apache2.0原文，不安装依赖。

本地 `jiuchuan_panel.cpp` 单独实现受检查的面板生命周期和真实MADCTL镜像／旋转；未整份复制上游驱动中的无效接口。来源许可声明与可运行、可分发、硬件芯片识别是不同证据；发行前仍需按实际固件汇集全部通知。其余此前板级来源见对应 `main/third_party/` 及各批适配记录，本段不代替完整SBOM。

ATK BOX2 Wi-Fi后续新增 `main/atk_box2_panel_commands.h`，改编同一固定小智提交下 `main/boards/alientek/atk-dnesp32s3-box2-wifi/atk_dnesp32s3_box2.cc` 的20条面板命令数据。来源无嵌套许可文件，保留仓库MIT版权与原文于 `main/third_party/xiaozhi_atk_box2/`，README说明来源及改编范围；ES8389仍使用本机已有codec_dev1.5.11的独立驱动及其许可，没有整份导入上游电源／按键应用。这不改变整体项目许可或代表设备验收完成。

微雪Audio Board后续新增 `main/jd9853_panel_commands.h`，保留同一固定提交 `main/boards/waveshare/esp32-s3-audio-board/esp_lcd_jd9853.c` 的32条有效默认命令，包含32字节gamma数据和延时。来源声明Espressif2022–2023／Apache-2.0，原通知和条款位于 `main/third_party/xiaozhi_jd9853/`；本地面板生命周期检查传输结果与绘制范围，不整份复制上游应用板类。嵌套通知不改变项目整体许可，也不证明实际面板或设备通过验收。

Quandong S3 Dev后续新增 `main/quandong_panel_commands.h`，保留同一固定提交 `main/boards/quandong-s3-dev/quandong_s3_dev_board.cc` 的19条有效ILI9341命令数据和顺序。三条cmd0记录只有1字节却声明17／17／41字节，已移除，不猜测原意；睡眠退出和亮屏交给固定驱动与共用API。仓库MIT版权与原文保留于嵌套 `main/third_party/xiaozhi_quandong/`，README说明改编范围；没有复制整份板类应用或改变项目整体许可。目标仍缺ILI9341组件未编译，来源通知不代表屏幕修正已通过实机验收。

Guition后续新增本地 `main/nv3041a_panel.c/.h`，改编固定小智提交 `main/boards/guition/jc4827w543/esp_lcd_nv3041a.c/.h`；保留92项原始表／延时及QSPI02寄存器、32-3C像素封装，增加固定范围和状态提交检查。仓库MIT通知与其注明的Arduino_GFX来源BSD／Adafruit2012通知分别保留在 `main/third_party/xiaozhi_guition/LICENSE`、`Arduino_GFX_LICENSE.txt`。另读Arduino_GFX固定a8615fbc7dcd05c769c3031173e2f60dbf410469的header／license，发现部分电源值不同，故不继续宣称两版本表逐字节相同；本地表仅与固定小智板源相同。GT9111.2.1只读公开API／源码未安装，本板仍未编译，嵌套通知不改变项目整体许可或构成硬件接受。

LCDWiki后续新增 `main/lcdwiki_panel_commands.h`，改编固定小智提交 `main/boards/lcdwiki-es3c35p/lcdwiki-es3c35p.cc` 的63条ST77922命令数据，全部负载／顺序／延时保留并做静态长度检查；嵌套 `main/third_party/xiaozhi_lcdwiki/` 保留仓库MIT版权／条款与改编范围。本地显示／触摸生命周期另写，不复制板类MCP应用；正式ST779222.0.2组件仅公开源码阅读、尚未安装。来源通知、源表一致性与实际屏幕／可发行状态分别记录，不改变项目整体许可。

Kevin RGB后续新增 `main/kevin_rgb_commands.h`，仅改编固定小智提交 `main/boards/kevin/yuying-313lcd/esp_lcd_gc9503.c` 中36条有效GC9503V命令数据／顺序／延时，包含52字节gamma；注释备用表未恢复。源标Espressif2023／Apache-2.0，原条款与改编说明位于嵌套 `main/third_party/xiaozhi_kevin_rgb/`。本地生命周期只编排既有三线IO、IDF RGB和LVGL port接口，没有整份导入上游GC驱动自动删除IO的封装；项目整体许可未改。来源通知与屏幕方向／刷新／设备验收分别记录。
