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

#include <QByteArray>
#include <QList>
#include <QString>

class Game;

// Steamworks API definitions
typedef uint32_t AppId_t;

#pragma pack(push)
#pragma pack(1)
class AppInfoVDF
{
public:
    static AppInfoVDF *instance();

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

            using KeyValuePair = QPair<const char *, QPair<_TokenOp, void *>>;

            struct SectionData
            {
                QString name;
                SectionDesc desc;
                QList<KeyValuePair> keys;
            };

            QList<SectionData> finished_sections;

            void parse(SectionDesc &desc);
        };

        void *getRootSection(size_t *pSize = nullptr);
        AppInfo *getNextApp(void);
    };

    struct Header
    {
        uint32_t version;
        uint32_t universe;
        // String table pointer (not shown here) added in 0x29
        AppInfo head;
    };

    // Added in June 2024 (version 0x29)
    struct StringTable
    {
        uint32_t num_strings;
        uint8_t strings[1];
    };

    AppInfo *game(int steamId);

    // For debug purposes only - dump every app's info into files in the cache directory
    void dumpAppInfo();

private:
    AppInfoVDF();
    static AppInfoVDF *s_instance;
    static uint32_t vdf_version;

    QString m_appInfoPath;
    QByteArray m_data;
    QList<char *> m_strs; // Preparsed array of pointers

    Header *base = nullptr;
    AppInfo *root = nullptr;
    StringTable *table = nullptr;
};
#pragma pack(pop)
