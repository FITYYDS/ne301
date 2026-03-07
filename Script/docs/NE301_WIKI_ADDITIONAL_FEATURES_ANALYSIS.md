# NE301 Wiki 未覆盖特色功能系统设计框架分析

## 1. 结论先行

对照官方 NE301 wiki、当前仓库源码、以及已经产出的几份分析文档后，可以确认此前还没有被单独系统拆开的特色功能，主要集中在下面 7 类：

1. 工作模式与触发编排
2. 多网络接入与切换
3. MQTT/MQTTS 云交互与远程控制
4. 本地预览与 RTMP/RTMPS 推流
5. AI 模型生命周期管理
6. 设备硬件管理与运维管理
7. OTA/A-B 分区升级

这些能力并不是 7 个互不相关的小模块，而是共用同一套控制面骨架：

- Web/API 负责接入用户和平台请求
- Service 层负责业务编排
- `json_config_mgr` 负责策略持久化
- HAL/Network/Storage/VideoHub 负责真正执行

换句话说，NE301 的“特色功能”不是靠很多孤立 demo 叠起来的，而是靠一套统一的服务化框架拼出来的。

## 2. 分析范围与证据来源

本次分析时间：2026-03-07。

官方资料主要参考：

- [NE301 Overview](https://wiki.camthink.ai/docs/neoeyes-ne301-series/overview)
- [NE301 Quick Start / Data Interaction](https://wiki.camthink.ai/docs/neoeyes-ne301-series/quick-start)

本地源码主要参考：

- `Custom/Services/System/system_service.c`
- `Custom/Services/Communication/communication_service.c`
- `Custom/Services/MQTT/mqtt_service.c`
- `Custom/Services/RTMP/rtmp_service.c`
- `Custom/Services/AI/ai_service.c`
- `Custom/Services/Device/device_service.c`
- `Custom/Services/OTA/ota_service.c`
- `Custom/Services/Web/api/*.c`
- `Custom/Core/System/json_config_mgr.*`

本次不重复展开、默认你已经有上下文的文档：

- `STM32N6_SOFTWARE_FRAMEWORK.md`
- `NE301_LOW_POWER_ANALYSIS.md`
- `NE301_U0_N6_PROTOCOL_ANALYSIS.md`
- `WEB_ON_MCU_ANALYSIS.md`

## 3. 先说明哪些内容以前已经分析过

这次不是把整机再讲一遍，而是专门补 wiki 上还能看到、但之前没有单独拆成“系统设计框架”的部分。

之前已经独立分析过的内容：

- STM32N6 侧总体软件框架
- U0 + N6 低功耗设计
- U0 与 N6 双 MCU 通信协议
- Web 为什么能运行在 MCU 上，以及 NE301 的 Web 移植方式

所以这份文档关注的是“产品功能框架”，不是“基础平台原理”。

## 4. 根据 Wiki 识别出的新增特色功能

| Wiki 可见能力 | 用户视角看到什么 | 源码里对应的主模块 | 本次结论 |
| --- | --- | --- | --- |
| Work Mode / Trigger Method | 图片模式、视频模式、PIR、RTC、远程触发 | `system_service`、`api_work_mode_module`、`json_config_mgr` | 已落地，核心是策略编排框架 |
| Network Settings | WiFi / Cellular / PoE 选择与配置 | `communication_service`、`api_network_module` | 已落地，核心是多网络仲裁框架 |
| MQTT/MQTTS | 云端数据交互、远程拍照、远程休眠 | `mqtt_service`、`api_mqtt_module`、`system_service` | 已落地，核心是消息驱动控制框架 |
| Live Stream / Local Preview | 网页预览、RTMP 推流 | `video_hub`、`websocket_stream_server`、`rtmp_service` | 已落地，核心是视频数据面分发框架 |
| Model Deployment / Validation | 模型校验、重载、阈值调参 | `ai_service`、`api_ai_management_module`、`api_model_validation_module`、`ota_service` | 已落地，但“部署”是组合链路，不是单一模块 |
| Hardware Management | 图像、灯光、存储、设备信息 | `device_service`、`api_device_module` | 已落地，核心是设备服务统一抽象 |
| Firmware Upgrade / Config Import Export | OTA 升级、导出固件、导入导出配置、恢复出厂 | `ota_service`、`api_ota_module`、`api_device_module` | 大部分已落地，局部接口仍有占位 |

## 5. 这些特色功能共用的整机框架

### 5.1 总览图

```text
                               +----------------------------------+
                               | 浏览器 / Web UI / 云平台 / App   |
                               | MQTT Broker / RTMP Server / OTA  |
                               +----------------+-----------------+
                                                |
                         HTTP / WebSocket / MQTT / RTMP / Upload
                                                |
                                                v
+----------------------------------------------------------------------------------+
|                                   NE301 N6 App                                   |
|                                                                                  |
|  [API 适配层]                                                                    |
|  api_work_mode  api_network  api_mqtt  api_rtmp  api_ai  api_device  api_ota    |
|          |             |          |         |        |         |         |        |
|          +-------------+----------+---------+--------+---------+---------+        |
|                                        |                                         |
|                                  [Service 层]                                    |
|  system_service  communication_service  mqtt_service  rtmp_service  ai_service   |
|  device_service  ota_service  websocket_stream_server  video_hub                 |
|                                        |                                         |
|                            [Core / Config / Persist]                              |
|                           json_config_mgr / NVS / service_init                   |
|                                        |                                         |
|         +------------------------------+-------------------------------+          |
|         |                              |                               |          |
|   [Network & HAL]                [Video & AI]                   [Flash & SD]     |
| WiFi / Cellular / PoE        Camera -> Encoder -> AI           OTA slots / SD    |
| nm / lwIP / drivers                -> video_hub                 system_state      |
+----------------------------------------------------------------------------------+
```

### 5.2 这个总览图最重要的两个阅读点

1. 控制面和数据面是分开的。`system_service`、`communication_service`、`mqtt_service` 主要处理控制面；`video_hub`、`rtmp_service`、`websocket_stream_server` 主要处理数据面。
2. 大多数产品能力都不是直接调驱动，而是先过 `api_xxx -> service_xxx -> config/persist -> HAL` 这条链。

## 6. 各特色功能的系统设计框架

### 6.1 工作模式与触发编排

wiki 对外暴露的是：

- Image Mode
- Video Mode
- Trigger Method
- PIR trigger
- RTC timer trigger
- MQTT remote control

源码里真正的实现核心不是“很多模式枚举”，而是“少量主模式 + 一组触发策略配置”。

#### 6.1.1 框架图

```text
Web 设置 / MQTT 命令 / PIR / RTC / 按键 / U0 唤醒标志
                    |
                    v
          api_work_mode_module / mqtt_service
                    |
                    v
          system_controller + system_service
                    |
      +-------------+---------------------------+
      |             |                           |
      v             v                           v
 work_mode      trigger_config            wakeup_source_config
                    |
                    v
         system_service_capture_request()
                    |
          +---------+------------------------------+
          |                                        |
          v                                        v
 device_service_camera_capture()          视频模式相关链路
 device_service_camera_capture_fast()     preview / RTMP
          |
          v
 AI / SD / MQTT 上传 / 任务完成 / 休眠请求
```

#### 6.1.2 关键层次

- 接口层：`/api/v1/work-mode/status`、`/api/v1/work-mode/switch`、`/api/v1/work-mode/triggers`、`/api/v1/work-mode/video-stream/config`
- 编排层：`system_controller_set_work_mode()`、`system_controller_set_work_config()`、`system_service_process_wakeup_event()`
- 执行层：`system_service_capture_request()`、`system_service_capture_and_upload_mqtt()`
- 设备层：`device_service_camera_capture()`、`device_service_camera_capture_fast()`
- 持久化层：`json_config_set_work_mode_config()`

#### 6.1.3 设计要点

- `aicam_work_mode_t` 只有 `IMAGE` 和 `VIDEO_STREAM` 两个主工作模式，说明产品宣传里的“多模式”在软件里并不是很多互斥状态，而更像“主模式 x 触发方式 x 电源策略 x 联网策略”的组合。
- `timer_trigger`、`pir_trigger`、`remote_trigger` 被放进同一个 `work_mode_config_t`，说明触发器本质上是策略数据，不是线程。
- 抓拍入口被统一收敛到 `system_service_capture_request()`，这样按键抓拍、网页抓拍、MQTT 远程抓拍、唤醒后自动抓拍就共用一套执行链。
- 针对 RTC 唤醒，代码走 `device_service_camera_capture_fast()` 快路径，说明它明确做了“唤醒后最短时间完成任务”的优化。

#### 6.1.4 一个很重要的推断

从源码看，NE301 并不存在一个“25 个工作模式”的固件枚举表。更合理的解释是：

- wiki 的“25 modes”更可能是产品组合能力的表述
- 固件内部真正稳定的设计是“主模式少、策略组合多”

这是根据源码结构做出的推断，不是 wiki 明文原句。

### 6.2 多网络接入与切换框架

wiki 对外暴露的是：

- Network Settings
- WiFi / Cellular / PoE

NE301 的网络不是一个简单的 “连不连 WiFi” 开关，而是一套多通信介质仲裁框架。

#### 6.2.1 框架图

```text
Web 网络配置页 / 启动时策略 / 用户切换动作
                    |
                    v
              api_network_module
                    |
                    v
          communication_service
                    |
     +--------------+-----------------------------+
     |              |              |              |
     v              v              v              v
 selected_type   active_type   preferred_type   auto_priority
     |              |              |              |
     +--------------+--------------+--------------+
                    |
                    v
        WiFi STA/AP / Cellular / PoE 子适配层
                    |
                    v
             nm_* / netif / lwIP / drivers
                    |
                    v
          device_service_update_communication_type()
```

#### 6.2.2 关键 API

- 总览：`/api/v1/system/network/status`
- WiFi：`/api/v1/system/network/wifi/sta`、`/wifi/ap`、`/wifi/config`、`/wifi/scan`、`/wifi/disconnect`、`/wifi/delete`
- 通信类型：`/api/v1/system/network/comm/types`、`/comm/switch`、`/comm/prefer`、`/comm/priority`
- Cellular：`/api/v1/system/network/cellular/status`、`/info`、`/settings`、`/connect`、`/disconnect`、`/at`
- PoE：`/api/v1/system/network/poe/status`、`/info`、`/config`、`/validate`、`/apply`、`/save`

服务层关键函数：

- `communication_get_current_type()`
- `communication_get_selected_type()`
- `communication_switch_type_sync()`
- `communication_set_preferred_type()`
- `communication_apply_priority()`

#### 6.2.3 设计要点

- `selected_type` 表示“UI 当前选中的网络页/目标网络类型”。
- `active_type` 表示“当前真正承载业务流量的网络类型”。
- `preferred_type` 表示“用户长期偏好策略”。

这三个状态分离得很关键，因为它避免了“页面上切过去了，但底层网络还没连上”这种产品状态和连接状态混在一起。

#### 6.2.4 启动仲裁思想

`communication_service` 的启动逻辑很清晰：

1. 等 WiFi、Cellular、PoE 各自 ready
2. 如果用户设置了 `preferred_type`，优先尝试它
3. 如果没设置，就按优先级选择
4. 失败后默认不自动回退到别的网络

这说明它不是“网络漫游器”设计，而是“可解释、可控的连接仲裁器”设计。

### 6.3 MQTT/MQTTS 云交互与远程控制框架

wiki 对外暴露的是：

- MQTT Data Interaction
- MQTT/MQTTS
- 远程控制
- 事件抓拍上传

NE301 把 MQTT 设计成了“云端数据面 + 云端控制面”的合一入口。

#### 6.3.1 框架图

```text
Web MQTT 配置页 / Broker 下行命令 / 设备本地抓拍事件
                    |
                    v
               api_mqtt_module
                    |
                    v
                mqtt_service
                    |
     +--------------+------------------------------+
     |              |              |               |
     v              v              v               v
  connect task   topic config   event flags   control cmd parser
     |              |              |               |
     +--------------+--------------+---------------+
                    |
                    v
      publish_data / publish_image / publish_chunked
                    |
                    v
 system_service_capture_request() / system_service_request_sleep()
                    |
                    v
         device_service + AI + SD + MQTT PUBACK 等待
```

#### 6.3.2 关键 API

- 配置：`/api/v1/apps/mqtt/config`
- 连接控制：`/api/v1/apps/mqtt/connect`、`/disconnect`
- 测试发布：`/api/v1/apps/mqtt/publish/data`、`/publish/status`、`/publish/json`
- 抓拍上传：`/api/v1/device/capture`

服务层关键函数：

- `mqtt_service_set_config()`
- `mqtt_service_set_topic_config()`
- `mqtt_service_publish_image_with_ai()`
- `mqtt_service_publish_image_chunked()`
- `mqtt_service_wait_for_event()`
- `mqtt_service_parse_control_cmd()`
- `mqtt_service_execute_control_cmd()`

#### 6.3.3 命令执行语义

从 `mqtt_control_cmd_t` 可以直接看出，当前控制协议至少支持：

- `capture`
- `sleep`
- `task_completed`

也就是说，MQTT 在 NE301 里不只是上报数据，它还能直接驱动本地业务状态机。

#### 6.3.4 为什么这套设计适合 MCU

- 大消息走 chunked image，而不是一次性巨包
- 通过 `osEventFlags` 等待 `PUBLISHED` 事件，而不是同步死等 socket
- 抓拍上传仍然通过 `system_service` 统一入口，不让 MQTT 服务直接碰摄像头
- TLS 配置和普通 MQTT 配置走同一套服务模型，所以前端能直接做 MQTT/MQTTS 切换

这里还有一个可见事实：前端明确支持 `mqtt` / `mqtts` 两种配置形态，后端配置项也有 `ca_path`、`client_cert_path`、`client_key_path`、内联证书数据，说明 MQTTS 是正式产品能力，不是后续计划。

### 6.4 本地预览与 RTMP/RTMPS 推流框架

wiki 对外暴露的是：

- Live stream
- Video stream push

这部分最核心的设计点，是把“视频采集/编码”和“视频消费端”解耦。

#### 6.4.1 框架图

```text
Camera -> Encoder -> AI Pipeline
            |
            v
         video_hub
        /         \
       /           \
      v             v
websocket_stream   rtmp_service
server             |
      |            v
      |      async send queue
      |            |
      v            v
 Browser WS/MSE   rtmp_send task -> RTMP publisher -> RTMP server
```

#### 6.4.2 本地预览链

- API：`/api/v1/preview/status`、`/preview/start`、`/preview/stop`
- 服务：`websocket_stream_server`
- 分发中心：`video_hub`

本地预览不是单独再开一条摄像头采集链，而是让 WebSocket 订阅 `video_hub`。这意味着：

- 采集链只需要维护一份
- 本地预览和云推流可以同时消费同一份编码结果
- 新增别的消费者时，只要继续挂到 `video_hub` 即可

#### 6.4.3 RTMP 推流链

API：

- `/api/v1/apps/rtmp/config`
- `/api/v1/apps/rtmp/start`
- `/api/v1/apps/rtmp/stop`
- `/api/v1/apps/rtmp/status`

服务层关键函数：

- `rtmp_service_start_stream()`
- `video_hub_subscribe()`
- `send_queue_push()`
- `rtmp_send_task()`

#### 6.4.4 设计要点

- `rtmp_service` 默认走异步发送队列，避免在 Hub 回调里直接做重网络 IO。
- 发送队列满时丢旧帧，这比卡死采集线程更适合实时流。
- SPS/PPS 单独管理，保证推流端在关键帧边界恢复。
- 本地预览和 RTMP 推流都挂在 `video_hub` 之后，体现出明显的发布订阅设计。

#### 6.4.5 关于 RTSP 和 RTMPS 的判断

源码里能看到 `rtsp_server_url` 配置字段，但当前真正落地的服务是：

- WebSocket 本地预览
- RTMP 推流

没有看到独立 RTSP 服务模块，所以 RTSP 更像预留字段。

同时，底层 `librtmp` 能看到 `rtmps` 解析支持，因此可以合理推断：

- 如果上层 URL 传入 `rtmps://...`
- 底层库大概率具备协议支持

但当前 `rtmp_service` 自己并没有再做一层单独的 RTMPS 证书管理抽象，所以这里属于“底层能力存在，上层产品封装偏轻”的状态。这一条是结合库实现做出的推断。

### 6.5 AI 模型生命周期管理框架

wiki 对外暴露的是：

- Model Package Download
- Validate Package
- Trigger Model Deployment
- Model Deployment Result
- Model Parameter Adjustment

从源码看，NE301 的“模型部署”并不是一个孤立服务，而是四段式组合链路。

#### 6.5.1 框架图

```text
模型包上传 / 固件分区 / Web 调参 / Web 验证图片上传
                |
                v
         OTA / Flash slot / json config
                |
                v
            ai_service
                |
     +----------+-----------------------------+
     |          |              |              |
     v          v              v              v
 model load   reload       threshold set   single image validation
     |          |              |              |
     +----------+--------------+--------------+
                |
                v
           nn runtime / AI pipeline
```

#### 6.5.2 四个子问题分别是谁做

1. 模型包运输和落盘：`ota_service`、`api_ota_module`
2. 模型切换和重载：`ai_reload_model()`、`ai_load_model()`
3. 参数调优：`ai_set_confidence_threshold()`、`ai_set_nms_threshold()`
4. 离线验证：`ai_single_image_inference()`、`/api/v1/model/validation/upload`

#### 6.5.3 关键 API

- AI 状态：`/api/v1/ai/status`
- AI 开关：`/api/v1/ai/toggle`
- AI pipeline：`/api/v1/ai/pipeline/start`、`/stop`
- 阈值参数：`/api/v1/ai/params`
- 模型验证：`/api/v1/model/validation/upload`
- 模型重载：`/api/v1/model/reload`

#### 6.5.4 为什么说“部署”不是单一模块

因为源码中的模型生命周期被拆开了：

- 包头校验属于 OTA
- 模型镜像切换属于分区/配置
- 模型真正生效靠 `reload`
- 模型效果评估靠 validation upload
- 参数修正靠 threshold API

这是一种很典型的“运输层、安装层、运行层、验证层分离”的工程化做法。

#### 6.5.5 AI 模型验证链

`/api/v1/model/validation/upload` 的实现流程很完整：

1. 收 multipart/form-data
2. 解析 AI 输入图和绘制输出图
3. 调 `ai_single_image_inference()`
4. 生成 AI 结果 JSON
5. 把输出 JPEG 重新 Base64 返回

这说明它不是只做“能不能加载模型”的验证，而是做“模型在真实图片上的输出可视化验证”。

### 6.6 设备硬件管理与运维管理框架

wiki 对外暴露的是：

- Image Configuration
- Light Control
- Storage Management
- Configuration File Import and Export
- System restart

这部分的核心是 `device_service`，它相当于“设备能力总管家”。

#### 6.6.1 框架图

```text
Web 设置页 / 导入导出 / 维护动作
                |
                v
          api_device_module
                |
                v
           device_service
                |
   +------------+-------------+--------------+----------------+
   |            |             |              |                |
   v            v             v              v                v
 image       light         storage        device info      reset/restart
 config      control       policy         battery/mac      config reset
   |            |             |              |                |
   +------------+-------------+--------------+----------------+
                |
                v
       json_config_mgr / HAL / SD / LED / button / sensor
```

#### 6.6.2 硬件管理不是零散接口，而是统一设备服务

主要 API：

- 设备信息：`/api/v1/device/info`
- 存储：`/api/v1/device/storage`、`/api/v1/device/storage/config`
- 图像：`/api/v1/device/image/config`
- 灯光：`/api/v1/device/light/config`、`/api/v1/device/light/control`
- 相机：`/api/v1/device/camera/config`
- 设备名：`/api/v1/device/name`
- 配置导出：`/api/v1/device/config/export`
- 配置导入：`/api/v1/device/config/import`
- 恢复出厂：`/api/v1/system/factory-reset`

#### 6.6.3 设计要点

- 图像配置通过 `json_config_get/set_device_service_image_config()` 持久化，再由 `device_service` 下发到相机硬件。
- 灯光配置支持 `OFF / ON / AUTO / CUSTOM`，说明补光灯不是单纯开关，而是带策略的执行器。
- 存储管理不仅返回 SD 卡容量，还支持循环覆盖阈值，这是一种面向长期无人值守设备的设计。
- 设备名可以根据 MAC 自动生成，体现出“出厂即用”的运维思路。

#### 6.6.4 配置导入导出的设计

配置导出并不是把几个字段随便拼一下，而是直接围绕整机 `aicam_global_config_t` 做：

1. 读取当前全局配置
2. 序列化为 JSON
3. 包上一层导出元数据
4. 提供导入解析和重新校验

这说明 NE301 的 Web 管理界面不是只改单项参数，而是已经具备了“整机配置资产化”的意识。

#### 6.6.5 维护动作设计

恢复出厂并不只是清几个参数，它还会：

- reset NVS 配置
- 清理 AI 模型槽状态
- 触发系统重启

因此它的语义是“整机回到可重新部署状态”，不是“页面选项恢复默认值”。

### 6.7 OTA 与 A/B 分区升级框架

wiki 对外暴露的是：

- Firmware Upgrade
- 固件导出
- 模型包升级

#### 6.7.1 框架图

```text
Web 预检查 / 流式上传 / 本地文件升级 / 导出固件
                    |
                    v
               api_ota_module
                    |
                    v
                ota_service
                    |
     +--------------+-------------------------------+
     |              |               |               |
     v              v               v               v
 header check   system_state    stream write    slot export
     |              |               |               |
     +--------------+---------------+---------------+
                    |
                    v
      upgrade_begin -> write_chunk -> finish -> slot metadata
                    |
                    v
              A/B slot / boot selection
```

#### 6.7.2 关键 API

- 预检查：`/api/v1/system/ota/precheck`
- 上传：`/api/v1/system/ota/upload`
- 本地升级：`/api/v1/system/ota/upgrade-local`
- 导出：`/api/v1/system/ota/export`

服务层关键函数：

- `ota_validate_firmware_header()`
- `ota_validate_system_state()`
- `ota_upgrade_begin()`
- `ota_upgrade_write_chunk()`
- `ota_upgrade_finish()`
- `ota_upgrade_read_begin()`

#### 6.7.3 设计要点

- 先校验 1KB OTA header，再决定是否接收整个文件，这很适合 MCU 内存条件。
- AI 模型包在 OTA header 后面还要再看 1KB 模型包头，说明 AI 升级被看成特殊固件类型。
- 真正写入时是流式 chunk 写 Flash，不把整包先缓存在 RAM。
- A/B 槽位和 `system_state` 让升级具备回退和容错基础。

#### 6.7.4 当前实现边界

从 API 层看：

- `precheck`
- `upload`
- `export`

这三条是明显已落地的。

但 `/api/v1/system/ota/upgrade-local` 当前处理函数还是占位返回，说明：

- 底层 `ota_service` 已经支持 file/memory upgrade
- Web API 里的“本地升级”入口还没有完全收口成最终产品流程

这属于“服务层能力已具备，上层接口尚未彻底闭环”。

## 7. 这些特色功能背后的共同设计思想

把上面 7 类功能放到一起看，NE301 的设计思路其实非常统一。

### 7.1 控制面与数据面分离

- 网络切换、模式切换、MQTT 命令、OTA 升级属于控制面
- 视频帧、编码流、推流属于数据面

这让控制逻辑不会被大流量数据链拖死。

### 7.2 配置驱动，而不是写死逻辑

大量产品行为都落在 `json_config_mgr` 里：

- 工作模式
- 触发器
- 网络偏好
- RTMP 配置
- 图像和灯光配置

这说明功能切换靠“策略数据”，不是靠到处写 if/else。

### 7.3 服务化 Facade

每个大能力都先被封装成 `xxx_service`：

- `communication_service`
- `mqtt_service`
- `rtmp_service`
- `device_service`
- `ota_service`
- `ai_service`

Web 层基本不直接碰 HAL，这让后续 CLI、MQTT、按键、自动流程都能复用同一套服务接口。

### 7.4 发布订阅与统一入口

- 视频流通过 `video_hub` 做发布订阅
- 抓拍通过 `system_service_capture_request()` 统一入口
- MQTT 通过事件标志做异步确认

这使得功能扩展时不用反复复制整条执行链。

### 7.5 面向 MCU 约束的流式处理

NE301 很多地方都体现出明显的 MCU 思维：

- OTA 流式写入 Flash
- 大图 MQTT 分块上传
- RTMP 走异步发送队列
- 模型验证只在需要时做单图推理

这不是 Linux 服务器那种“先把资源堆起来再说”的写法，而是典型的嵌入式资源受限设计。

## 8. 当前从源码看到的几个边界

### 8.1 “25 modes”更像产品组合，不像固件枚举

源码只看到两个主工作模式，所以 wiki 的多模式宣传更可能是组合能力描述。

### 8.2 RTSP 更像预留位

配置里能看到 `rtsp_server_url`，但当前实际服务链是：

- WebSocket 本地预览
- RTMP 推流

没有看到完整独立 RTSP 服务。

### 8.3 模型部署是组合流程，不是一个 Deployment Manager

当前更像：

- OTA 包校验/写入
- 模型重载
- 参数调节
- 单图验证

组合起来完成“部署”，而不是单独一个超级服务。

### 8.4 OTA Web 入口和 OTA 服务能力还没完全对齐

底层 OTA 服务已经比较完整，但部分 Web API 入口仍有占位，这意味着后续产品化还可能继续收口。

## 9. 最终判断

如果只看 wiki，NE301 会像一个“功能很多的 AI 摄像头”。

但结合源码再看，它更像一个已经相当工程化的嵌入式平台，具备三层能力：

1. 平台底座：网络、存储、升级、低功耗、Web、双 MCU
2. 业务中枢：`system_service`、`communication_service`、`device_service`
3. 产品能力：触发抓拍、云交互、流媒体、模型运行、运维管理

因此，wiki 上这些“特色功能”并不是零散功能点，而是同一套服务化架构在不同业务场景下的投影。

## 10. 参考链接

- [NE301 Overview](https://wiki.camthink.ai/docs/neoeyes-ne301-series/overview)
- [NE301 Quick Start / Data Interaction](https://wiki.camthink.ai/docs/neoeyes-ne301-series/quick-start)
