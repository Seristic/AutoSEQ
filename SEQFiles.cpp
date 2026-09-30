#include "SEQFiles.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <string_view>

namespace SEQ {
    namespace {
        constexpr std::uint32_t kCompressed = 0x00040000;

        // Record header; for GRUPs, `flags` holds the group label and `formID` the group type.
        struct Header {
            char type[4];
            std::uint32_t size;
            std::uint32_t flags;
            std::uint32_t formID;
            std::uint32_t unused[2];
        };
        static_assert(sizeof(Header) == 24);

        bool Is(const char (&a_type)[4], std::string_view a_sig) { return std::string_view(a_type, 4) == a_sig; }

        std::string Lower(std::string a_str) {
            std::ranges::transform(a_str, a_str.begin(), [](unsigned char a_ch) { return static_cast<char>(std::tolower(a_ch)); });
            return a_str;
        }

        std::string ZString(const char* a_data, std::size_t a_size) { return {a_data, strnlen(a_data, a_size)}; }

        // An out-of-range file index means "this file".
        std::uint32_t FixFormID(std::uint32_t a_formID, std::size_t a_masterCount) {
            if ((a_formID >> 24) <= a_masterCount) {
                return a_formID;
            }
            return (static_cast<std::uint32_t>(a_masterCount) << 24) | (a_formID & 0xFFFFFF);
        }

        template <class Fn>
        void ForEachSubrecord(const std::vector<char>& a_data, Fn&& a_fn) {
            std::size_t pos = 0;
            std::optional<std::uint32_t> bigSize;
            while (pos + 6 <= a_data.size()) {
                const std::string_view type(a_data.data() + pos, 4);
                std::uint16_t size16 = 0;
                std::memcpy(&size16, a_data.data() + pos + 4, 2);
                pos += 6;

                if (type == "XXXX") {
                    if (size16 >= 4 && pos + 4 <= a_data.size()) {
                        std::uint32_t size32 = 0;
                        std::memcpy(&size32, a_data.data() + pos, 4);
                        bigSize = size32;
                    }
                    pos += size16;
                    continue;
                }

                const std::size_t size = bigSize.value_or(size16);
                bigSize.reset();
                if (pos + size > a_data.size()) {
                    return;
                }
                a_fn(type, a_data.data() + pos, size);
                pos += size;
            }
        }

        class RecordFile {
        public:
            explicit RecordFile(const std::filesystem::path& a_path) : m_file(a_path, std::ios::binary) {
                if (m_file) {
                    m_file.seekg(0, std::ios::end);
                    m_size = static_cast<std::uint64_t>(m_file.tellg());
                }
            }

            explicit operator bool() const { return static_cast<bool>(m_file); }
            [[nodiscard]] std::uint64_t size() const { return m_size; }

            bool ReadHeader(std::uint64_t a_pos, Header& a_header) {
                return Read(a_pos, reinterpret_cast<char*>(&a_header), sizeof(Header));
            }

            bool ReadData(std::uint64_t a_pos, std::uint32_t a_size, std::vector<char>& a_out) {
                a_out.resize(a_size);
                return Read(a_pos, a_out.data(), a_size);
            }

        private:
            bool Read(std::uint64_t a_pos, char* a_out, std::uint64_t a_size) {
                if (a_pos + a_size > m_size) {
                    return false;
                }
                m_file.clear();
                m_file.seekg(static_cast<std::streamoff>(a_pos));
                return static_cast<bool>(m_file.read(a_out, static_cast<std::streamsize>(a_size)));
            }

            std::ifstream m_file;
            std::uint64_t m_size{0};
        };

        // Visits the records directly inside every top-level group labelled a_label. Returns false if the file is malformed or a_fn fails.
        template <class Fn>
        bool ForEachTopRecord(RecordFile& a_file, std::uint64_t a_start, std::string_view a_label, Fn&& a_fn) {
            Header group{};
            for (auto pos = a_start; pos + sizeof(Header) <= a_file.size(); pos += group.size) {
                if (!a_file.ReadHeader(pos, group) || !Is(group.type, "GRUP") || group.size < sizeof(Header)) {
                    return false;
                }
                if (std::string_view(reinterpret_cast<const char*>(&group.flags), 4) != a_label) {
                    continue;
                }

                const auto end = pos + group.size;
                Header record{};
                for (auto rpos = pos + sizeof(Header); rpos < end;) {
                    if (!a_file.ReadHeader(rpos, record)) {
                        return false;
                    }
                    if (Is(record.type, "GRUP")) {
                        if (record.size < sizeof(Header)) {
                            return false;
                        }
                        rpos += record.size;
                        continue;
                    }
                    if (!a_fn(rpos + sizeof(Header), record)) {
                        return false;
                    }
                    rpos += sizeof(Header) + record.size;
                }
            }
            return true;
        }

