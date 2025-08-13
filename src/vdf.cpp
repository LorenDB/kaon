//
// Copyright 2020-2024 Andon "Kaldaien" Coleman
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//

// This file was originally part of the Special K Injection Frontend (https://github.com/SpecialKO/SKIF).
// It has been modified to work in Kaon. These modifications include, but are not limited to, deleting
// parts that are irrelevant to Kaon's operation, migrating code to use modern C++ idioms, using Qt
// functionality where possible, and targeting Linux instead of Windows.

#include "vdf.h"

#include <filesystem>

#include <QDebug>
#include <QFile>
#include <QFileInfo>

#include "Steam.h"

using namespace Qt::Literals;

// Shorthands, to make life less painful
using AppInfo = ValveDataFile::AppInfo;
using app_section_s = AppInfo::Section;

ValveDataFile *ValveDataFile::s_instance{nullptr};

uint32_t ValveDataFile::vdf_version = 0x27; // Default to Pre-December 2022

ValveDataFile::ValveDataFile(QString source)
    : path(source)
{
    if (!s_instance)
        s_instance = this;

    QFile dataFile{path};
    if (dataFile.open(QIODevice::ReadOnly))
    {
        _data = dataFile.readAll();
        base = reinterpret_cast<header_s *>(_data.data());

        vdf_version = ((uint8_t *)&base->version)[0];
        root = &base->head;

        // A string table was added in June of 2024 (0x29)
        if (vdf_version >= 0x29)
        {
            uintptr_t strtable_pos = (uintptr_t)*(uint64_t *)root;
            root = (AppInfo *)((uint64_t *)root + 1);
            table = (str_tbl_s *)(&_data[strtable_pos]);

            // Valve is using 64-bit offsets, if this file is larger than
            // 4 GiB SKIF32 is fundamentally inoperable!
            if (*(uint64_t *)root > std::numeric_limits<uintptr_t>::max())
                qCritical() << "VDF File is Too Large!";

            strs.reserve(table->num_strings);
            strs.push_back((char *)table->strings);

            char *str = (char *)table->strings;
            char *end_tbl = (char *)_data.data() + _data.size();

            for (uint32_t i = 1; i < table->num_strings; ++i)
            {
                while (*str++ != '\0' && str < end_tbl)
                    ;

                if (str > end_tbl)
                {
                    // On overflow, restart table iteration from the beginning
                    qCritical() << "Malformed string table detected!";
                    str = (char *)table->strings;
                }

                strs.push_back(str);
            }
        }
    }
}

void app_section_s::parse(SectionDesc &desc)
{
    static std::map<_TokenOp, size_t> operand_sizes = {{Int32, sizeof(int32_t)}, {Int64, sizeof(int64_t)}};
    std::vector<section_data_s> raw_sections;
    static bool exception = false;

    if (!exception)
    {
        for (uint8_t *cur = (uint8_t *)desc.blob; cur < (uint8_t *)desc.blob + desc.size; cur++)
        {
            auto op = (_TokenOp)(*cur);
            auto name = (char *)(cur + 1);
            if (op != SectionEnd)
            {
                // String Table Lookup (June 2024+)
                //
                if (ValveDataFile::instance()->vdf_version >= 0x29)
                {
                    name = (char *)ValveDataFile::instance()->table->strings;

                    const auto str_idx = *(uint32_t *)(cur + 1);

#ifdef DEBUG
                    qDebug() << "String Table Index:  " << str_idx << ", op=" << op;
#endif

                    if (str_idx < ValveDataFile::instance()->table->num_strings)
                    {
                        name = ValveDataFile::instance()->strs[str_idx];
#ifdef DEBUG
                        qDebug() << "String=" << name;
#endif
                    }
                    else
                        qCritical() << "String Table Index (" << str_idx << ") Out-of-Range!";

                    cur += 4;
                }

                // Legacy: null-terminated name is serialized inline after token type
                //
                else
                {
                    // Skip past name declarations, except for </Section> because it has no name.
                    cur++;
                    while (*cur != '\0')
                        ++cur;
                }
            }

            if (op == SectionBegin)
            {
                if (!raw_sections.empty())
                    raw_sections.push_back({raw_sections.back().name + '.' + name, {(void *)cur, 0}});
                else
                    raw_sections.push_back({name, {(void *)cur, 0}});
            }
            else if (op == SectionEnd)
            {
                if (!raw_sections.empty())
                {
                    raw_sections.back().desc.size = (uintptr_t)cur - (uintptr_t)raw_sections.back().desc.blob;
                    finished_sections.push_back(raw_sections.back());
                    raw_sections.pop_back();
                }
            }
            else
            {
                ++cur;

                switch (op)
                {
                case String:
                    if (!raw_sections.empty())
                        raw_sections.back().keys.push_back({name, {String, (void *)cur}});
                    else
                        exception = true;

                    while (*cur != '\0')
                        ++cur;
                    break;

                case Int32:
                case Int64:
                    if (!raw_sections.empty())
                        raw_sections.back().keys.push_back({name, {op, (void *)cur}});
                    else
                        exception = true;

                    cur += (operand_sizes[op] - 1);
                    break;

                default:
                    qWarning() << "Unknown VDF Token Operator: " << op;
                    exception = true;
                    break;
                }
            }
        }
    }
}

