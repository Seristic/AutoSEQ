#include "StalledQuests.h"
#include "SEQAudit.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace {
    std::optional<std::filesystem::path> LogDirectory() {
        const auto path = SKSE::log::log_directory();
        if (!path) {
            return std::nullopt;
        }
        std::error_code ec;
        const char* folder = std::filesystem::exists("Galaxy64.dll", ec)                ? "Skyrim Special Edition GOG"
                             : std::filesystem::exists("EOSSDK-Win64-Shipping.dll", ec) ? "Skyrim Special Edition EPIC"
                                                                                         : "Skyrim Special Edition";
        return path->parent_path().parent_path() / folder / "SKSE";
    }

    void SetupLog() {
        auto path = LogDirectory();
        if (!path) {
            return;
        }
        *path /= std::format("{}.log", SKSE::PluginDeclaration::GetSingleton()->GetName());

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);
    SetupLog();
    SEQ::Audit::ApplyPendingArchive();

    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
        switch (a_msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                SEQ::Audit::Run();
                SEQ::StalledQuests::GetSingleton()->Register();
                break;
            case SKSE::MessagingInterface::kNewGame:
                SEQ::StalledQuests::GetSingleton()->OnNewGame();
                break;
            case SKSE::MessagingInterface::kPostLoadGame:
                SEQ::StalledQuests::GetSingleton()->OnPostLoadGame();
                break;
            default:
                break;
        }
    });

    return true;
}