        std::optional<PluginRecords> ReadPlugin(const std::filesystem::path& a_path) {
            RecordFile file(a_path);
            Header header{};
            std::vector<char> data;
            if (!file || !file.ReadHeader(0, header) || !Is(header.type, "TES4") ||
                !file.ReadData(sizeof(Header), header.size, data)) {
                return std::nullopt;
            }

            PluginRecords plugin;
            ForEachSubrecord(data, [&](std::string_view a_type, const char* a_data, std::size_t a_size) {
                if (a_type == "MAST") {
                    plugin.masters.push_back(ZString(a_data, a_size));
                }
            });

            const bool ok = ForEachTopRecord(file, sizeof(Header) + header.size, "QUST",
                                             [&](std::uint64_t a_dataPos, const Header& a_record) {
                if (!Is(a_record.type, "QUST")) {
                    return true;
                }
                // Never guess: a skipped quest would produce an incomplete SEQ. We never touch these because they just cause issues.
                if ((a_record.flags & kCompressed) || !file.ReadData(a_dataPos, a_record.size, data)) {
                    return false;
                }

                QuestRecord quest{FixFormID(a_record.formID, plugin.masters.size()), 0, {}};
                bool hasFlags = false;
                ForEachSubrecord(data, [&](std::string_view a_type, const char* a_data, std::size_t a_size) {
                    if (a_type == "EDID") {
                        quest.editorID = ZString(a_data, a_size);
                    } else if (a_type == "DNAM" && !hasFlags && a_size >= 2) {
                        std::memcpy(&quest.flags, a_data, 2);
                        hasFlags = true;
                    }
                });
                if (hasFlags) {
                    plugin.quests.push_back(std::move(quest));
                }
                return true;
            });

            if (!ok) {
                return std::nullopt;
            }
            return plugin;
        }
    }

    const QuestRecord* PluginRecords::FindQuest(std::uint32_t a_formID) const {
        const auto it = std::ranges::find(quests, a_formID, &QuestRecord::formID);
        return it != quests.end() ? &*it : nullptr;
    }

    const PluginRecords* PluginReader::Load(const std::string& a_fileName) {
        auto [it, inserted] = m_cache.try_emplace(Lower(a_fileName));
        if (inserted) {
            if (const auto path = m_resolve(a_fileName)) {
                it->second = ReadPlugin(*path);
            }
        }
        return it->second ? &*it->second : nullptr;
    }

    std::vector<const QuestRecord*> PluginReader::ExpectedSEQ(const PluginRecords& a_plugin) {
        std::vector<const QuestRecord*> expected;
        for (const auto& quest : a_plugin.quests) {
            if (!(quest.flags & 1)) {
                continue;
            }
            const auto index = quest.formID >> 24;
            if (index < a_plugin.masters.size()) {
                if (const auto* master = Load(a_plugin.masters[index])) {
                    const auto masterID = (static_cast<std::uint32_t>(master->masters.size()) << 24) | (quest.formID & 0xFFFFFF);
                    const auto* original = master->FindQuest(masterID);
                    if (original && (original->flags & 1)) {
                        continue;  // the master quest was already start game enabled... I hope
                    }
                }
            }
            expected.push_back(&quest);
        }
        return expected;
    }

    std::optional<std::unordered_set<std::uint32_t>> PluginReader::ReadDialogueOwners(const std::string& a_fileName) {
        const auto* plugin = Load(a_fileName);
        const auto path = m_resolve(a_fileName);
        if (!plugin || !path) {
            return std::nullopt;
        }

        RecordFile file(*path);
        Header header{};
        if (!file || !file.ReadHeader(0, header)) {
            return std::nullopt;
        }

        std::unordered_set<std::uint32_t> owners;
        std::vector<char> data;
        const bool ok = ForEachTopRecord(file, sizeof(Header) + header.size, "DIAL",
                                         [&](std::uint64_t a_dataPos, const Header& a_record) {
            if (!Is(a_record.type, "DIAL") || (a_record.flags & kCompressed)) {
                return true;
            }
            if (!file.ReadData(a_dataPos, a_record.size, data)) {
                return false;
            }
            ForEachSubrecord(data, [&](std::string_view a_type, const char* a_data, std::size_t a_size) {
                if (a_type == "QNAM" && a_size >= 4) {
                    std::uint32_t owner = 0;
                    std::memcpy(&owner, a_data, 4);
                    owners.insert(FixFormID(owner, plugin->masters.size()));
                }
            });
            return true;
        });

        if (!ok) {
            return std::nullopt;
        }
        return owners;
    }

    bool WriteSEQ(const std::filesystem::path& a_path, const std::vector<std::uint32_t>& a_formIDs) {
        std::error_code ec;
        std::filesystem::create_directories(a_path.parent_path(), ec);

        std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(a_formIDs.data()),
                  static_cast<std::streamsize>(a_formIDs.size() * sizeof(std::uint32_t)));
        out.close();
        return !out.fail();
    }
}
