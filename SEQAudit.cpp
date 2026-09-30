#include "SEQAudit.h"

#include <chrono>
#include <fstream>
#include <set>

#include "SEQFiles.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <Windows.h>
#include <psapi.h>

namespace SEQ::Audit {
    namespace {

        std::string Lower(std::string_view a_str) {
            std::string out(a_str);
            std::ranges::transform(out, out.begin(),
                                   [](unsigned char a_ch) { return static_cast<char>(std::tolower(a_ch)); });
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

        std::string Trim(std::string_view a_str) {
            const auto first = a_str.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) {
                return {};
            }
            const auto last = a_str.find_last_not_of(" \t\r\n");
            return std::string(a_str.substr(first, last - first + 1));
        }

        // path::string() throws on characters outside the ANSI code page, so log paths as UTF-8.
        std::string PathText(const std::filesystem::path& a_path) {
            const auto text = a_path.u8string();
            return std::string(reinterpret_cast<const char*>(text.data()), text.size());
        }

        bool SamePath(const std::filesystem::path& a_lhs, const std::filesystem::path& a_rhs) {
            std::error_code ec;
            const auto lhs = std::filesystem::absolute(a_lhs, ec).lexically_normal();
            const auto rhs = std::filesystem::absolute(a_rhs, ec).lexically_normal();
            return Lower(PathText(lhs)) == Lower(PathText(rhs));
        }

        // Anything in Data belongs to other mods (and with Vortex it's a hardlink to their copy), so we never write
        // there.
        bool InsideData(const std::filesystem::path& a_path) {
            std::error_code ec;
            const auto data = Lower(PathText(std::filesystem::absolute("Data", ec).lexically_normal()));
            const auto path = Lower(PathText(std::filesystem::absolute(a_path, ec).lexically_normal()));
            return data.empty() || path.empty() || path == data || path.starts_with(data + "\\");
        }

