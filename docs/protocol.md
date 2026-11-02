# Project Babble / Baballonia OSC 协议要点

本模块按线格式独立实现（C++），未复制上游源码；上游核对材料放在仓库之外。
核对时间与依据：

| 上游 | 状态 | 核对依据 | 结论口径 |
|---|---|---|---|
| [dfgHiatus/VRCFT-Babble](https://github.com/dfgHiatus/VRCFT-Babble) | **已归档**（2025-04-24，v2.1.2） | `BabbleOSC.cs` / `BabbleExpressions.cs` | 仅参考，不代表当前发送端 |
| [Project-Babble/Baballonia](https://github.com/Project-Babble/Baballonia) | 活跃（main，最后推送 2026-10-30） | `src/Baballonia/Services/ParameterSenderService.cs`、`OscSendService.cs`、`src/Baballonia/Models/OscTarget.cs`、`src/VRCFaceTracking.Baballonia/*` | **本文的默认依据** |
| [SummerSigh/ProjectBabble](https://github.com/SummerSigh/ProjectBabble) | 原项目，2025-04 后不再活跃 | VRCFT-Babble README 指向 | 未单独核对 |

## 传输与端口（已确认）

- OSC 1.0 over UDP，Baballonia 默认发往 **127.0.0.1:8888**
  （`OscTarget.cs`：`OSCAddress=127.0.0.1`、`OSCOutPort=8888`、`OSCInPort=8889`；
  归档版 VRCFT-Babble 与 Baballonia 自带的 VRCFT 接收模块 `BabbleConfig.json`
  同为 127.0.0.1:8888）。本模块默认监听 **8888**，与摘要一致。
- 当前发送端把每条地址作为**独立 UDP 数据报逐条发出**（`OscSendService.Send` 对数组
  逐条 `SendAsync`，不是 bundle），约每 10ms 全量刷新一遍。本模块的 OSC 解析器
  同时接受单消息与 `#bundle`，两种写法都能吃。
- 地址可带用户配置的前缀（`AppSettings_OSCPrefix`，如 `/avatar/parameters`）。
  本模块按**地址末段**匹配，前缀无关。
- 参数类型：单个 float，值域约 [0,1]（眼位 X/Y 为 [-1,1]）。

## 面部地址：45 条（已确认，两处上游一致）

归档版 `BabbleExpressions.cs` 与 Baballonia `ParameterSenderService.FaceExpressionMap`
的 45 条 camelCase 地址完全相同。按本模块的处理路径分两组：

**A. 与 ARKit 系数同名的 30 条**（走 FGB-Module-Kit 通用 ARKit → Unified 映射）：

```text
/jawOpen /jawForward /jawLeft /jawRight
/noseSneerLeft /noseSneerRight
/mouthFunnel /mouthPucker /mouthLeft /mouthRight
/mouthRollUpper /mouthRollLower /mouthShrugUpper /mouthShrugLower
/mouthClose
/mouthSmileLeft /mouthSmileRight /mouthFrownLeft /mouthFrownRight
/mouthDimpleLeft /mouthDimpleRight
/mouthUpperUpLeft /mouthUpperUpRight /mouthLowerDownLeft /mouthLowerDownRight
/mouthPressLeft /mouthPressRight /mouthStretchLeft /mouthStretchRight
/tongueOut
```

**B. 通用映射覆盖不到、但目标名与 Unified Expressions 同名的 15 条**
（本模块显式列举并单测逐项断言）：

```text
/cheekPuffLeft /cheekPuffRight /cheekSuckLeft /cheekSuckRight
/tongueUp /tongueDown /tongueLeft /tongueRight /tongueRoll
/tongueBendDown /tongueCurlUp /tongueSquish /tongueFlat
/tongueTwistLeft /tongueTwistRight
```

## 眼睛通道（与摘要不符，重点说明）

- 摘要里的 `/eyeLookInLeft` 一类 **ARKit 眼动地址 Babble 系应用根本不发送**。
  通用 ARKit 表恰好认识这些名字，所以它们若出现会被计入「已解析」但不会写入注视
  方向（注视只来自下面的 Eye 通道）；单测覆盖了这一行为边界。
- Baballonia 实际发送的是 PascalCase 的 6 条眼通道（`EyeExpressionMap` 当前生效项）：

  ```text
  /LeftEyeX /LeftEyeY /LeftEyeLid /RightEyeX /RightEyeY /RightEyeLid
  ```

- 另有 4 条（`/LeftEyeWiden /LeftEyeBrow /RightEyeWiden /RightEyeBrow`）在
  Baballonia 自带 VRCFT 接收模块的 switch 里仍被解析，但发送端已注释掉；
  本模块接受它们、只做记录，不参与睁眼度公式。**含眼睛通道数据：是**（眼位 + 眼睑）。
- `/LeftEyeLower`、`/RightEyeLower` 上下游都已注释，视为未知地址。
- 若用户在 Baballonia 里启用 VRC 原生眼动，还会向同一端口发
  `/tracking/eye/EyesClosedAmount`（1 float）与 `/tracking/eye/LeftRightPitchYaw`
  （4 float）。本模块不消费这两条（计入未知并忽略）——**未确认**是否有用户依赖该模式。

## 极化与单位

- `EyeLid`：1 = 全开。依据：上游 `BabbleVRC.cs` 直接 `Openness = EyeLid`，
  且 `ParameterSenderService` 的原生 VRC 分支按 `ClosedAmount = 1 - lid` 计算。
- `EyeX / EyeY`：归一化眼位（原生分支乘 45° 当角度用，说明量纲是 ±1 内的比例）。
  本模块按上游 BabbleVRC 直接写入 VRCFT `Gaze.x/y` 的口径处理：+X 右、+Y 上，
  归一成 ABI 要求的 HMD 局部单位向量（-Z 前）。**极化未经真机确认**（上游对 Y
  的注释「up is negative pitch」与直通赋值之间存在矛盾表述），置信度按 0.5 输出。
- 面部系数：NaN/Inf 清洗为 0，越界裁剪 [0,1]；眼位裁剪 [-1,1]（负值合法）。

## 与归档版 VRCFT-Babble 的一处刻意差异

归档版对 `/mouthFunnel`、`/mouthPucker` 先乘 4、对 `/mouthLeft`、`/mouthRight` 先乘 2
再写入四个/两个 Unified 目标——那是补偿 VRCFT 内部把多目标**平均**合并的行为。
FGB 的 `ShapeTable` 按 max 合并、每个目标独立生效，**乘系数反而会失真**，
因此本模块原值直通（单测锁定 `LipFunnelUpperLeft == 0.2`，而非 0.8）。

## 数值刷新模型

上游按「一地址一包」高频全量发送；本模块在一个 tick 内合并队列里的全部数据报，
而不是只取最新一包（否则其余 50 多个通道会被当成 0）。10 秒无包判定断流
（与上游 `TIMEOUT_MS = 10000` 同口径）。

## 冲突

8888 是 Babble 系应用给 VRCFT 的默认输出口，同一端口只能有一个接收者。
VRCFT 运行时会占用它；绑定失败时模块卡片给出端口与原因。改端口：
Baballonia 输出页改 `OSCOutPort`，并在模块目录写 `config.ini`：

```ini
port = 8888
```
