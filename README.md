# 伊迪斯

跑在自己电脑上的桌面语音助手。浏览器可以直接对话、管理提醒和记忆；ESP32 是可选的语音和点云终端。不需要租赁服务器。

这是公开的个人原型源码，不是开箱即用的安装包，也不是可暴露到公网的服务。

## 现在能做什么

- 在 Windows 上启动本地网关，用浏览器做文字或语音对话。
- 管理个人待办、提醒和记忆。记忆存在本机 SQLite。
- 可选连接 Home Assistant，控制已授权的家居设备。
- 可选编译 ESP32 固件，使用「你好，伊迪斯」唤醒和点云界面。

## 现在不能做什么

- 不能离线完成语音识别、对话和合成。这些默认使用你自己的云端账号，并可能计费。
- 不能把网关直接暴露到公网。当前没有多租户、生产级 TLS 和配额。
- 不能把本仓库许可证套到字体、ESP-SR、语音模型、小米集成或音乐内容上。见 [NOTICE](NOTICE)。
- 历史文档只记录当时的验收，不代表当前提交已经通过同样的实机验收。

## 自己的电脑上跑起来

支持基线：Windows，Python 3.12。依赖由你自己安装，仓库脚本不会自动安装。

```powershell
pip install -r requirements.txt
python tools/init_config.py --lan-ip 127.0.0.1
python gateway.py --config config.local.json
```

浏览器打开 <http://127.0.0.1:8765>。真实密钥只放在不被 Git 跟踪的 `config.local.json` 或 `.env`，示例见 `config.example.json` 和 `.env.example`。

操作演示：<https://leic8959-sudo.github.io/desktop-voice-assistant/>。演示只模拟启动和对话，不连接任何网关，也不接收密钥。

全新克隆＋全新 venv＋无密钥的安装验证记录见 [安装验证](docs/安装验证-20261117.md)。

没有模型密钥时，不要期待语音对话可用。管理页面、手工待办和记忆是否能在无密钥时完整使用，仍需你在自己的环境确认。

## 安全与贡献

- 不要提交密钥、录音、设备绑定、运行数据库或真实邮箱授权。
- 漏洞不要发在公开 Issue 里，见 [SECURITY.md](SECURITY.md)。
- 提交要求见 [CONTRIBUTING.md](CONTRIBUTING.md)。
- 发布前可运行 `python tools/open_source_preflight.py .`

更早的交付记录在 [docs/](docs/)。公开范围见 [公开说明](docs/公开说明-20261116.md)。
