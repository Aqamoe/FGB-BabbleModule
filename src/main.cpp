// Project Babble / Baballonia → FaceGaze Bridge 原生模块。
//
// 监听 Babble 系应用按 VRCFT 兼容模式发到本机 8888 端口的 OSC over UDP，
// 把 45 条面部地址（ARKit 同名 + Babble 特有）映射成 Unified Expressions，
// 并消费 /LeftEyeX /LeftEyeY /LeftEyeLid 等眼部通道。
// 不需要 VRCFT，不注入任何进程，也不需要管理员权限。

#include "babble_payload.hpp"

#include "fgb/kit/config.hpp"
#include "fgb/kit/module.hpp"

#include <cstdint>
#include <string>

namespace {

constexpr std::uint16_t kDefaultPort = 8888;
// 与上游 VRCFT 模块一致：10 秒没数据判定断流。
constexpr std::uint64_t kNoDataTimeoutNs = 10ull * 1000ull * 1000ull * 1000ull;
constexpr std::uint64_t kWarnIntervalNs = 5ull * 1000ull * 1000ull * 1000ull;

class BabbleSource : public fgb::kit::IModuleSource {
public:
    bool Start(fgb::kit::Host& host, const FgbInitParams& params, std::string& error) override {
        fgb::kit::ConfigIni config;
        std::string config_error;
        const std::string directory = params.module_dir != nullptr ? params.module_dir : "";
        if (config.Load(directory, "config.ini", config_error)) {
            port_ = static_cast<std::uint16_t>(config.GetInt("port", port_));
            if (!config_error.empty()) {
                host.Warn("config.ini：" + config_error);
            }
            if (!config.UnknownKeys().empty()) {
                host.Warn("config.ini 有未识别的键：" + config.UnknownKeys()[0]);
            }
        }

        fgb::kit::ShapeTable table;
        babble::DeclareTargets(table);
        if (!table.Build(host)) {
            error = "宿主没有提供 shape_index，无法映射 Unified Expressions";
            return false;
        }
        if (table.ResolvedCount() < table.Size()) {
            host.Warn("部分 Unified Expressions 形状未被宿主识别，已跳过 " +
                      std::to_string(table.Size() - table.ResolvedCount()) + " 个");
        }
        table_ = std::move(table);

        // Babble 按「一条地址一个数据报」高频发送，队列放宽，积压时丢旧包。
        if (!endpoint_.Bind(port_, error, 512)) {
            return false;
        }
        host.Info("Project Babble 监听 UDP " + std::to_string(port_) + "，等待 Babble 端连接");
        host.StatusStarting("等待 Project Babble 数据");
        return true;
    }

    bool Read(fgb::kit::Host& host, FgbFrameSlot& slot, std::uint64_t now_ns) override {
        babble::Packet packet;
        bool parsed = false;

        // 一个 tick 内可能积压几十包「单地址」数据报，必须全部合并；
        // 只取最新一包会把其它 50 多个通道当成 0。
        fgb::kit::UdpPacket datagram;
        while (endpoint_.Poll(datagram)) {
            const std::string_view text{reinterpret_cast<const char*>(datagram.data.data()),
                                        datagram.data.size()};
            std::string error;
            if (babble::ApplyDatagram(text, packet, error)) {
                parsed = packet.messages_parsed > 0;
            } else {
                host.WarnThrottled(warn_ns_, now_ns, kWarnIntervalNs, "丢弃一包：" + error);
            }
            if (packet.addresses_unknown > 0 && packet.addresses_unknown != reported_unknown_) {
                reported_unknown_ = packet.addresses_unknown;
                host.Warn("Project Babble 上报了 " + std::to_string(packet.addresses_unknown) +
                          " 个无法识别的地址（模块会忽略它们）");
            }
        }

        if (!staleness_.Check(host, endpoint_.LastPacketNs(), now_ns) || !parsed) {
            return false;
        }

        babble::WriteFrame(packet, table_, &slot);
        return true;
    }

    void Stop(fgb::kit::Host& host) override {
        endpoint_.Close();
        host.StatusStopped("Project Babble 已停止");
    }

private:
    fgb::kit::UdpEndpoint endpoint_;
    fgb::kit::ShapeTable table_;
    fgb::kit::Staleness staleness_{kNoDataTimeoutNs, "Project Babble"};
    std::uint16_t port_ = kDefaultPort;
    std::uint64_t warn_ns_ = 0;
    std::size_t reported_unknown_ = 0;
};

} // namespace

FGB_KIT_EXPORT(BabbleSource, "com.fgbbridge.babble", "Project Babble",
               FGB_CAP_EXPRESSION | FGB_CAP_EYES)
