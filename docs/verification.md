# 真机验收清单

> 当前状态：**未验证**。下列任一项完成后，请在 issue 里附设备与版本信息。

## 环境

| 项 | 值 |
|---|---|
| 来源应用 | Baballonia（记录版本号）或 Project Babble |
| 硬件 | Babble 官方面捕（ESP32-S3）/ Baballonia 支持的其它设备 |
| 系统 | Windows 10 / 11 x64 |
| 宿主 | FaceGaze Bridge（记录版本号） |

## 步骤

- [ ] Baballonia 输出页启用 VRCFT/OSC 输出，地址 `127.0.0.1`、端口 `8888`
- [ ] PC 上关闭 VRCFT 及其它占用 8888 的程序
- [ ] FGB「模块库 → 原生模块」安装 `babble-<版本>.zip`
- [ ] 「模块」页把 Project Babble 设为启动，重启引擎
- [ ] 模块卡片显示活跃，状态页有表情输入

## 观察项

- [ ] 张嘴 → `JawOpen`
- [ ] 微笑 → `MouthCornerPullLeft/Right`
- [ ] 鼓腮 → `CheekPuffLeft/Right`（Babble 特有左右独立通道）
- [ ] 伸舌头 / 卷舌 → `TongueOut`、`TongueRoll` 等
- [ ] 眨眼 → 睁眼度下降，左右眼独立（`/LeftEyeLid`、`/RightEyeLid`）
- [ ] **眼看左 / 右 / 上 / 下 → 注视方向极化正确**（本项是 docs/protocol.md
      中标注「未经真机确认」的重点，出现镜像请截图反馈）
- [ ] 停止发送 / 拔设备 → 10 秒内卡片给出「已 N 秒没有数据」，恢复后回到活跃
- [ ] 在 Baballonia 设置里启用 OSC 前缀 → 表情仍应正常（末段匹配）

## 与上游对比（可选但有用）

同一设备上并用 VRCFT + 归档版 VRCFT-Babble 模块，对比同一组表情的参数曲线。
注意归档版对 `/mouthFunnel` `/mouthPucker` 有 ×4、`/mouthLeft` `/mouthRight` 有 ×2
的补偿（VRCFT 内部平均合并所致），数值不匹配是预期行为，形状「是否随动」才是关键。
