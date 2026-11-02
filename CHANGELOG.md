# 更新日志

遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 与语义化版本。

## [未发布]

## [0.1.0] - 未发布
### 新增
- Project Babble / Baballonia OSC over UDP（默认 127.0.0.1:8888）→
  Unified Expressions 表情、舌头与逐眼方向。
- 45 条面部地址：30 条走 Kit 通用 ARKit 映射，15 条 Babble 特有通道显式列举并单测。
- 6 条眼部地址（`/LeftEyeX /LeftEyeY /LeftEyeLid` 及 Right 对应项）；
  Widen/Brow 通道记录但不参与公式。
- 支持自定义 OSC 前缀（按地址末段匹配）；单消息与 `#bundle` 都能解析。
- `config.ini` 支持覆盖端口。
- 协议单测 + 通过真实 ABI 的端到端冒烟测试。

### 已知限制
- 官方 VRCFT 桥 dfgHiatus/VRCFT-Babble 已归档；以 Baballonia（2026-10 核对）为当前依据。
- 眼部 X/Y 极化由上游代码推断，未经真机确认。
- 未在真机（Babble 硬件 / Baballonia）验证，状态为未验证。
