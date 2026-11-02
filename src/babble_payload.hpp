#pragma once
// Project Babble / Baballonia 的 OSC over UDP 负载解析与帧写入。
//
// 上游格式（Baballonia 的 VRCFT 输出，默认 127.0.0.1:8888，float 单参数）：
//   - 45 条 camelCase 面部地址：/jawOpen、/cheekPuffLeft、/tongueUp……
//   - 眼部地址（PascalCase）：/LeftEyeX /LeftEyeY /LeftEyeLid 与 Right 对应项；
//     /LeftEyeWiden /LeftEyeBrow 等在上游接收端仍解析、但当前发送端已注释。
//   - 一个数据报通常只含一条消息（上游逐条 send）；本模块同时接受 #bundle。
//   - 上游支持自定义前缀（如 /avatar/parameters/jawOpen），按地址末段匹配。

#include "fgb/kit/arkit.hpp"
#include "fgb/kit/host.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace babble {

// 通用 ARKit 映射覆盖不到、但与 Unified Expressions 同名对应的 Babble 特有通道。
enum class Direct : std::uint8_t {
    CheekPuffLeft,
    CheekPuffRight,
    CheekSuckLeft,
    CheekSuckRight,
    TongueUp,
    TongueDown,
    TongueLeft,
    TongueRight,
    TongueRoll,
    TongueBendDown,
    TongueCurlUp,
    TongueSquish,
    TongueFlat,
    TongueTwistLeft,
    TongueTwistRight,
    Count,
};

inline constexpr std::size_t kDirectCount = static_cast<std::size_t>(Direct::Count);

struct EyeStream {
    // X/Y 是归一化眼位（约 [-1,1]），Lid 是睁眼度（1 = 全开，对齐上游 BabbleVRC 的直接赋值）。
    float x = 0.0f;
    float y = 0.0f;
    float lid = 0.0f;
    float widen = 0.0f;  // 上游当前不发，收到就记录
    float squint = 0.0f; // /LeftEyeBrow 归到这里，语义未完全确认
    bool has_lid = false;
    bool has_gaze = false;
};

struct Packet {
    fgb::kit::ArkitWeights weights{};             // 30 条 ARKit 同名地址
    std::array<float, kDirectCount> direct{};     // 15 条 Babble 特有地址
    EyeStream left{};
    EyeStream right{};
    std::size_t messages_parsed = 0;   // 认出来的消息数
    std::size_t addresses_unknown = 0; // 认不出的地址数（忽略并计数）
    std::size_t messages_no_value = 0; // 缺数值参数或参数类型不对
    bool has_face = false;
    bool has_eyes = false;
};

// 解析一个 UDP 数据报（单消息或 #bundle）并把结果累积进 packet。
// 返回 false 表示结构非法（调用方整包丢弃），error 带上游解析原因。
bool ApplyDatagram(std::string_view data, Packet& packet, std::string& error);

// 单条地址应用，测试逐条构造时也用它。
void ApplyValue(std::string_view address_leaf, float value, Packet& out);

// 登记本模块会写的全部 Unified Expressions 目标名。
void DeclareTargets(fgb::kit::ShapeTable& table);

// 写表情 + 舌头 + 双眼属性，并置上对应的 attributes 位。
void WriteFrame(const Packet& packet, const fgb::kit::ShapeTable& table, FgbFrameSlot* slot);

} // namespace babble
