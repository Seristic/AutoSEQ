#include "StalledQuests.h"

namespace SEQ {

    void StalledQuests::Register() {
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
        }
    }

    void StalledQuests::OnNewGame() {
        // kNewGame fires before the engine starts its own SGE quests, so wait until the player is actually in the world.
        m_scan_pending = true;
    }

    void StalledQuests::OnPostLoadGame() {
        auto* ui = RE::UI::GetSingleton();
        if (ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
            m_scan_pending = true;
        } else {
            RunScan();
        }
    }

    RE::BSEventNotifyControl StalledQuests::ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                                                     RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
        if (a_event && !a_event->opening && a_event->menuName == RE::LoadingMenu::MENU_NAME &&
            m_scan_pending.exchange(false)) {
            SKSE::GetTaskInterface()->AddTask([this] { RunScan(); });
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    void StalledQuests::RunScan() {
        auto* handler = RE::TESDataHandler::GetSingleton();
        if (!handler) {
            LOG_ERROR("AutoSEQ: TESDataHandler unavailable, aborting scan.");
            return;
        }

        const auto& quests = handler->GetFormArray<RE::TESQuest>();
        ++m_scan_count;

        std::uint32_t eligible = 0;
        std::uint32_t started = 0;

        for (auto* quest : quests) {
            if (!quest || !quest->StartsEnabled() || quest->eventID != RE::QuestEvent::kNone) {
                continue;
            }
            ++eligible;

            if (quest->IsEnabled() || quest->promoteTask) {
                continue;
            }

            // Only rescue quests that never got started. SGE quests that already ran and were stopped (init/installer quests, finished quests) must stay stopped.
            if (quest->alreadyRun || quest->IsCompleted() || quest->data.flags.any(RE::QuestFlag::kFailed) ||
                quest->GetCurrentStageID() != 0) {
                continue;
            }

            if (quest->Start()) {
                ++started;
                LOG_INFO("AutoSEQ: started stalled quest [{:08X}] {}", quest->GetFormID(),
                         quest->GetFormEditorID());
            } else {
                LOG_WARN("AutoSEQ: failed to start quest [{:08X}] {}", quest->GetFormID(),
                         quest->GetFormEditorID());
            }
        }

        LOG_INFO("AutoSEQ: scan #{} - {} start-enabled quests, {} started.", m_scan_count, eligible, started);

        if (started > 0) {
            const auto msg =
                std::format("AutoSEQ: recovered {} stalled quest{}", started, started == 1 ? "" : "s");
            RE::DebugNotification(msg.c_str());
            if (auto* console = RE::ConsoleLog::GetSingleton()) {
                console->Print("%s (see AutoSEQ.log)", msg.c_str());
            }
        }
    }
}
