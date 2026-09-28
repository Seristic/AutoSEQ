#pragma once

namespace SEQ {
    class StalledQuests : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static StalledQuests* GetSingleton() {
            static StalledQuests singleton;
            return &singleton;
        }

        void Register();
        void OnNewGame();
        void OnPostLoadGame();
        void RunScan();

        StalledQuests(const StalledQuests&) = delete;
        StalledQuests(StalledQuests&&) = delete;
        StalledQuests& operator=(const StalledQuests&) = delete;
        StalledQuests& operator=(StalledQuests&&) = delete;

    protected:
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source) override;

    private:
        StalledQuests() = default;
        ~StalledQuests() override = default;

        std::atomic_bool m_scan_pending{false};
        std::uint32_t m_scan_count{0};
    };
}
