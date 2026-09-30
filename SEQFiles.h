#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SEQ {
    struct QuestRecord {
        std::uint32_t formID;
        std::uint16_t flags;
        std::string editorID;
    };

    struct PluginRecords {
        std::vector<std::string> masters;
        std::vector<QuestRecord> quests;

        [[nodiscard]] const QuestRecord* FindQuest(std::uint32_t a_formID) const;
    };

    // Read plugin files straight from disk and works out what their SEQ file must contain.
    class PluginReader {
    public:
        using Resolver = std::function<std::optional<std::filesystem::path>(const std::string& a_fileName)>;

        explicit PluginReader(Resolver a_resolver) : m_resolve(std::move(a_resolver)) {}

        // Cached. nullptr if the file is missing or can't be read safely.
        const PluginRecords* Load(const std::string& a_fileName);

        // Mirrors xEdit's "Create SEQ File": every Start Game Enabled quest that is new in the plugin, or that sets SGE
        // on a master quest which didn't have it.
        std::vector<const QuestRecord*> ExpectedSEQ(const PluginRecords& a_plugin);

        // Quests (file-relative FormIDs) that own a dialogue topic in this file. Not cached.
        std::optional<std::unordered_set<std::uint32_t>> ReadDialogueOwners(const std::string& a_fileName);

    private:
        Resolver m_resolve;
        std::unordered_map<std::string, std::optional<PluginRecords>> m_cache;
    };

    bool WriteSEQ(const std::filesystem::path& a_path, const std::vector<std::uint32_t>& a_formIDs);
}