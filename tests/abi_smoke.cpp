// 走真实 ABI 的端到端冒烟：加载 DLL → query → initialize → 喂合成 OSC/UDP → update → 检查提交帧。
//
// 它会绑定 8888 端口，运行前关掉 VRCFT 和其它 Babble 接收端。
// 因此不挂进 ctest，只由 `cmake --build ... --target smoke-test` 触发。

#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/testing/host_mock.hpp"
#include "fgb_module_api.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr std::uint16_t kPort = 8888;

// ---- 与 test_payload 相同的最小 OSC 编码（模拟 Babble「一地址一包」）----

void AppendPadded(std::string& out, std::string_view text) {
    out.append(text);
    out.push_back('\0');
    while (out.size() % 4u != 0u) {
        out.push_back('\0');
    }
}

std::string EncodeMessage(std::string_view address, float value) {
    std::string message;
    AppendPadded(message, address);
    AppendPadded(message, ",f");
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int shift = 24; shift >= 0; shift -= 8) {
        message.push_back(static_cast<char>((bits >> shift) & 0xFFu));
    }
    return message;
}

bool SendToSelf(const std::string& payload) {
    WSADATA data{};
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return false;
    }
    const SOCKET handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(kPort);
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int sent = ::sendto(handle, payload.data(), static_cast<int>(payload.size()), 0,
                              reinterpret_cast<sockaddr*>(&target), sizeof(target));
    ::closesocket(handle);
    ::WSACleanup();
    return sent != SOCKET_ERROR;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("用法：%s <babble.dll 路径>\n", argv[0]);
        return 2;
    }

    HMODULE library = ::LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    CHECK(library != nullptr);
    if (library == nullptr) {
        std::printf("LoadLibrary 失败，错误码 %lu（DLL 是否已构建？依赖是否在同目录？）\n",
                    ::GetLastError());
        return CheckSummary("babble-abi-smoke");
    }

    const auto query = reinterpret_cast<decltype(&fgb_module_query_v2)>(
        ::GetProcAddress(library, "fgb_module_query_v2"));
    CHECK(query != nullptr);

    fgb::kit::testing::HostMock host;
    FgbInitParams params = host.MakeParams();

    const FgbModule* module = query(FGB_MODULE_ABI_V2);
    CHECK(module != nullptr);
    if (module == nullptr) {
        ::FreeLibrary(library);
        return CheckSummary("babble-abi-smoke");
    }
    CHECK(module->id == std::string{"com.fgbbridge.babble"});
    CHECK(module->display_name == std::string{"Project Babble"});
    CHECK_EQ(module->abi_version, FGB_MODULE_ABI_V2);
    CHECK_EQ(module->capabilities,
             static_cast<std::uint32_t>(FGB_CAP_EXPRESSION | FGB_CAP_EYES));
    CHECK(query(FGB_MODULE_ABI_V2 + 1) == nullptr);  // ABI 不匹配必须拒绝

    void* self = nullptr;
    CHECK_EQ(module->initialize(&self, host.services(), &params), 1);
    if (self == nullptr) {
        std::printf("initialize 失败：%s\n", host.LastStatusDetail().c_str());
        ::FreeLibrary(library);
        return CheckSummary("babble-abi-smoke");
    }

    // 没有数据时不应该提交帧
    module->update(self);
    CHECK(host.committed().empty());

    // Babble 的高频写法：每条地址单独一包，这里连发一轮面部 + 眼部
    const std::string payloads[] = {
        EncodeMessage("/jawOpen", 0.62f),
        EncodeMessage("/mouthSmileLeft", 0.4f),
        EncodeMessage("/cheekPuffLeft", 0.3f),
        EncodeMessage("/tongueUp", 0.5f),
        EncodeMessage("/LeftEyeLid", 0.9f),
        EncodeMessage("/LeftEyeX", 0.2f),
    };
    bool sent_all = true;
    for (const auto& payload : payloads) {
        sent_all = sent_all && SendToSelf(payload);
    }
    CHECK(sent_all);

    // 等到某一帧同时看到表情、Direct 通道与眼睛为止（各包到达顺序不保证）。
    bool complete = false;
    for (int attempt = 0; attempt < 400 && !complete; ++attempt) {
        module->update(self);
        if (!host.committed().empty()) {
            const FgbFrameSlot& slot = host.committed().back();
            complete = (slot.attributes & FGB_ATTR_LEFT_OPENNESS) != 0 &&
                       slot.shape_valid[host.IndexOf("CheekPuffLeft")] != 0 &&
                       slot.shape_valid[host.IndexOf("TongueUp")] != 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(!host.committed().empty());
    CHECK(complete);

    if (!host.committed().empty()) {
        const FgbFrameSlot& slot = host.committed().back();
        CHECK((slot.attributes & FGB_ATTR_EXPRESSION) != 0);
        CHECK_NEAR(slot.shapes[host.IndexOf("JawOpen")], 0.62, 1e-5);
        CHECK_NEAR(slot.shapes[host.IndexOf("MouthCornerPullLeft")], 0.4, 1e-5);
        CHECK_NEAR(slot.shapes[host.IndexOf("CheekPuffLeft")], 0.3, 1e-5);
        CHECK_NEAR(slot.shapes[host.IndexOf("TongueUp")], 0.5, 1e-5);
        CHECK_NEAR(slot.left_eye.openness, 0.9, 1e-5);
        CHECK(slot.left_eye.gaze_dir[2] < 0.0f);
        CHECK(host.HasStatus(FGB_STATUS_STARTING) || host.HasStatus(FGB_STATUS_ACTIVE));
    }

    module->teardown(self);
    ::FreeLibrary(library);
    return CheckSummary("babble-abi-smoke");
}
