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

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <set>
#include <map>

#include <QByteArray>
#include <QString>

class Game;

// Steamworks API definitions
typedef uint32_t AppId_t;
typedef uint32_t DepotId_t;
typedef uint64_t PublishedFileId_t;
typedef uint64_t ManifestId_t;
typedef uint64_t UGCHandle_t;

struct AppRecord
{
    AppRecord(uint32_t id_)
        : id(id_) {};

    enum class Platform
    {
        Unknown = 0x0,
        Windows = 0x1,
        Linux = 0x2,
        Mac = 0x4,
        All = 0xffff
    };

    struct CommonConfig
    {
        enum class AppType
        { // Used by Steam to indicate the type of "app" we're dealing with
            Unknown = 0x0, // Default in SKIF
            Game = 0x1, // game
            Application = 0x2, // application
            Tool = 0x3, // tool
            Music = 0x4, // music
            Demo = 0x5, // demo
            Unspecified = 0xffff // not expected to be seen
        };

        uint32_t appid = 0;
        // important! It's either not set, empty, or set to 64 -- nothing else!
        AppType type = AppType::Unknown; // Steam
        QString icon_hash; // Steam's internal name for the icon (40 digits)
        QString boxart_hash; // Alternate filesystem location for boxart
    };

    struct LaunchConfig
    {
        enum class Type
        {
            Default = 0x0, // default - Launch (Default)
            Option1 = 0x1, // option1 - Play %%description%% (previously Config)
            Option2 = 0x2, // option2 - Play %%description%%
            Option3 = 0x3, // option3 - Play %%description%%
            Unspecified = 0xffff // none    - Unspecified
        };

        int id = 0; // SKIF's internal launch identifier
        int id_steam = 0; // Steam's internal launch identifer

        Type type = Type::Unspecified;
        Platform platforms = Platform::All;

        QString getExecutableFileName(void);
        bool isExecutableFileNameValid(void);

        QString getLaunchOptions(void) const;

        // private:
        // AppRecord* parent = nullptr;

        QString executable;
        QString executable_utf8;
        int executable_valid = -1;

        QString executable_path;
        QString executable_path_utf8;
        int executable_path_valid = -1;

        QString install_dir;
        QString Xbox_ApplicationId; // e.g. HaloMCCShipping or HaloMCCShippingNoEAC

        QString description;
        QString description_utf8;
        QString launch_options;
        QString launch_options_utf8;
        QString working_dir;
        QString working_dir_utf8;
        QString blacklist_file;
        QString elevated_file;
        QString executable_helper; // Used by Xbox to hold gamelaunchhelper.exe
        std::set<QString> branches; // Steam: Only show launch option if one of these beta branches are active
        QString branches_joined;
        QString requires_dlc; // Steam: Only show launch option if this DLC is owned

        int valid = -1; // Launch config is valid (what does this actually mean?)
        bool duplicate_exe = false; // Used for Steam games indicating that a launch option is a duplicate (shares the same
        // executable as another)
        bool duplicate_exe_args = false; // Used for Steam games indicating that a launch option is a duplicate (shares the
        // same executable and arguments as another)
        bool custom_skif = false; // Is the launch config an online-based custom one populated by SKIF ?
        bool custom_user = false; // Is the launch config a user-specied custom one?
        int owns_dlc = -1; // Does the user own the required DLC ?
        int blacklisted = -1;
        int elevated = -1;
    };

    struct BranchRecord
    {
        AppRecord *parent;
        uint32_t build_id;
        uint32_t pwd_required;
        time_t time_updated;
        QString description;
        QString description_utf8; // For non-Latin characters to print in ImGui
        QString time_string; // Cached text representation
        QString time_string_utf8; // Cached text representation
    };

    std::map<QString, BranchRecord> branches;

    uint32_t id;
};

#pragma pack(push)
#pragma pack(1)
class ValveDataFile
{
public:
    ValveDataFile(QString source);
    static ValveDataFile *instance() { return s_instance; }

    static constexpr uint32_t _LastSteamApp = 0;

    static uint32_t vdf_version;

    struct AppInfo27
    {
        AppId_t appid;
        uint32_t size;
        uint32_t state;
        uint32_t last_update;
        uint64_t access_token;
        uint8_t sha1sum[20];
        uint32_t change_num;
    };

    struct AppInfo28 : AppInfo27
    {
        uint8_t sha1_sec[20]; // Added December 2022
    };

    struct AppInfo : AppInfo28
    {
        struct SectionDesc
        {
            void *blob;
            size_t size;
        };

        struct Section
        {
            enum _TokenOp
            {
                SectionBegin = 0x0,
                String = 0x1,
                Int32 = 0x2,
                Int64 = 0x7,
                SectionEnd = 0x8
            };

            using _kv_pair = std::pair<const char *, std::pair<_TokenOp, void *>>;

            struct section_data_s
            {
                QString name;
                SectionDesc desc;
                std::vector<_kv_pair> keys;
            };

            std::vector<section_data_s> finished_sections;

            void parse(SectionDesc &desc);
        };

        void *getRootSection(size_t *pSize = nullptr);
        AppInfo *getNextApp(void);
    };

    AppInfo *getAppInfo(Game *game);

    struct header_s
    {
        uint32_t version;
        uint32_t universe;
        // String table pointer (not shown here) added in 0x29
        AppInfo head;
    };

    // Added in June 2024 (version 0x29)
    struct str_tbl_s
    {
        uint32_t num_strings;
        uint8_t strings[1];
    };

    header_s *base = nullptr;
    AppInfo *root = nullptr;
    str_tbl_s *table = nullptr;

protected:
private:
    static ValveDataFile *s_instance;

    QString path;
    QByteArray _data;
    std::vector<char *> strs; // Preparsed array of pointers
};
#pragma pack(pop)
