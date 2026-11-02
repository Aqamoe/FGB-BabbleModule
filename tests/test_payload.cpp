#include "babble_payload.hpp"

#include "fgb/kit/module.hpp"
#include "fgb/kit/osc.hpp"
#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/testing/host_mock.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace fgb::kit;
using babble::ApplyDatagram;
using babble::ApplyValue;
using babble::Direct;
using babble::Packet;

namespace {

constexpr std::uint16_t Slot(Arkit name) {
    return static_cast<std::uint16_t>(static_cast<std::size_t>(name));
}

// ---- OSC 编码辅助（仅测试用）----

void AppendPadded(std::string& out, std::string_view text) {
    out.append(text);
    out.push_back('\0');
    while (out.size() % 4u != 0u) {
        out.push_back('\0');
    }
}

void AppendFloatBE(std::string& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<char>((bits >> shift) & 0xFFu));
    }
}

void AppendInt32BE(std::string& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xFFu));
    }
}

std::string EncodeMessage(std::string_view address, float value) {
    std::string message;
    AppendPadded(message, address);
    AppendPadded(message, ",f");
    AppendFloatBE(message, value);
    return message;
}

std::string EncodeStringMessage(std::string_view address, std::string_view text) {
    std::string message;
    AppendPadded(message, address);
    AppendPadded(message, ",s");
    AppendPadded(message, text);
    return message;
}

std::string EncodeBundle(const std::vector<std::string>& elements) {
    std::string bundle{"#bundle"};
    bundle.push_back('\0'); // 标记含结尾 NUL 共 8 字节
    AppendInt32BE(bundle, 0);
    AppendInt32BE(bundle, 1); // 时间戳只要结构合法即可，解析器不消费它
    for (const auto& element : elements) {
        AppendInt32BE(bundle, static_cast<std::uint32_t>(element.size()));
        bundle.append(element);
    }
    return bundle;
}

FgbFrameSlot FreshSlot() {
    FgbFrameSlot slot{};
    slot.struct_size = sizeof(FgbFrameSlot);
    slot.abi_version = FGB_MODULE_ABI_V2;
    slot.shape_count = FGB_MAX_SHAPES;
    return slot;
}

float Nan() {
    return std::numeric_limits<float>::quiet_NaN();
}

// ---- 全地址逐项断言表 ----

enum Kind : std::uint8_t { KArkit, KDirect, KEye };

struct Row {
    const char* address;
    Kind kind;
    std::uint16_t slot; // KArkit→Arkit 枚举；KDirect→Direct 枚举；KEye→眼*8+字段
};

constexpr int EyeSlot(int eye, int field) { return eye * 8 + field; }
constexpr int Fx = 0, Fy = 1, Flid = 2, Fwiden = 3, Fsquint = 4;

