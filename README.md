# FGB BabbleModule

[![build](https://github.com/Aqamoe/FGB-BabbleModule/actions/workflows/ci.yml/badge.svg)](https://github.com/Aqamoe/FGB-BabbleModule/actions/workflows/ci.yml)

**状态：0.1.0，未验证（untested）**。协议解析、映射与 ABI 生命周期有单元测试和合成 UDP
端到端冒烟；真机（Baballonia / Project Babble 硬件）验收待补，见
[docs/verification.md](docs/verification.md)。

[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge) 的 **Project Babble /
Baballonia** 原生输入模块。直接接收 Babble 系应用按 VRCFT 兼容模式发到本机
**8888** 端口的 OSC over UDP，输出 Unified Expressions 表情（含舌头 12 通道）与
逐眼睁眼度、注视方向。不需要 VRCFT，不注入任何进程，也不需要管理员权限。

Native input module for FaceGaze Bridge. Reads the OSC-over-UDP stream
(127.0.0.1:8888 by default) that Baballonia / Project Babble already emit for
VRCFaceTracking and outputs Unified Expressions shapes, tongue and per-eye
openness/gaze. No VRCFT, no process injection, no admin rights.

> **上游风险提示**：官方 VRCFT 桥 [dfgHiatus/VRCFT-Babble](https://github.com/dfgHiatus/VRCFT-Babble)
> 已于 2025-04 归档（最后发布 v2.1.2）。本模块以仍活跃的
> [Project-Babble/Baballonia](https://github.com/Project-Babble/Baballonia)
> （main 分支，2026-10 核对）输出协议为当前依据；两边面部地址表一致（45 条），
> 眼动通道仅 Baballonia 有。若上游再改协议，本模块可能静默失配，请配合
> docs/protocol.md 的版本依据使用。

## 功能 / Features

- **表情**：30 条与 ARKit 同名的地址走 Kit 通用映射；15 条 Babble 特有通道
  （`/cheekPuffLeft`、`/tongueUp` 等）与 Unified Expressions 同名直连并逐项单测。
- **舌头**：12 条 tongue 通道全部落到真实形状名（`TongueOut`…`TongueTwistRight`）。
- **眼睛**：`/LeftEyeLid`（1 = 全开）直通为睁眼度；`/LeftEyeX`、`/LeftEyeY`
  归一为 HMD 局部注视单位向量，置信度 0.5（摄像头估计值，极化未真机确认）。
- **断连可恢复**：10 秒没有数据时在模块卡片提示原因（与上游超时口径一致），
  数据恢复后自动回到活跃。
- **可诊断**：端口冲突、畸形 OSC、未知地址都有明确日志；未知地址计数上报，
  不静默丢数据。

## 不提供 / Not provided

| 数据 | 原因 |
|---|---|
| 头姿 | 该协议没有头姿地址 |
| 瞳孔 | 协议没有该字段 |
| 2D 注视点 | 协议只有归一化眼位，无屏幕坐标 |
| 渲染级注视 | 眼动来自摄像头估计，请配真正的眼动源 |
| `/tracking/eye/*` 原生 VRC 眼动包 | 属 VRChat 专用格式，本模块忽略并计数 |

## 环境要求

| 项 | 要求 |
|---|---|
| 系统 | Windows 10 / 11 x64 |
| 宿主 | FaceGaze Bridge（原生模块 ABI v2） |
| 来源应用 | Baballonia（或 Project Babble），启用 VRCFT/OSC 输出，目标 127.0.0.1:8888 |
| 硬件 | Babble 官方面捕（ESP32-S3 摄像头）或 Baballonia 支持的其它设备 |
| 其它 | 关闭 VRCFaceTracking：它和本模块抢同一个 UDP 端口 |

## 安装

1. 从 Releases 下载 `babble-0.1.0.zip`。
2. FaceGaze Bridge →「模块库 → 原生模块 → 安装原生模块 (.zip)」，或「模块」页「从 .zip 安装」。
3. Baballonia：输出页启用 VRCFT/OSC 输出，地址填 `127.0.0.1`（跨机填本机 IP）、
   端口填 `8888`（与模块 `config.ini` 一致）。
4. 「模块」页把 **Project Babble** 设为启动（角色通常给 `expression` 与 `eyes`），
   重启引擎；Babble 端开始发送后模块卡片应显示活跃。
5. 本模块为**社区未验证**实现。

## 配置

模块目录下可选 `config.ini`：

```ini
port = 8888
```

改完重启引擎，并同步修改 Baballonia 的输出端口。未知键会被记录一条警告，不会静默失效。

## 从源码构建

需要 Visual Studio 2022（x64）、CMake ≥ 3.24 与 Node 20（脚本用）。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

SDK 与 Kit 由 `sdk.lock.json` / `kit.lock.json` 在配置阶段拉取并校验 SHA256，仓库里不含 SDK 头文件副本。
本地想指向已解压的 Kit：

```powershell
cmake -S . -B build -A x64 -DFGB_KIT_DIR="D:/code/FGB-Module/FGB-Module-Kit"
```

打包与测试：

```powershell
cmake --build build --config Release --target package-modules   # build/Release/babble-<版本>.zip
ctest --test-dir build -C Release --output-on-failure           # 协议与映射单测
cmake --build build --config Release --target smoke-test        # 真实 ABI + 合成 UDP 冒烟（需 8888 空闲）
```

## 目录结构

```
src/main.cpp                ABI 生命周期、UDP 接收、逐 tick 合并、状态上报
src/babble_payload.*        OSC 解析与 Unified Expressions 写入
module/                     fgb-native.json 与图标
tests/                      协议单测、真实 ABI 冒烟
docs/protocol.md            线格式、地址全表与映射依据（已确认/未确认标注）
docs/verification.md        真机验收清单
cmake/                      依赖锁解析（SDK / Kit 都是拉取，不复制）
```

## 排错

| 现象 | 处理 |
|---|---|
| 卡片显示「无法绑定 UDP 8888：端口已被占用」 | 关闭 VRCFT 或其它接收端；或用 `config.ini` 换端口并同步改 Baballonia 输出端口 |
| 一直提示「还没收到任何数据包」 | 确认 Baballonia 已启用输出、目标地址是本机、防火墙放行回环/局域网 UDP |
| 表情不动、眼睛正常 | 角色没分给 `expression`；或上游只启用了眼动输出 |
| 日志出现「无法识别的地址」 | 上游 App 改了地址表；把版本号和日志一并提到 issue |
| 眼动方向左右/上下相反 | 极化未经真机确认（见 docs/protocol.md），请复现并反馈，先按现状对齐上游口径 |
| 舌头不动 | 确认 Babble 模型输出了 tongue 系数（部分配置默认关闭） |

## 致谢 / Attribution

- 线格式与映射依据参考 [Project-Babble/Baballonia](https://github.com/Project-Babble/Baballonia)
  与已归档的 [dfgHiatus/VRCFT-Babble](https://github.com/dfgHiatus/VRCFT-Babble)（C#/MIT）；
  本仓库是该线格式的独立 C++ 实现，未包含其源码。
- ABI 头与宿主：[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge)；共享工具链：[FGB-Module-Kit](https://github.com/Aqamoe/FGB-Module-Kit)。

## 许可

Apache License 2.0，见 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)。

---

## English summary

**Status: 0.1.0, community-untested.** Listens for the OSC-over-UDP stream
(127.0.0.1:8888 by default) that Baballonia / Project Babble already emit for
VRCFaceTracking and writes 45 face addresses plus eye position/eyelid channels
into Unified Expressions shapes and per-eye attributes. No VRCFT required, no
process injection. Note that the official VRCFT bridge (dfgHiatus/VRCFT-Babble)
is archived; this module follows the active Baballonia output
(`ParameterSenderService.cs`, verified 2026-10) as its source of truth. Eye
polarity is inferred from upstream code and has not been validated on real
hardware — reports are welcome. Full protocol notes: [docs/protocol.md](docs/protocol.md).