void *AppInfo::getRootSection(size_t *pSize)
{
    size_t vdf_header_size = (vdf_version > 0x27 ? sizeof(AppInfo) : sizeof(AppInfo27));

    static bool runOnce = true;
    if (runOnce)
    {
        runOnce = false;

        switch (vdf_version)
        {
        case 0x29: // v41
            qDebug() << "appinfo.vdf version: " << vdf_version << " (June 2024)";
            break;
        case 0x28: // v40
            qDebug() << "appinfo.vdf version: " << vdf_version << " (December 2022)";
            break;
        case 0x27: // v39
            qDebug() << "appinfo.vdf version: " << vdf_version << " (pre-December 2022)";
            break;
        default:
            qWarning() << "appinfo.vdf version: " << vdf_version << " (unknown/unsupported)";
        }
    }

    size_t kv_size = (size - vdf_header_size + 8);

    if (pSize != nullptr)
        *pSize = kv_size;

    return (uint8_t *)&appid + vdf_header_size;
}

AppInfo *AppInfo::getNextApp(void)
{
    SectionDesc root_sec{};

    root_sec.blob = getRootSection(&root_sec.size);

    auto *pNext = (AppInfo *)((uint8_t *)root_sec.blob + root_sec.size);

    return (pNext->appid == _LastSteamApp) ? nullptr : pNext;
}