// 45 条面部地址 + 10 条眼部地址，逐条与 docs/protocol.md 的清单对应。
const Row kAddressTable[] = {
    {"/jawOpen", KArkit, Slot(Arkit::JawOpen)},
    {"/jawForward", KArkit, Slot(Arkit::JawForward)},
    {"/jawLeft", KArkit, Slot(Arkit::JawLeft)},
    {"/jawRight", KArkit, Slot(Arkit::JawRight)},
    {"/noseSneerLeft", KArkit, Slot(Arkit::NoseSneerLeft)},
    {"/noseSneerRight", KArkit, Slot(Arkit::NoseSneerRight)},
    {"/mouthFunnel", KArkit, Slot(Arkit::MouthFunnel)},
    {"/mouthPucker", KArkit, Slot(Arkit::MouthPucker)},
    {"/mouthLeft", KArkit, Slot(Arkit::MouthLeft)},
    {"/mouthRight", KArkit, Slot(Arkit::MouthRight)},
    {"/mouthRollUpper", KArkit, Slot(Arkit::MouthRollUpper)},
    {"/mouthRollLower", KArkit, Slot(Arkit::MouthRollLower)},
    {"/mouthShrugUpper", KArkit, Slot(Arkit::MouthShrugUpper)},
    {"/mouthShrugLower", KArkit, Slot(Arkit::MouthShrugLower)},
    {"/mouthClose", KArkit, Slot(Arkit::MouthClose)},
    {"/mouthSmileLeft", KArkit, Slot(Arkit::MouthSmileLeft)},
    {"/mouthSmileRight", KArkit, Slot(Arkit::MouthSmileRight)},
    {"/mouthFrownLeft", KArkit, Slot(Arkit::MouthFrownLeft)},
    {"/mouthFrownRight", KArkit, Slot(Arkit::MouthFrownRight)},
    {"/mouthDimpleLeft", KArkit, Slot(Arkit::MouthDimpleLeft)},
    {"/mouthDimpleRight", KArkit, Slot(Arkit::MouthDimpleRight)},
    {"/mouthUpperUpLeft", KArkit, Slot(Arkit::MouthUpperUpLeft)},
    {"/mouthUpperUpRight", KArkit, Slot(Arkit::MouthUpperUpRight)},
    {"/mouthLowerDownLeft", KArkit, Slot(Arkit::MouthLowerDownLeft)},
    {"/mouthLowerDownRight", KArkit, Slot(Arkit::MouthLowerDownRight)},
    {"/mouthPressLeft", KArkit, Slot(Arkit::MouthPressLeft)},
    {"/mouthPressRight", KArkit, Slot(Arkit::MouthPressRight)},
    {"/mouthStretchLeft", KArkit, Slot(Arkit::MouthStretchLeft)},
    {"/mouthStretchRight", KArkit, Slot(Arkit::MouthStretchRight)},
    {"/tongueOut", KArkit, Slot(Arkit::TongueOut)},
    {"/cheekPuffLeft", KDirect, static_cast<std::uint16_t>(Direct::CheekPuffLeft)},
    {"/cheekPuffRight", KDirect, static_cast<std::uint16_t>(Direct::CheekPuffRight)},
    {"/cheekSuckLeft", KDirect, static_cast<std::uint16_t>(Direct::CheekSuckLeft)},
    {"/cheekSuckRight", KDirect, static_cast<std::uint16_t>(Direct::CheekSuckRight)},
    {"/tongueUp", KDirect, static_cast<std::uint16_t>(Direct::TongueUp)},
    {"/tongueDown", KDirect, static_cast<std::uint16_t>(Direct::TongueDown)},
    {"/tongueLeft", KDirect, static_cast<std::uint16_t>(Direct::TongueLeft)},
    {"/tongueRight", KDirect, static_cast<std::uint16_t>(Direct::TongueRight)},
    {"/tongueRoll", KDirect, static_cast<std::uint16_t>(Direct::TongueRoll)},
    {"/tongueBendDown", KDirect, static_cast<std::uint16_t>(Direct::TongueBendDown)},
    {"/tongueCurlUp", KDirect, static_cast<std::uint16_t>(Direct::TongueCurlUp)},
    {"/tongueSquish", KDirect, static_cast<std::uint16_t>(Direct::TongueSquish)},
    {"/tongueFlat", KDirect, static_cast<std::uint16_t>(Direct::TongueFlat)},
    {"/tongueTwistLeft", KDirect, static_cast<std::uint16_t>(Direct::TongueTwistLeft)},
    {"/tongueTwistRight", KDirect, static_cast<std::uint16_t>(Direct::TongueTwistRight)},
    {"/LeftEyeX", KEye, static_cast<std::uint16_t>(EyeSlot(0, Fx))},
    {"/LeftEyeY", KEye, static_cast<std::uint16_t>(EyeSlot(0, Fy))},
    {"/LeftEyeLid", KEye, static_cast<std::uint16_t>(EyeSlot(0, Flid))},
    {"/LeftEyeWiden", KEye, static_cast<std::uint16_t>(EyeSlot(0, Fwiden))},
    {"/LeftEyeBrow", KEye, static_cast<std::uint16_t>(EyeSlot(0, Fsquint))},
    {"/RightEyeX", KEye, static_cast<std::uint16_t>(EyeSlot(1, Fx))},
    {"/RightEyeY", KEye, static_cast<std::uint16_t>(EyeSlot(1, Fy))},
    {"/RightEyeLid", KEye, static_cast<std::uint16_t>(EyeSlot(1, Flid))},
    {"/RightEyeWiden", KEye, static_cast<std::uint16_t>(EyeSlot(1, Fwiden))},
    {"/RightEyeBrow", KEye, static_cast<std::uint16_t>(EyeSlot(1, Fsquint))},
};

