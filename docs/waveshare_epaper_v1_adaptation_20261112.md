# 微雪1.54 V1受限方案实验（未确认，入口已撤回）

用户尚未选择接受取消本地唤醒和OTA；上一轮不应据此登记支持。本轮已撤回profile、板型配置和4处源码变更。下文只保留实验经过，不能作为已授权功能或当前适配状态。实验记录在`evidence/epaper-v1-withdrawal-20261112/`，不计入官方已构建总数。

V1是ESP32-S3FH4R2，4MB Flash、2MB QUAD PSRAM。它与V2共用已核对的200×200 SSD电子纸引脚、159字节波形、ES8311音频和供电极性，但内存和Flash无法容纳当前模型／双OTA布局。

## 受限范围

- 分区为NVS 9000/4000、phy f000/1000、单factory app 20000/3e0000；没有model、otadata或第二应用分区。
- `single_app_update.cpp`拒绝网络OTA并提示通过USB更新；没有把4MB板伪装成支持OTA。
- CMake选择`manual_wake_word.cpp`，不初始化本地唤醒模型。BOOT0必须先松开再按住说话，卡片确认沿已有手动输入路径；Wi-Fi配网和在线对话仍可用。
- 共享V2电子纸worker、200×200像素映射、屏幕供电、ES8311 24kHz接线；电池、电源键、RTC、SHTC3、SD、触摸均未知或未实现。
- 绑定凭据和NVS保留在0x9000，不执行擦除。所有动作结果仍以后台真实确认和控制台为准。

## 构建证据

V1完整缓存构建退出0，stage为`I:\xy_board_builds_20261109\xy_cached_452a66e5e7df`，应用`assistant_wspaper1`为2,093,920字节，factory分区剩余1,969,312字节，无model产物。镜像、ELF、分区检查退出0，实验当时的195个main文件、201份输入摘要匹配当时工作区。

本地现有微雪1.8实物无法验收V1，屏幕方向、残影、声音、BOOT按住说话、USB升级、配网绑定、提醒和功耗均为`not_run`。没有安装依赖、刷机、Git写操作或发布。