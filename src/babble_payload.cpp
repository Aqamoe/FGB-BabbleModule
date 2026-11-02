#include "babble_payload.hpp"

#include "fgb/kit/osc.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

namespace babble {

using fgb::kit::OscArgument;
using fgb::kit::OscMessage;
using fgb::kit::OscType;

namespace {

struct DirectEntry {
    std::string_view leaf;   // Babble 地址末段（camelCase）
    std::string_view target; // Unified Expressions 目标名
};

// Kit 通用 ARKit 映射覆盖不到的 15 条通道，地址名 → 目标名逐一显式列举。
// 注意不套用归档版 VRCFT-Babble 的 ×4/×2 预放大：那是补偿 VRCFT 内部对多个
// 目标的平均合并；FGB 的 ShapeTable 按 max 合并、每个目标独立生效，原值即正确。
constexpr DirectEntry kDirectEntries[] = {
    {"cheekPuffLeft", "CheekPuffLeft"},
    {"cheekPuffRight", "CheekPuffRight"},
    {"cheekSuckLeft", "CheekSuckLeft"},
    {"cheekSuckRight", "CheekSuckRight"},
    {"tongueUp", "TongueUp"},
    {"tongueDown", "TongueDown"},
    {"tongueLeft", "TongueLeft"},
    {"tongueRight", "TongueRight"},
    {"tongueRoll", "TongueRoll"},
    {"tongueBendDown", "TongueBendDown"},
    {"tongueCurlUp", "TongueCurlUp"},
    {"tongueSquish", "TongueSquish"},
    {"tongueFlat", "TongueFlat"},
    {"tongueTwistLeft", "TongueTwistLeft"},
    {"tongueTwistRight", "TongueTwistRight"},
};

constexpr std::size_t kDirectEntryCount = sizeof(kDirectEntries) / sizeof(kDirectEntries[0]);

// 归一化末段名 → Direct 槽位；找不到返回 -1。用具名函数而不是 lambda：
// MSVC 14.36 对「捕获数组 + lambda 内下标」会触发内部编译器错误（C1001）。
std::ptrdiff_t DirectIndex(const std::string& normalized_leaf) {
    for (std::size_t i = 0; i < kDirectEntryCount; ++i) {
        if (fgb::kit::NormalizeShapeName(kDirectEntries[i].leaf) == normalized_leaf) {
            return static_cast<std::ptrdiff_t>(i);
        }
    }
    return -1;
}

enum class EyeField : std::uint8_t { X, Y, Lid, Widen, Squint };

struct EyeEntry {
    std::string_view leaf;
    std::uint8_t eye; // 0 = 左，1 = 右
    EyeField field;
};

// 眼部通道。X/Y/Lid 是 Baballonia 当前明确在发的；Widen 与 Brow（按上游接收端
// 的口径归入 squint）只在上游接收端解析、发送端已注释 —— 收到就记录，暂不参与
// 睁眼度公式（Lid 即上游 BabbleVRC 直接赋值的 openness，1 = 全开）。
constexpr EyeEntry kEyeEntries[] = {
    {"LeftEyeX", 0, EyeField::X},
    {"LeftEyeY", 0, EyeField::Y},
    {"LeftEyeLid", 0, EyeField::Lid},
    {"LeftEyeWiden", 0, EyeField::Widen},
    {"LeftEyeBrow", 0, EyeField::Squint},
    {"RightEyeX", 1, EyeField::X},
    {"RightEyeY", 1, EyeField::Y},
    {"RightEyeLid", 1, EyeField::Lid},
    {"RightEyeWiden", 1, EyeField::Widen},
    {"RightEyeBrow", 1, EyeField::Squint},
};

constexpr std::size_t kEyeEntryCount = sizeof(kEyeEntries) / sizeof(kEyeEntries[0]);

const EyeEntry* FindEyeEntry(const std::string& normalized_leaf) {
    for (std::size_t i = 0; i < kEyeEntryCount; ++i) {
        if (fgb::kit::NormalizeShapeName(kEyeEntries[i].leaf) == normalized_leaf) {
            return &kEyeEntries[i];
        }
    }
    return nullptr;
}

// X/Y 是 [-1,1] 的归一化眼位，不能走 CleanUnit（那会把负值剪成 0）。
float CleanSigned(float value) {
    if (std::isnan(value) || std::isinf(value)) {
        return 0.0f;
    }
    return std::clamp(value, -1.0f, 1.0f);
}

void ApplyEyeValue(EyeStream& stream, EyeField field, float value) {
    switch (field) {
        case EyeField::X:
            stream.x = CleanSigned(value);
            stream.has_gaze = true;
            break;
        case EyeField::Y:
            stream.y = CleanSigned(value);
            stream.has_gaze = true;
            break;
        case EyeField::Lid:
            stream.lid = fgb::kit::Clamp01(fgb::kit::CleanUnit(value));
            stream.has_lid = true;
            break;
        case EyeField::Widen:
            stream.widen = fgb::kit::CleanUnit(value);
            break;
        case EyeField::Squint:
            stream.squint = fgb::kit::CleanUnit(value);
            break;
    }
}

} // namespace

void ApplyValue(std::string_view address_leaf, float value, Packet& out) {
    // 先走 Kit 通用 ARKit 映射：30 条面部地址与 ARKit 系数同名，直接归一进表。
    if (fgb::kit::SetArkitByName(out.weights, address_leaf, value)) {
        ++out.messages_parsed;
        out.has_face = true;
        return;
    }

    const std::string key = fgb::kit::NormalizeShapeName(address_leaf);

    const std::ptrdiff_t direct = DirectIndex(key);
    if (direct >= 0) {
        float& slot_value = out.direct[static_cast<std::size_t>(direct)];
        slot_value = std::max(slot_value, fgb::kit::CleanUnit(value));
        ++out.messages_parsed;
        out.has_face = true;
        return;
    }

    const EyeEntry* eye = FindEyeEntry(key);
    if (eye != nullptr) {
        ApplyEyeValue(eye->eye == 0 ? out.left : out.right, eye->field, value);
        ++out.messages_parsed;
        out.has_eyes = true;
        return;
    }

    ++out.addresses_unknown;
}

bool ApplyDatagram(std::string_view data, Packet& packet, std::string& error) {
    const auto messages = fgb::kit::ParseOsc(data, error);
    if (!messages.has_value()) {
        return false; // 畸形包：原因由 osc.hpp 回传，调用方限频记日志
    }
    for (const OscMessage& message : *messages) {
        if (message.arguments.empty()) {
            ++packet.messages_no_value;
            continue;
        }
        const OscArgument& first = message.arguments.front();
        if (first.type != OscType::Int && first.type != OscType::Float &&
            first.type != OscType::Double) {
            // 上游只发 float；字符串/blob 等参数不是表情数据，忽略并计数。
            ++packet.messages_no_value;
            continue;
        }
        ApplyValue(message.Leaf(), static_cast<float>(first.number), packet);
    }
    return true;
}

void DeclareTargets(fgb::kit::ShapeTable& table) {
    for (std::size_t i = 0; i < kDirectEntryCount; ++i) {
        table.Declare(kDirectEntries[i].target);
    }
    fgb::kit::DeclareUnifiedTargets(table);
}

void WriteFrame(const Packet& packet, const fgb::kit::ShapeTable& table, FgbFrameSlot* slot) {
    if (packet.has_face) {
        slot->attributes |= FGB_ATTR_EXPRESSION;
        fgb::kit::MapArkitToUnified(packet.weights, table, slot);
        for (std::size_t i = 0; i < kDirectEntryCount; ++i) {
            const float value = packet.direct[i];
            if (value > 0.0f) {
                table.Write(slot, kDirectEntries[i].target, value);
            }
        }
    }

    if (packet.left.has_lid) {
        slot->attributes |= FGB_ATTR_LEFT_OPENNESS;
        slot->left_eye.openness = fgb::kit::Clamp01(packet.left.lid);
        slot->left_eye.openness_confidence = 1.0f;
    }
    if (packet.right.has_lid) {
        slot->attributes |= FGB_ATTR_RIGHT_OPENNESS;
        slot->right_eye.openness = fgb::kit::Clamp01(packet.right.lid);
        slot->right_eye.openness_confidence = 1.0f;
    }
    if (packet.left.has_gaze) {
        slot->attributes |= FGB_ATTR_LEFT_GAZE;
        // X/Y 当成投影平面分量归一成 HMD 局部单位向量（-Z 前，+X 右，+Y 上）。
        // 极化沿用上游 BabbleVRC 直接写入 VRCFT Gaze.x/y 的口径，未经真机确认。
        fgb::kit::GazeFromLookWeights(packet.left.x, packet.left.y, slot->left_eye.gaze_dir);
        slot->left_eye.gaze_confidence = 0.5f; // 摄像头估计值，置信度压低
    }
    if (packet.right.has_gaze) {
        slot->attributes |= FGB_ATTR_RIGHT_GAZE;
        fgb::kit::GazeFromLookWeights(packet.right.x, packet.right.y, slot->right_eye.gaze_dir);
        slot->right_eye.gaze_confidence = 0.5f;
    }
}

} // namespace babble