void CheckRow(const char* where, Row row, float value, const Packet& packet) {
    switch (row.kind) {
        case KArkit:
            CHECK_NEAR(packet.weights[row.slot], value, 1e-6);
            if (packet.weights[row.slot] != value) {
                std::printf("FAIL %s %s -> ARKit 槽 %u\n", where, row.address, row.slot);
            }
            break;
        case KDirect:
            CHECK_NEAR(packet.direct[row.slot], value, 1e-6);
            if (packet.direct[row.slot] != value) {
                std::printf("FAIL %s %s -> Direct 槽 %u\n", where, row.address, row.slot);
            }
            break;
        case KEye: {
            const babble::EyeStream& eye = row.slot < 8 ? packet.left : packet.right;
            const int field = row.slot % 8;
            CHECK_NEAR(field == Fx ? eye.x : field == Fy ? eye.y
                                 : field == Flid  ? eye.lid
                                 : field == Fwiden ? eye.widen
                                                   : eye.squint,
                       value, 1e-6);
            if (field == Flid) {
                CHECK(eye.has_lid);
            } else if (field == Fx || field == Fy) {
                CHECK(eye.has_gaze);
            }
            break;
        }
    }
}

constexpr std::size_t AddressTableSize() {
    return sizeof(kAddressTable) / sizeof(kAddressTable[0]);
}

} // namespace