        // GetModuleFileNameW gives MO2's virtual Data path, but the mapped file is the real one in the mod folder.
        std::optional<std::filesystem::path> DllLocation() {
            static const char anchor = 0;
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&anchor), &module)) {
                return std::nullopt;
            }

            // e.g. \Device\HarddiskVolume5\Skyrim Modlists\...\AutoSEQ.dll
            std::wstring mapped(32768, L'\0');
            const auto length =
                K32GetMappedFileNameW(GetCurrentProcess(), module, mapped.data(), static_cast<DWORD>(mapped.size()));
            if (length == 0) {
                return std::nullopt;
            }
            mapped.resize(length);

            // Swap the device part for a drive letter. The list is "C:\<NUL>D:\<NUL>..."
            std::wstring drives(1024, L'\0');
            const auto drivesLength = GetLogicalDriveStringsW(static_cast<DWORD>(drives.size()), drives.data());
            if (drivesLength == 0 || drivesLength > drives.size()) {
                return std::nullopt;
            }
            drives.resize(drivesLength);

            for (std::size_t pos = 0; pos < drives.size();) {
                auto end = drives.find(L'\0', pos);
                if (end == std::wstring::npos) {
                    end = drives.size();
                }
                const auto letter = drives.substr(pos, 2);
                pos = end + 1;

                std::wstring device(1024, L'\0');
                if (QueryDosDeviceW(letter.c_str(), device.data(), static_cast<DWORD>(device.size())) == 0) {
                    continue;
                }
                if (const auto nul = device.find(L'\0'); nul != std::wstring::npos) {
                    device.resize(nul);
                }
                if (mapped.size() > device.size() && mapped.starts_with(device) && mapped[device.size()] == L'\\') {
                    return std::filesystem::path(letter + mapped.substr(device.size()));
                }
            }
            return std::nullopt;
        }

        // Vortex hardlinks the DLL into Data, so one of its other names points at the staging folder copy.
        std::vector<std::filesystem::path> DllPaths() {
            std::vector<std::filesystem::path> paths;
            const auto loaded = DllLocation();
            if (!loaded) {
                return paths;
            }
            paths.push_back(*loaded);

            // Link names come back without the drive letter.
            const auto volume = loaded->root_name().wstring();
            std::wstring name(32768, L'\0');
            auto length = static_cast<DWORD>(name.size());
            const auto find = FindFirstFileNameW(loaded->c_str(), 0, &length, name.data());
            if (find == INVALID_HANDLE_VALUE) {
                return paths;
            }
            do {
                const std::filesystem::path link(volume + name.c_str());
                if (!SamePath(link, *loaded)) {
                    paths.push_back(link);
                }
                length = static_cast<DWORD>(name.size());
            } while (FindNextFileNameW(find, &length, name.data()));
            FindClose(find);
            return paths;
        }

        // The SEQ files we made ourselves, kept in Seq\AutoSEQ.txt. Anything not on it isn't ours to overwrite.
        std::set<std::string> ReadOwnList(const std::filesystem::path& a_file) {
            std::set<std::string> names;
            std::ifstream in(a_file);
            for (std::string line; std::getline(in, line);) {
                line = Trim(line);
                if (!line.empty()) {
                    names.insert(Lower(line));
                }
            }
            return names;
        }

        // Only ever AutoSEQ's own mod folder. If we can't find it, nothing gets written.
        std::optional<std::filesystem::path> OutputSeqFolder() {
            for (const auto& dll : DllPaths()) {
                LOG_INFO("SEQ: AutoSEQ.dll found at {}", PathText(dll));
                const auto plugins = dll.parent_path();
                const auto skse = plugins.parent_path();
                const auto mod = skse.parent_path();
                if (Lower(PathText(plugins.filename())) == "plugins" && Lower(PathText(skse.filename())) == "skse" &&
                    !InsideData(mod)) {
                    return mod / "Seq";
                }
            }

            LOG_ERROR(
                "SEQ: couldn't find AutoSEQ's mod folder, so no SEQ files will be written. Install AutoSEQ "
                "with MO2 or Vortex");
            return std::nullopt;
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

        std::uint32_t checked = 0;
        std::uint32_t ok = 0;
        std::vector<Problem> problems;

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
                if (!owners ||
                    std::ranges::none_of(expected, [&](auto* a_quest) { return owners->contains(a_quest->formID); })) {
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
            if (missing.empty()) {
                ++ok;
            } else {
                problems.push_back({name, stem, std::move(expected), std::move(missing), current.has_value()});
            }
        }

        std::uint32_t fixed = 0;
        std::uint32_t failed = 0;
        std::uint32_t unseen = 0;
        std::uint32_t otherMod = 0;
        std::uint32_t inBSA = 0;
        std::string summary;
        std::string messageBox;

        const auto outDir = OutputSeqFolder();
        std::set<std::string> ours;
        bool oursChanged = false;
        if (outDir) {
            LOG_INFO("SEQ: writing fixes to {}", PathText(*outDir));
            ours = ReadOwnList(*outDir / "AutoSEQ.txt");
        }

        for (const auto& problem : problems) {
            const auto expectedIDs = FormIDs(problem.expected);
            if (!outDir) {
                ++failed;
                LogProblem(problem, problem.hadSEQ ? "stale SEQ file" : "no SEQ file");
                LOG_ERROR("    not written, AutoSEQ's mod folder is unknown");
                continue;
            }
            const auto outFile = *outDir / (problem.stem + ".seq");

            // Belt and braces, never write into Data.
            if (InsideData(outFile)) {
                ++failed;
                LogProblem(problem, problem.hadSEQ ? "stale SEQ file" : "no SEQ file");
                LOG_ERROR("    not written, {} is inside the Data folder", PathText(outFile));
                continue;
            }

            // Ours is already right but the game reads something else. Check the loose copy the game sees to work out
            // why.
            if (ReadLooseSEQ(outFile) == expectedIDs) {
                const auto visible = ReadLooseSEQ(dataDir / "Seq" / (problem.stem + ".seq"));
                if (visible && *visible != expectedIDs) {
                    ++otherMod;
                    LogProblem(problem, "fix already written, but another mod's loose SEQ file wins");
                } else if (visible && problem.hadSEQ) {
                    ++inBSA;
                    LogProblem(problem, "fix already written, but the SEQ file packed in a BSA still wins");
                } else {
                    ++unseen;
                    LogProblem(problem, "fix already written, but the game doesn't see it yet");
                }
                continue;
            }

            LogProblem(problem, problem.hadSEQ ? "stale SEQ file" : "no SEQ file");

            // Even in our own folder, don't overwrite a file we didn't make (merged mod folders and such).
            const auto fileName = Lower(problem.stem + ".seq");
            std::error_code ec;
            if ((std::filesystem::exists(outFile, ec) || ec) && !ours.contains(fileName)) {
                ++failed;
                LOG_ERROR("    not written, {} is already there and AutoSEQ didn't make it", PathText(outFile));
                continue;
            }

            if (WriteSEQ(outFile, expectedIDs)) {
                ++fixed;
                oursChanged |= ours.insert(fileName).second;
                LOG_INFO("    wrote {}", PathText(outFile));
            } else {
                ++failed;
                LOG_ERROR("    could not write {}", PathText(outFile));
            }
        }

        if (oursChanged) {
            std::ofstream list(*outDir / "AutoSEQ.txt", std::ios::trunc);
            for (const auto& name : ours) {
                list << name << '\n';
            }
        }

        summary = std::format(
            "AutoSEQ: {} plugins need an SEQ file - {} OK, {} written, {} not picked up yet, {} overridden by another "
            "mod, {} overridden by a BSA, {} failed",
            checked, ok, fixed, unseen, otherMod, inBSA, failed);
        if (!outDir && !problems.empty()) {
            messageBox = std::format(
                "AutoSEQ: {} plugin{} need{} a new SEQ file, but AutoSEQ only writes into its own mod folder and "
                "couldn't find it, so nothing was written. Your mods' own SEQ files were not touched.\n\n"
                "Install AutoSEQ with Mod Organizer 2 or Vortex, not straight into Data.",
                problems.size(), problems.size() == 1 ? "" : "s", problems.size() == 1 ? "s" : "");
        }
        if (fixed > 0) {
            messageBox = std::format(
                "AutoSEQ wrote {} new SEQ file{} to {}\n\nYour mods' own SEQ files were not touched. Restart Skyrim "
                "before starting or loading a game so the new files take effect (in MO2 press F5, in Vortex click "
                "Deploy Mods, before launching again).",
                fixed, fixed == 1 ? "" : "s", PathText(*outDir));
        }
        if (unseen > 0) {
            messageBox += std::format(
                "{}{} new SEQ file{} not picked up by the game yet. Make sure the AutoSEQ mod is enabled, then press "
                "F5 in MO2 or click Deploy Mods in Vortex before launching.",
                messageBox.empty() ? "AutoSEQ: " : "\n\n", unseen, unseen == 1 ? " is" : "s are");
        }
        if (otherMod > 0) {
            messageBox += std::format(
                "{}{} new SEQ file{} overridden by another mod's loose SEQ file. Give AutoSEQ the highest priority "
                "(bottom of MO2's left pane, or a 'load after' rule in Vortex).",
                messageBox.empty() ? "AutoSEQ: " : "\n\n", otherMod, otherMod == 1 ? " is" : "s are");
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