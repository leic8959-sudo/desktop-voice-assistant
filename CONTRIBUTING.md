# 贡献指南

## 开始前

先阅读 `README.md` 和 `SECURITY.md`。不要提交 `config.local.json`、`runtime/`、真实设备日志、授权文件、音频或固件密钥材料。

## 提交要求

- 说明变更目的、影响范围和已验证的命令。
- 新增外部服务时提供可替换的接口边界和离线测试路径。
- 不在日志、测试夹具或文档中写入真实令牌和个人数据。
- 修改设备协议、认证或数据持久化时，同时更新对应文档。

发布前可运行：

```powershell
python tools/open_source_preflight.py .
```