int main() {
    // ---- 单消息 ----
    {
        Packet packet;
        std::string error;
        CHECK(ApplyDatagram(EncodeMessage("/jawOpen", 0.62f), packet, error));
        CHECK_EQ(packet.messages_parsed, static_cast<std::size_t>(1));
        CHECK_EQ(packet.addresses_unknown, static_cast<std::size_t>(0));
        CHECK(packet.has_face);
        CHECK_NEAR(packet.weights[Slot(Arkit::JawOpen)], 0.62, 1e-6);
    }

    // ---- #bundle 多条 ----
    {
        Packet packet;
        std::string error;
        const std::string bundle = EncodeBundle({
            EncodeMessage("/jawOpen", 0.5f),
            EncodeMessage("/mouthSmileLeft", 0.4f),
            EncodeMessage("/cheekPuffLeft", 0.3f),
        });
        CHECK(ApplyDatagram(bundle, packet, error));
        CHECK_EQ(packet.messages_parsed, static_cast<std::size_t>(3));
        CHECK_NEAR(packet.weights[Slot(Arkit::JawOpen)], 0.5, 1e-6);
        CHECK_NEAR(packet.weights[Slot(Arkit::MouthSmileLeft)], 0.4, 1e-6);
        CHECK_NEAR(packet.direct[static_cast<std::size_t>(Direct::CheekPuffLeft)], 0.3, 1e-6);
    }

    // ---- 自定义前缀（Baballonia 的 OSCPrefix）按末段匹配 ----
    {
        Packet packet;
        std::string error;
        CHECK(ApplyDatagram(EncodeMessage("/avatar/parameters/jawOpen", 0.2f), packet, error));
        CHECK_EQ(packet.messages_parsed, static_cast<std::size_t>(1));
        CHECK_NEAR(packet.weights[Slot(Arkit::JawOpen)], 0.2, 1e-6);
    }

    // ---- 全地址逐项断言：55 条，逐条独立一包 ----
    for (std::size_t i = 0; i < AddressTableSize(); ++i) {
        const Row& row = kAddressTable[i];
        Packet packet;
        std::string error;
        CHECK(ApplyDatagram(EncodeMessage(row.address, 0.35f), packet, error));
        CHECK_EQ(packet.messages_parsed, static_cast<std::size_t>(1));
        CHECK_EQ(packet.addresses_unknown, static_cast<std::size_t>(0));
        CheckRow("全地址表", row, 0.35f, packet);
    }

    // ---- 未知地址：忽略并计数 ----
    {
        Packet packet;
        std::string error;
        CHECK(ApplyDatagram(EncodeMessage("/headBob", 0.5f), packet, error));
        CHECK(ApplyDatagram(EncodeMessage("/tracking/eye/EyesClosedAmount", 0.5f), packet, error));
        CHECK(ApplyDatagram(EncodeMessage("/chatterbox", 0.5f), packet, error));
        CHECK_EQ(packet.messages_parsed, static_cast<std::size_t>(0));
        CHECK_EQ(packet.addresses_unknown, static_cast<std::size_t>(3));
        CHECK(!packet.has_face);
        CHECK(!packet.has_eyes);
    }

    // ---- 畸形包与缺参数：必须拒绝或计数，不能污染状态 ----
    {
        Packet packet;
        std::string error;
        CHECK(!ApplyDatagram("abc", packet, error));                       // 过短
        CHECK(!ApplyDatagram("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", packet, error)); // 非法标记/未对齐
        const std::string truncated = EncodeMessage("/jawOpen", 0.5f);
        CHECK(!ApplyDatagram(truncated.substr(0, truncated.size() - 4), packet, error)); // 缺 float 载荷
        // 结构合法但参数是字符串：接受这一包、忽略这条消息（与上游「只认 float」一致）
        CHECK(ApplyDatagram(EncodeStringMessage("/jawOpen", "quack"), packet, error));
        CHECK_EQ(packet.messages_parsed, static_cast<std::size_t>(0));
        CHECK_EQ(packet.messages_no_value, static_cast<std::size_t>(1));
        CHECK_EQ(packet.weights[Slot(Arkit::JawOpen)], 0.0); // 未被污染

        Packet empty_args;
        std::string no_arg;
        AppendPadded(no_arg, "/jawOpen");
        AppendPadded(no_arg, ","); // 地址合法、类型标记为空：视为无参数消息
        CHECK(ApplyDatagram(no_arg, empty_args, error));
        CHECK_EQ(empty_args.messages_no_value, static_cast<std::size_t>(1));
        CHECK_EQ(empty_args.messages_parsed, static_cast<std::size_t>(0));
    }

    // ---- NaN / 越界清洗 ----
    {
        Packet packet;
        std::string error;
        CHECK(ApplyDatagram(EncodeMessage("/jawOpen", Nan()), packet, error));
        CHECK_NEAR(packet.weights[Slot(Arkit::JawOpen)], 0.0, 1e-9);
        CHECK_EQ(packet.messages_parsed, static_cast<std::size_t>(1)); // 计入但值被清洗
        CHECK(ApplyDatagram(EncodeMessage("/mouthPucker", 7.0f), packet, error));
        CHECK_NEAR(packet.weights[Slot(Arkit::MouthPucker)], 1.0, 1e-6);
        CHECK(ApplyDatagram(EncodeMessage("/LeftEyeX", Nan()), packet, error));
        CHECK_NEAR(packet.left.x, 0.0, 1e-9);
        CHECK(packet.left.has_gaze);
        CHECK(ApplyDatagram(EncodeMessage("/LeftEyeX", -2.0f), packet, error));
        CHECK_NEAR(packet.left.x, -1.0, 1e-6); // 眼位裁剪到 [-1,1]，不能剪成 0
    }

    // ---- 帧写入：通用映射 + Direct 通道 + 双眼属性 ----
    testing::HostMock host;
    Host logger{host.services()};
    ShapeTable table;
    babble::DeclareTargets(table);
    CHECK(table.Build(logger));
    CHECK_EQ(table.ResolvedCount(), table.Size()); // 目标名必须都是真实形状

    {
        Packet packet;
        std::string error;
        const std::string bundle = EncodeBundle({
            EncodeMessage("/jawOpen", 0.62f),
            EncodeMessage("/mouthSmileLeft", 0.4f),
            EncodeMessage("/mouthFunnel", 0.2f),
            EncodeMessage("/cheekPuffLeft", 0.3f),
            EncodeMessage("/cheekSuckRight", 0.25f),
            EncodeMessage("/tongueUp", 0.5f),
            EncodeMessage("/LeftEyeLid", 0.9f),
            EncodeMessage("/LeftEyeX", 0.2f),
            EncodeMessage("/LeftEyeY", -0.1f),
            EncodeMessage("/RightEyeLid", 0.8f),
        });
        CHECK(ApplyDatagram(bundle, packet, error));

        FgbFrameSlot slot = FreshSlot();
        babble::WriteFrame(packet, table, &slot);

        CHECK((slot.attributes & FGB_ATTR_EXPRESSION) != 0);
        CHECK((slot.attributes & FGB_ATTR_LEFT_OPENNESS) != 0);
        CHECK((slot.attributes & FGB_ATTR_RIGHT_OPENNESS) != 0);
        CHECK((slot.attributes & FGB_ATTR_LEFT_GAZE) != 0);
        CHECK((slot.attributes & FGB_ATTR_RIGHT_GAZE) == 0); // 右眼只有 Lid，没发方向

        CHECK_NEAR(slot.shapes[host.IndexOf("JawOpen")], 0.62, 1e-6);
        CHECK_NEAR(slot.shapes[host.IndexOf("MouthCornerPullLeft")], 0.4, 1e-6);
        // 通用映射：mouthFunnel 一份值喂四个唇形目标，不做归档版 VRCFT 的 ×4
        CHECK_NEAR(slot.shapes[host.IndexOf("LipFunnelUpperLeft")], 0.2, 1e-6);
        CHECK_NEAR(slot.shapes[host.IndexOf("LipFunnelLowerRight")], 0.2, 1e-6);
        // Direct 通道逐项落位
        CHECK_NEAR(slot.shapes[host.IndexOf("CheekPuffLeft")], 0.3, 1e-6);
        CHECK_NEAR(slot.shapes[host.IndexOf("CheekSuckRight")], 0.25, 1e-6);
        CHECK_NEAR(slot.shapes[host.IndexOf("TongueUp")], 0.5, 1e-6);
        CHECK(slot.shape_valid[host.IndexOf("CheekPuffRight")] == 0);

        // 睁眼度 = Lid 直通（上游 BabbleVRC 语义：1 = 全开）
        CHECK_NEAR(slot.left_eye.openness, 0.9, 1e-6);
        CHECK_NEAR(slot.right_eye.openness, 0.8, 1e-6);
        CHECK_NEAR(slot.left_eye.gaze_confidence, 0.5, 1e-6);

        const float length = std::sqrt(slot.left_eye.gaze_dir[0] * slot.left_eye.gaze_dir[0] +
                                       slot.left_eye.gaze_dir[1] * slot.left_eye.gaze_dir[1] +
                                       slot.left_eye.gaze_dir[2] * slot.left_eye.gaze_dir[2]);
        CHECK_NEAR(length, 1.0, 1e-5);
        CHECK(slot.left_eye.gaze_dir[2] < 0.0f); // -Z 前
        CHECK(slot.left_eye.gaze_dir[0] > 0.0f); // X 正 = 向右
        CHECK(slot.left_eye.gaze_dir[1] < 0.0f); // Y 负 = 向下
    }

    // ---- 只有表情时不应虚报眼睛属性；ARKit 眼动名 Babble 不发，来了也不写注视 ----
    {
        Packet packet;
        ApplyValue("jawOpen", 0.3f, packet);          // 末段名，不带前导斜杠
        ApplyValue("eyeLookInLeft", 0.5f, packet);    // 通用表认识这个名字…
        FgbFrameSlot slot = FreshSlot();
        babble::WriteFrame(packet, table, &slot);
        CHECK((slot.attributes & FGB_ATTR_EXPRESSION) != 0);
        CHECK_EQ(packet.messages_parsed, static_cast<std::size_t>(2));
        CHECK_NEAR(packet.weights[Slot(Arkit::EyeLookInLeft)], 0.5, 1e-6);
        // …但 Babble 的注视只来自 Eye 通道，这里一个眼睛属性都不该置位
        CHECK((slot.attributes & (FGB_ATTR_LEFT_OPENNESS | FGB_ATTR_LEFT_GAZE |
                                  FGB_ATTR_RIGHT_OPENNESS | FGB_ATTR_RIGHT_GAZE)) == 0);
        CHECK(slot.shape_valid[host.IndexOf("EyeSquintLeft")] == 0);
    }

    // ---- 断流判定 ----
    {
        testing::HostMock stream_host;
        Host log{stream_host.services()};
        Staleness staleness{10ull * 1000ull * 1000ull * 1000ull, "Project Babble"};
        const std::uint64_t base = 1'000'000'000ull;

        CHECK(!staleness.Check(log, 0, base)); // 一个包都没收到
        CHECK(stream_host.HasStatus(FGB_STATUS_ERROR));
        CHECK(!staleness.Check(log, 0, base + 100)); // 只报一次，不刷屏
        CHECK_EQ(stream_host.CountStatuses(FGB_STATUS_ERROR), static_cast<std::size_t>(1));

        CHECK(staleness.Check(log, base + 200, base + 200)); // 数据到了
        CHECK(staleness.Check(log, base + 200, base + 250)); // 恢复后回到活跃
        CHECK(stream_host.HasStatus(FGB_STATUS_ACTIVE));

        CHECK(!staleness.Check(log, base + 200, base + 200 + 11ull * 1000ull * 1000ull * 1000ull));
        CHECK_EQ(stream_host.CountStatuses(FGB_STATUS_ERROR), static_cast<std::size_t>(2));
        CHECK(stream_host.LastStatusDetail().find("秒没有数据") != std::string::npos);
    }

    return CheckSummary("babble-payload");
}