AppInfo *ValveDataFile::getAppInfo(Game *game)
{
    // static SKIF_CommonPathsCache &_path_cache = SKIF_CommonPathsCache::GetInstance();

    if (root != nullptr)
    {
        ValveDataFile::AppInfo *pIter = root;

        static AppInfo::Section section;
        AppInfo::SectionDesc app_desc{};

        section.finished_sections.clear();
        app_desc.blob = pIter->getRootSection(&app_desc.size);
        section.parse(app_desc);

        for (auto &finished_section : section.finished_sections)
        {
            if (finished_section.keys.empty())
                continue;

            if (finished_section.name.startsWith("appinfo.extended"))
            {
                auto *pVac = &game->m_vacInfo;
                for (auto &key : finished_section.keys)
                {
                    if (QString{key.first}.toLower() == "vacmodulefilename"_L1)
                        pVac->vacModuleFilename = (const char *)key.second.second;
                    pVac->hasVac = !pVac->vacModuleFilename.isEmpty();
                }
            }

            // auto _ParseOSArch = [&](appinfo_s::section_s::_kv_pair &kv) -> app_record_s::CPUType {
            //     app_record_s::CPUType cpu_type = app_record_s::CPUType::Common;
            //     int bits = -1;

            //     // This key is an integer sometimes and a string others, it's a PITA!
            //     if (kv.second.first == appinfo_s::section_s::String)
            //     {
            //         bits = *(char *)kv.second.second;

            //         if (bits == 0)
            //         {
            //             cpu_type = app_record_s::CPUType::Any;
            //         }

            //         else
            //         {
            //             bits = std::atoi((char *)kv.second.second);
            //         }
            //     }

            //     if (bits != 0)
            //     {
            //         // We have an int32 key, need to extract the value
            //         if (bits == -1)
            //             bits = *(int32_t *)kv.second.second;

            //         // else
            //         // ... Otherwise we already got the value as a string

            //         switch (bits)
            //         {
            //         case 32:
            //             cpu_type = app_record_s::CPUType::x86;
            //             break;
            //         case 64:
            //             cpu_type = app_record_s::CPUType::x64;
            //             break;
            //         default:
            //             cpu_type = app_record_s::CPUType::Any;
            //             qCritical() << SK_FormatString("Got unexpected (int32) CPU osarch=%lu", bits);
            //             break;
            //         }
            //     }

            //     return cpu_type;
            // };

            // if (populate_common && finished_section.name.startsWith("appinfo.common"))
            // {
            //     game->common_config.appid = game->id;

            //     for (auto &key : finished_section.keys)
            //     {
            //         // OS Arch? More like CPU Arch...
            //         //
            //         // This key, under common_config, either:
            //         //  - do not exist at all, see  23310: The Last Remnant           (what does this mean?)
            //         //  -            is empty, see    480: Spacewar (or most games)   (what does this mean? x86?)
            //         //  -      is set to "64", see 546560: Half-Life: Alyx
            //         //
            //         // This makes it utterly useless for anything reliable, lol
            //         //
            //         // See SteamDB's unique values search:
            //         // -
            //         //
            //         https://steamdb.info/search/?a=app_keynames&type=-1&keyname=369&operator=9&keyvalue=&display_value=on

            //         if (!_stricmp(key.first, "osarch"))
            //         {
            //             game->common_config.cpu_type = _ParseOSArch(key);
            //         }

            //         else if (!_stricmp(key.first, "type"))
            //         {
            //             if (!_stricmp((char *)key.second.second, "game"))
            //                 game->common_config.type = app_record_s::common_config_s::AppType::Game;
            //             else if (!_stricmp((char *)key.second.second, "application"))
            //                 game->common_config.type = app_record_s::common_config_s::AppType::Application;
            //             else if (!_stricmp((char *)key.second.second, "tool"))
            //                 game->common_config.type = app_record_s::common_config_s::AppType::Tool;
            //             else if (!_stricmp((char *)key.second.second, "music"))
            //                 game->common_config.type = app_record_s::common_config_s::AppType::Music;
            //             else if (!_stricmp((char *)key.second.second, "demo"))
            //                 game->common_config.type = app_record_s::common_config_s::AppType::Demo;
            //         }

            //         else if (!_stricmp(key.first, "icon"))
            //         {
            //             game->common_config.icon_hash = (char *)key.second.second;
            //         }
            //     }
            // }

            // TODO: is this useful?
            // if (get_library_asset_paths && finished_section.name == "appinfo.common.library_assets_full."
            //         "library_capsule.image"_L1)
            // {
            //     bool found_english_asset = false;
            //     for (auto &key : finished_section.keys)
            //     {
            //         // TODO: refactor this to not need qstricmp
            //         if (!qstricmp(key.first, "english"))
            //         {
            //             game->common_config.boxart_hash = (const char *)key.second.second;
            //             found_english_asset = true;
            //             break;
            //         }
            //     }

            //     if (!found_english_asset)
            //     {
            //         // Since language is not known, just use the first language found
            //         for (auto &key : finished_section.keys)
            //         {
            //             game->common_config.boxart_hash = (const char *)key.second.second;
            //             break;
            //         }
            //     }
            //     get_library_asset_paths = false;
            // }

            if (finished_section.name.startsWith("appinfo.config.launch."))
            {
                // qDebug() << "---------------------------";

                int launch_idx_skif = static_cast<int>(game->launch_configs.size());
                int launch_idx_steam = 0; // We do not currently actually use this for anything.
                // It is also unreliable as developers can remove launch configs...

                // TODO: this probably can be ported to QString-native
                std::sscanf(finished_section.name.toStdString().c_str(), "appinfo.config.launch.%d", &launch_idx_steam);

                // The index used solely for parsing
                int idx = launch_idx_steam;

                // Use the Steam launch key to workaround some parsing issue or another...
                // TODO: Fix this shit -- it's a shitty workaround for stupid duplicate parsing!
                //       AND it breaks Instant Play custom options... :(
                auto &launch_cfg = game->launch_configs[launch_idx_steam];

                launch_cfg.id = launch_idx_skif;
                launch_cfg.id_steam = launch_idx_steam;

                QHash<QString, QString *> string_map = {{"executable", &launch_cfg.executable},
                                                        {"arguments", &launch_cfg.launch_options},
                                                        {"description", &launch_cfg.description},
                                                        {"workingdir", &launch_cfg.working_dir},
                                                        {"ownsdlc", &launch_cfg.requires_dlc}};

                for (auto &key : finished_section.keys)
                {
                    // if (QString{key.first}.toLower() == "oslist"_L1)
                    // {
                    //     if (QString{(const char *)key.second.second}.toLower().contains("windows"_L1))
                    //     {
                    //         app_record_s::addSupportFor(game->launch_configs[idx].platforms,
                    //         app_record_s::Platform::Windows);
                    //     }

                    //     else
                    //         game->launch_configs[idx].platforms = app_record_s::Platform::Unknown;
                    // }

                    // else if (QString{key.first}.toLower() == "osarch"_L1)
                    // {
                    //     OS Arch? More like CPU Arch...
                    //     game->launch_configs[idx].cpu_type = _ParseOSArch(key);
                    // }

                    /*else*/ if (QString{key.first}.toLower() == "betakey"_L1)
                    {
                        // Populate required betas for this launch option
                        std::istringstream betas((const char *)key.second.second);
                        std::string beta;
                        while (std::getline(betas, beta, ' '))
                            game->launch_configs[idx].branches.emplace(QString::fromStdString(beta));
                    }

                    else if (string_map.count(key.first) != 0)
                    {
                        auto &string_dest = string_map[key.first];

                        *string_dest = (const char *)key.second.second;
                    }
                }

                // There is a really annoying bug in SKIF where parsing the launch configs results in
                // semi-duplicate empty entries,
                //   though I have no idea why... Maybe has to do with some "padding" between the sections in the
                //   appinfo.vdf file?

                // #if 0

                qDebug() << "Steam Launch ID   : " << launch_idx_steam;
                qDebug() << "SKIF  Launch ID   : " << launch_idx_skif;
                qDebug() << "Executable        : " << launch_cfg.executable;
                qDebug() << "Arguments         : " << launch_cfg.launch_options;
                qDebug() << "Working Directory : " << launch_cfg.working_dir;
                qDebug() << "Launch Type       : " << (int)launch_cfg.type;
                qDebug() << "Description       : " << launch_cfg.description;
                qDebug() << "Operating System  : " << (int)launch_cfg.platforms;
                // qDebug() << "CPU Architecture  : " << (int) launch_cfg.cpu_type;
                // qDebug() << "Beta key          : " <<       launch_cfg.beta_key;
                qDebug() << "Owns DLC          : " << launch_cfg.owns_dlc;

                // #endif
            }
        }

        // If the appinfo.vdf didn't contain library asset paths, default to standard path
        // game->common_config.boxart_hash = "library_600x900.jpg";

        // At this point, since we used Steam's index as the position to stuff data into, the vector has
        //   has objects all over -- some at 0, some at 1, others at 0-4, not 5, 6-9. Basically we cannot
        //     be certain of anything, at all, what so bloody ever! So let's just recreate the std::map!

        QList<AppRecord::LaunchConfig> _cleaner;

        // Put away put away put away put away
        for (auto &keep : game->launch_configs)
            _cleaner.push_back(keep);

        // Clean clean clean clean!
        game->launch_configs.clear();

        // Restore restore restore restore!
        for (auto &launch : _cleaner)
        {
            // Reset SKIF's internal identifer for the launch configs
            launch.id = static_cast<int>(game->launch_configs.size());

            // Add it back
            game->launch_configs.insert(static_cast<int>(game->launch_configs.size()), launch);
        }
        // std::map is now reliable again!

        QSet<QString> _used_executables;
        QSet<QString> _used_executables_arguments;
        std::vector<AppRecord::LaunchConfig> _launches;

        // Now add in our custom launch configs!
        for (auto &custom_cfg : game->launch_configs_custom)
        {
            custom_cfg.id = static_cast<int>(game->launch_configs.size());

            // Custom launch config, so Steam launch ID is nothing we use
            custom_cfg.id_steam = -1;

            if (game->launch_configs.contains(custom_cfg.id))
                qCritical() << "Failed adding a custom launch config to the launch map. An element at that position "
                               "already exists!";
            else
                game->launch_configs.insert(custom_cfg.id, custom_cfg);
        }

        // Clear out the custom launches once they've been populated
        game->launch_configs_custom.clear();

        for (auto &launch_cfg : game->launch_configs)
        {
            auto &launch = launch_cfg;

            // Summarize the branches in a comma-delimited string as well
            if (!launch.branches.empty())
            {
                for (auto const &branch : launch.branches)
                    launch.branches_joined += branch + ", ";

                // Remove trailing comma and space
                launch.branches_joined.chop(2);
            }

            // Filter launch configurations for other OSes
            // if ((!app_record_s::supports(launch.platforms, app_record_s::Platform::Windows)))
            // continue;

            // Filter launch configurations lacking an executable
            // This also skips all the semi-weird duplicate launch configs SKIF randomly creates
            if (launch.executable.isEmpty())
                continue;

            // Filter launch configurations requiring a beta branch that the user do not have
            // if (!launch.branches.empty() && launch.branches.count(game->steam.branch) == 0)
            // launch.valid = 0;

            // File extension, so we can flag out non-executable ones (e.g. link2ea)
            // TODO: is this still valid on Linux?
            QFileInfo launchExecutable{launch.executable};
            if (launchExecutable.suffix() != "exe"_L1)
                launch.valid = 0;

            // Clean valid executable paths
            if (launch.valid)
                launch.executable = QString::fromStdWString(
                            std::filesystem::path(launch.executable.toStdString()).lexically_normal().wstring());

            // Flag duplicates...
            //   but not if we found them as not valid above (as that would filter out custom launch configs that
            //   matches beta launch configs...)
            if (launch.isExecutableFileNameValid() && launch.valid)
            {
                // Use the executable to identify duplicates (affects Disable Special K menu)
                if (_used_executables.contains(launch.getExecutableFileName()))
                    launch.duplicate_exe = true;
                else
                    _used_executables.insert(launch.getExecutableFileName());

                // Use a combination of the executable and arguments to identify duplicates (affects Instant Play
                // menu)
                if (_used_executables_arguments.contains(launch.getExecutableFileName() + launch.getLaunchOptions()))
                    launch.duplicate_exe_args = true;
                else
                    _used_executables_arguments.insert(launch.getExecutableFileName() + launch.getLaunchOptions());
            }

            // Working dir = empty, ., / or \ all need fixups
            // if (wcslen(launch.working_dir.c_str()) < 2)
            // {
            //     if (launch.working_dir[0] == '/' || launch.working_dir[0] == '.' ||
            //             launch.working_dir[0] == '\0')
            //     {
            //         launch.working_dir.clear();
            //     }
            // }

            // Fix working directories
            // TODO: Test this out properly with games with different working directories set!
            //       See if there's any games that uses different working directories between launch configs, but
            //       otherwise the same executable and cmd-line arguments!
            if (!launch.working_dir.isEmpty())
            {
                QString wd = game->installDir() + '/' + launch.working_dir;
                launch.working_dir = game->installDir() + '/' + launch.working_dir;
                // launch.working_dir = std::filesystem::path(launch.working_dir).lexically_normal();
            }

            // Flag the launch config to be added back
            _launches.push_back(launch);
        }

        game->launch_configs.clear();

        for (auto &launch : _launches)
        {
            // Reset SKIF's internal identifer for the launch configs
            launch.id = static_cast<int>(game->launch_configs.size());

            // #if 0

            qDebug() << "SKIF  Launch ID   : " << launch.id;
            qDebug() << "Steam Launch ID   : " << launch.id_steam;
            qDebug() << "Executable        : " << launch.executable;
            qDebug() << "Arguments         : " << launch.launch_options;
            qDebug() << "Working Directory : " << launch.working_dir;
            qDebug() << "Launch Type       : " << (int)launch.type;
            qDebug() << "Description       : " << launch.description;
            qDebug() << "Operating System  : " << (int)launch.platforms;
            // qDebug() << "CPU Architecture  : " << (int) launch.cpu_type;
            // qDebug() << "Beta key          : " <<       launch.beta_key;
            qDebug() << "Owns DLC          : " << launch.owns_dlc;

            // #endif

            // Add it back
            game->launch_configs.insert(static_cast<int>(game->launch_configs.size()), launch);
        }

        // Naively move the first Default launch config type to the front
        int firstValidFound = -1;

        for (const auto &[key, launch] : game->launch_configs.asKeyValueRange())
        {
            // Ignore launch options requiring a beta key
            // TODO: Contemplate this choice now that SKIF can read current beta!
            if (!launch.branches.empty())
                continue;

            if (launch.type == AppRecord::LaunchConfig::Type::Default)
            {
                firstValidFound = key;
                break;
            }
        }

        if (firstValidFound != -1)
        {
            AppRecord::LaunchConfig copy = game->launch_configs[0];
            game->launch_configs[0] = game->launch_configs[firstValidFound];
            game->launch_configs[firstValidFound] = copy;

            // Swap the internal identifers that SKIF uses (default becomes 0, not-default becomes not)
            int copy_id = game->launch_configs[0].id;
            game->launch_configs[0].id = 0;
            game->launch_configs[firstValidFound].id = copy_id;
        }
        // #if 0

        for (auto &launch : game->launch_configs)
        {
            qDebug() << "SKIF  Launch ID   : " << launch.id;
            qDebug() << "Steam Launch ID   : " << launch.id_steam;
            qDebug() << "Executable        : " << launch.executable;
            qDebug() << "Arguments         : " << launch.launch_options;
            qDebug() << "Working Directory : " << launch.working_dir;
            qDebug() << "Launch Type       : " << (int)launch.type;
            qDebug() << "Description       : " << launch.description;
            qDebug() << "Operating System  : " << (int)launch.platforms;
            // qDebug() << "CPU Architecture  : " << (int) launch.cpu_type;
            // qDebug() << "Beta key          : " <<       launch.beta_key;
            qDebug() << "Owns DLC          : " << launch.owns_dlc;
        }

        // #endif

        // QMap<QString, QString> roots = {{"WinMyDocuments"_L1, _path_cache.my_documents.path},
        //                                              {"WinAppDataLocal", _path_cache.app_data_local.path},
        //                                              {"WinAppDataLocalLow", _path_cache.app_data_local_low.path},
        //                                              {"WinAppDataRoaming", _path_cache.app_data_roaming.path},
        //                                              {"WinSavedGames", _path_cache.win_saved_games.path},
        //                                              {"App Install Directory", game->installDir()},
        //                                              {"gameinstall", game->installDir()},
        //                                              {"SteamCloudDocuments", L"<Steam Cloud Docs>"}};

        // std::wstring account_id_str = L"anonymous";

        // std::wstring cloud_path =
        // SK_FormatStringW(LR"(%ws\userdata\%ws\%d\)", _path_cache.steam_install, account_id_str.c_str(), appid);

        // roots["SteamCloudDocuments"] = cloud_path;

        for (auto &finished_section : section.finished_sections)
        {
            if (finished_section.keys.empty())
                continue;

            if (game != nullptr)
            {
                // if (populate_branches && finished_section.name.startsWith("appinfo.depots.branches."))
                // {
                //     std::string branch_name = finished_section.name.substr(24);

                //     auto *branch_ptr = &game->branches[branch_name];

                //     static const std::unordered_map<std::string, ptrdiff_t> int_map = {
                //         {"buildid", offsetof(app_record_s::branch_record_s, build_id)},
                //         {"pwdrequired", offsetof(app_record_s::branch_record_s, pwd_required)},
                //         {"timeupdated", offsetof(app_record_s::branch_record_s, time_updated)}};

                //     static const std::unordered_map<std::string, ptrdiff_t> str_map = {
                //         {"description", offsetof(app_record_s::branch_record_s, description)}};

                //     int matches = 0;

                //     for (auto &key : finished_section.keys)
                //     {
                //         auto pInt = int_map.find(key.first);

                //         if (pInt != int_map.cend())
                //             *(uint32_t *)VOID_OFFSET(branch_ptr, pInt->second) = *(uint32_t *)key.second.second,
                //             ++matches;

                //         else
                //         {
                //             auto pStr = str_map.find(key.first);

                //             if (pStr != str_map.cend())
                //             {
                //                 *(std::wstring *)VOID_OFFSET(branch_ptr, pStr->second) =
                //                         QString::fromStdString((char *)key.second.second).toStdWString(),
                //                         ++matches;
                //             }
                //         }
                //     }

                //     if (matches > 0)
                //         game->branches[branch_name].parent = game;
                //     else
                //         game->branches.erase(branch_name);
                // }
            }
        }

        if (game != nullptr)
        {
            std::set<std::wstring> _used_paths;

            // Steam requires we resolve executable_path here as well
            for (auto &launch_cfg : game->launch_configs)
            {
                if (!game->installDir().isEmpty())
                {
                    launch_cfg.install_dir = game->installDir();

                    // EA games using link2ea:// protocol handlers to launch games does not have an executable,
                    //  so this ensures we do not end up testing the installation folder instead (since this has
                    //   bearing on whether a launch config is deemed valid or not as part of the blacklist check)
                    if (launch_cfg.isExecutableFileNameValid())
                    {
                        launch_cfg.executable_path = launch_cfg.install_dir;
                        launch_cfg.executable_path += '/';
                        launch_cfg.executable_path.append(launch_cfg.getExecutableFileName());
                    }
                }

                // Populate empty launch descriptions as well
                if (launch_cfg.description.isEmpty())
                {
                    launch_cfg.description = game->name();
                    launch_cfg.description_utf8 = game->name();
                }
            }
        }

        // qDebug() << game->name();

        return pIter;
    }

    return nullptr;
}

// Shorthand, because these are way too long
using AppLaunchConfig = AppRecord::LaunchConfig;
using AppBranchRecord = AppRecord::BranchRecord;

QString AppLaunchConfig::getExecutableFileName(void)
{
    if (!executable.isEmpty())
        return executable;

    // EA games using link2ea:// protocol handlers to launch games does not have an executable,
    //  so this ensures we do not end up testing the installation folder instead (since this has
    //   bearing on whether a launch config is deemed valid or not as part of the blacklist check)
    executable_valid = 0;
    executable = "<InvalidPath>"_L1;

    return executable;
}

bool AppLaunchConfig::isExecutableFileNameValid(void)
{
    if (executable_valid != -1)
        return executable_valid;

    executable_valid =
            (!executable.isEmpty() && !executable.contains("InvalidPath"_L1) && !executable.contains("link2ea"_L1));

    if (executable_valid == 0)
    {
        executable_path_valid = 0;
        valid = 0;
        executable = "<InvalidPath>"_L1;
    }

    return executable_valid;
}

QString AppLaunchConfig::getLaunchOptions(void) const
{
    return launch_options;
}
