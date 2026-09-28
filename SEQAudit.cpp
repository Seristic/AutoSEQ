#include "SEQAudit.h"

#include "SEQFiles.h"

#include <chrono>
#include <fstream>

namespace SEQ::Audit {
    namespace {
        constexpr std::string_view kPlugin = "AutoSEQ.esp";
        const std::filesystem::path kArchive{"Data/AutoSEQ.bsa"};
        const std::filesystem::path kPendingArchive{"Data/AutoSEQ.bsa.pending"};

        std::string Lower(std::string_view a_str) {
            std::string out(a_str);
            std::ranges::transform(out, out.begin(), [](unsigned char a_ch) { return static_cast<char>(std::tolower(a_ch)); });
            return out;
        }

        // Base game and Creation Club files ship without SEQ files and are left alone.
        std::unordered_set<std::string> BethesdaFiles() {
            std::unordered_set<std::string> files{"skyrim.esm", "update.esm", "dawnguard.esm", "hearthfires.esm",
                                                  "dragonborn.esm"};
            std::ifstream ccc("Skyrim.ccc");
            for (std::string line; std::getline(ccc, line);) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.empty()) {
                    files.insert(Lower(line));
                }
            }
            return files;
        }

        // Goes through the game's file system, so SEQ files packed inside BSAs are found too.
        std::optional<std::vector<std::uint32_t>> ReadSEQ(const std::string& a_stem) {
            RE::BSResourceNiBinaryStream stream(std::format("Seq\\{}.seq", a_stem));
            if (!stream.good()) {
                return std::nullopt;
            }
            std::vector<std::uint32_t> formIDs;
            std::uint32_t formID = 0;
            while (formIDs.size() < 0x10000 && stream.read(&formID, 1)) {
                formIDs.push_back(formID);
            }
            return formIDs;
        }

        // Loose file only, bypassing BSAs. Not ideal, but this was an absolute nightmare.
        std::optional<std::vector<std::uint32_t>> ReadLooseSEQ(const std::filesystem::path& a_path) {
            std::ifstream file(a_path, std::ios::binary);
            if (!file) {
                return std::nullopt;
            }
            std::vector<std::uint32_t> formIDs;
            std::uint32_t formID = 0;
            while (formIDs.size() < 0x10000 && file.read(reinterpret_cast<char*>(&formID), sizeof(formID))) {
                formIDs.push_back(formID);
            }
            return formIDs;
        }

        std::optional<std::vector<char>> ReadBytes(const std::filesystem::path& a_path) {
            std::ifstream in(a_path, std::ios::binary);
            if (!in) {
                return std::nullopt;
            }
            return std::vector<char>(std::istreambuf_iterator<char>(in), {});
        }

        bool WriteBytes(const std::filesystem::path& a_path, const std::vector<char>& a_bytes) {
            std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
            out.write(a_bytes.data(), static_cast<std::streamsize>(a_bytes.size()));
            out.close();
            return !out.fail();
        }

        void Print(const std::string& a_msg) {
            if (auto* console = RE::ConsoleLog::GetSingleton()) {
                console->Print("%s", a_msg.c_str());
            }
        }

        struct Problem {
            std::string name;
            std::string stem;
            std::vector<const QuestRecord*> expected;
            std::vector<const QuestRecord*> missing;
            bool hadSEQ;
        };

        void LogProblem(const Problem& a_problem, std::string_view a_what) {
            LOG_WARN("SEQ: {} - {}, {} of {} start-enabled quests not listed:", a_problem.name, a_what,
                     a_problem.missing.size(), a_problem.expected.size());
            for (const auto* quest : a_problem.missing) {
                LOG_WARN("    [{:08X}] {}", quest->formID, quest->editorID);
            }
        }

        std::vector<std::uint32_t> FormIDs(const std::vector<const QuestRecord*>& a_quests) {
            std::vector<std::uint32_t> formIDs;
            for (const auto* quest : a_quests) {
                formIDs.push_back(quest->formID);
            }
            return formIDs;
        }
    }

    void ApplyPendingArchive() {
        const auto pending = ReadBytes(kPendingArchive);
        if (!pending) {
            return;
        }
        if (WriteBytes(kArchive, *pending)) {
            std::error_code ec;
            std::filesystem::remove(kPendingArchive, ec);
            LOG_INFO("SEQ: installed the updated AutoSEQ.bsa ({} bytes)", pending->size());
        } else {
            LOG_ERROR("SEQ: could not install the updated AutoSEQ.bsa");
        }
    }

    void Run() {
        auto* handler = RE::TESDataHandler::GetSingleton();
        if (!handler) {
            return;
        }

        const auto start = std::chrono::steady_clock::now();
        const std::filesystem::path dataDir{"Data"};
        PluginReader reader([&](const std::string& a_name) -> std::optional<std::filesystem::path> {
            std::error_code ec;
            auto path = dataDir / a_name;
            return std::filesystem::is_regular_file(path, ec) ? std::optional{path} : std::nullopt;
        });
        const auto skip = BethesdaFiles();

        bool archiveMode = false;
        std::unordered_set<std::string> inArchive;
        for (const auto* file : handler->files) {
            if (file && file->GetCompileIndex() != 0xFF && Lower(file->GetFilename()) == Lower(kPlugin)) {
                archiveMode = true;
            }
        }
        if (archiveMode) {
            for (auto& name : ReadBSAFileNames(kArchive)) {
                inArchive.insert(std::move(name));
            }
        }

        std::uint32_t checked = 0;
        std::uint32_t ok = 0;
        std::vector<Problem> problems;
        std::vector<ArchiveFile> archiveFiles;

        for (const auto* file : handler->files) {
            if (!file || file->GetCompileIndex() == 0xFF) {
                continue;  // not loaded
            }
            const std::string name(file->GetFilename());
            if (skip.contains(Lower(name))) {
                continue;
            }

            const auto* plugin = reader.Load(name);
            if (!plugin) {
                LOG_WARN("SEQ: could not read {}, skipped", name);
                continue;
            }
            auto expected = reader.ExpectedSEQ(*plugin);
            if (expected.empty()) {
                continue;
            }

            const auto stem = std::filesystem::path(name).stem().string();
            const auto current = ReadSEQ(stem);
            if (!current) {
                // A missing SEQ only matters when a start-enabled quest has dialogue.
                const auto owners = reader.ReadDialogueOwners(name);
                if (!owners || std::ranges::none_of(expected, [&](auto* a_quest) { return owners->contains(a_quest->formID); })) {
                    continue;
                }
            }
            ++checked;

            std::vector<const QuestRecord*> missing;
            for (const auto* quest : expected) {
                if (!current || std::ranges::find(*current, quest->formID) == current->end()) {
                    missing.push_back(quest);
                }
            }

            // Keep plugins already in the archive: once it loads, the game sees our copy as the current one.
            if (!missing.empty() || inArchive.contains(Lower(stem) + ".seq")) {
                archiveFiles.push_back({stem + ".seq", FormIDs(expected)});
            }
            if (missing.empty()) {
                ++ok;
            } else {
                problems.push_back({name, stem, std::move(expected), std::move(missing), current.has_value()});
            }
        }

        std::uint32_t fixed = 0;
        std::uint32_t failed = 0;
        std::string summary;
        std::string messageBox;

        if (archiveMode) {
            bool updated = false;
            const auto fixCount = archiveFiles.size();
            archiveFiles.push_back({"AutoSEQ.seq", {}});
            const auto bytes = BuildBSA("seq", archiveFiles);
            const auto existing = ReadBytes(kArchive);
            if (!existing || *existing != bytes) {
                const auto& target = existing ? kPendingArchive : kArchive;
                if (WriteBytes(target, bytes)) {
                    updated = true;
                    LOG_INFO("SEQ: wrote {} with {} corrected SEQ files", target.string(), fixCount);
                } else {
                    failed = static_cast<std::uint32_t>(problems.size());
                    LOG_ERROR("SEQ: could not write {}", target.string());
                }
            }

            for (const auto& problem : problems) {
                LogProblem(problem, updated ? "fixed in AutoSEQ.bsa"
                                            : "still out of date even though AutoSEQ.bsa has the fix");
            }
            if (updated) {
                fixed = static_cast<std::uint32_t>(problems.size());
            }

            if (updated) {
                summary = std::format("AutoSEQ: {} of {} SEQ files were out of date - corrected in AutoSEQ.bsa, "
                                      "restart Skyrim to apply",
                                      problems.size(), checked);
            } else if (problems.empty()) {
                summary = std::format("AutoSEQ: all {} SEQ files are correct ({} supplied by AutoSEQ.bsa)",
                                      checked, fixCount);
            } else {
                summary = std::format("AutoSEQ: {} of {} SEQ files are still out of date - load AutoSEQ.esp last",
                                      problems.size(), checked);
            }
            if (updated) {
                messageBox = std::format("AutoSEQ updated AutoSEQ.bsa ({} corrected SEQ file{}).\n\nRestart "
                                         "Skyrim before starting or loading a game so the fixes take effect.",
                                         fixCount, fixCount == 1 ? "" : "s");
            } else if (!problems.empty()) {
                messageBox = std::format("AutoSEQ: {} plugin{} still read an out-of-date SEQ file. Make sure "
                                         "AutoSEQ.esp is enabled and at the very end of your load order.",
                                         problems.size(), problems.size() == 1 ? "" : "s");
            }
        } else {
            std::uint32_t blocked = 0;
            for (const auto& problem : problems) {
                const auto seqPath = dataDir / "Seq" / (problem.stem + ".seq");
                const auto loose = problem.hadSEQ ? ReadLooseSEQ(seqPath) : std::nullopt;
                const auto visible = problem.hadSEQ ? ReadSEQ(problem.stem) : std::nullopt;
                if (loose && visible && *loose != *visible) {
                    ++blocked;  // the game prefers the plugin's BSA copy, so a loose fix wouldn't be used
                    LogProblem(problem, "stale SEQ inside a BSA (needs AutoSEQ.esp to fix)");
                    continue;
                }
                LogProblem(problem, !problem.hadSEQ ? "no SEQ file" : loose ? "stale loose SEQ file" : "stale SEQ file inside a BSA");
                if (WriteSEQ(seqPath, FormIDs(problem.expected))) {
                    ++fixed;
                    LOG_INFO("    wrote Data\\Seq\\{}.seq", problem.stem);
                } else {
                    ++failed;
                    LOG_ERROR("    could not write Data\\Seq\\{}.seq", problem.stem);
                }
            }

            summary = std::format("AutoSEQ: {} plugins need an SEQ file - {} OK, {} fixed, {} blocked by a BSA "
                                  "copy, {} failed (AutoSEQ.esp not loaded)",
                                  checked, ok, fixed, blocked, failed);
            if (fixed > 0) {
                messageBox = std::format("AutoSEQ fixed {} SEQ file{}.\n\nRestart Skyrim before starting or loading "
                                         "a game so the fixes take effect.",
                                         fixed, fixed == 1 ? "" : "s");
            }
            if (blocked > 0) {
                messageBox += std::format("{}{} out-of-date SEQ file{} inside BSAs need AutoSEQ.esp: enable it at "
                                          "the very end of your load order.",
                                          messageBox.empty() ? "AutoSEQ: " : "\n\n", blocked, blocked == 1 ? "" : "s");
            }
        }

        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        summary += std::format(" ({} ms)", ms.count());
        LOG_INFO("{}", summary);
        Print(summary);
        if (!messageBox.empty()) {
            RE::DebugMessageBox((messageBox + "\n\nDetails are in AutoSEQ.log.").c_str());
        }
    }
}
