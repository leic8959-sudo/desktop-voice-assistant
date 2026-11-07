# OTA 0.3.19：显示暖启动复位（2026-11-06）

牛牛明确要求OTA。应用镜像已于北京时间09:07:40推送，09:07:57重新hello报告0.3.19，随后牛牛明确确认在USB和电池保持连接时“点云和文字都恢复了”。本次OTA暖启动显示恢复取得现场确认；多次软件重启/长期运行未独立复测，不能据此承诺所有黑屏情况都已消失。

## 构建和变更

只改显示启动前TCA9554的P0/P1/P2复位与版本号，其他冻结源码核对保持。复位低20ms、高150ms后进入既有SPI/LVGL初始化，不改充电或PMIC寄存器。原0.3.18有限首句协议、导航和音量保留。

复用已有构建工作目录`runtime/native-wakeclip-0318`，目录名不代表本次镜像版本。先将原0.3.18 main完整保存到`runtime/releases/wake-clip-v1-20261106/source-main-0318/`，确认与原候选哈希一致后，仅更新构建工作副本的display_ui.cpp和version.txt。

实际命令（激活已有环境后）：

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif\official-5.5.6'
$env:IDF_PYTHON_ENV_PATH='C:\Users\29248\.espressif\python_env\idf5.5_py3.12_env'
$env:IDF_COMPONENT_MANAGER='0'
& 'C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6\export.ps1'
& 'C:\Users\29248\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe' 'C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6\tools\idf.py' -C 'runtime/native-wakeclip-0318' -B 'I:\new find\desktop_voice_assistant\build' -DIDF_COMPONENT_MANAGER=0 build
python evidence/display-warm-maintenance-20261106.py preflight
python evidence/display-warm-maintenance-20261106.py backup
python evidence/display-warm-maintenance-20261106.py postflight
```

构建退出0，应用编译、链接、镜像及分区检查通过；保留已有LVGL弃用警告。源码候选的主机复位检查此前5场景通过，未冒充完整设备验收。

镜像app_desc0.3.19，2540224字节，SHA256 `532d0595cb92003e8c946938eda2e9ad88f69aeefbdc49530b8b55a7657f12b2`；3MiB应用分区剩605504字节（约19%）。日志`evidence/display-warm-build-env-20261106.log`和`evidence/display-warm-build-20261106.log`。只推应用OTA，未执行构建输出里的整机flash示例。

## 发布核验

原镜像保存为`runtime/releases/display-warm-reset-20261106/previous-0.3.18.bin`，新镜像同目录保存，摘要核对后原子替换现有OTA下载工件。认证GET返回200，下载摘要相同。私人一致性备份位于`runtime/reload-backups/display-warm-reset-20261106/`，不上传。

设备空闲、云未连接、双媒体停止、计时取消、无待执行调度时停止原PID46376，隐藏启动新网关PID41932；包装校验46文件及镜像，仅提供本次0.3.19一次OTA。09:08:15 postflight确认健康200、设备online/idle、版本0.3.19、wake_clip_v1=True、镜像与冻结源码匹配、九个保护文件哈希保持、tasks/memories分批比较相同、启动Traceback0、OTA推送1次、缓存“我在”可用、显示偏好回执true。

发布记录位于`runtime/releases/display-warm-reset-20261106/`；业务日志`runtime/display-warm-gateway.stderr.log`。没有串口操作、依赖安装、规则修改、Git写入或远程上传。设备显示恢复由牛牛确认，语音唤醒延迟仍由后续专项继续处理。
