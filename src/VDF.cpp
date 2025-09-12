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

#include "VDF.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTimer>

#include "stores/Steam.h"

Q_LOGGING_CATEGORY(VDFLog, "vdf")

namespace
{
    constexpr uint32_t LAST_STEAM_APP = 0;
}

AppInfoVDF *AppInfoVDF::s_instance{nullptr};

uint32_t AppInfoVDF::vdf_version = 0x27; // Default to Pre-December 2022

AppInfoVDF::AppInfoVDF()
    : m_appInfoPath{Steam::instance()->storeRoot() + "/appcache/appinfo.vdf"_L1}
{
    if (!QFileInfo::exists(m_appInfoPath))
        return; // TODO: log error

    QFile dataFile{m_appInfoPath};
    if (dataFile.open(QIODevice::ReadOnly))
    {
        m_data = dataFile.readAll();
        base = reinterpret_cast<Header *>(m_data.data());

        vdf_version = (reinterpret_cast<uint8_t *>(&base->version))[0];
        root = &base->head;

        // A string table was added in June of 2024 (0x29)
        if (vdf_version >= 0x29)
        {
            uintptr_t strtable_pos = (uintptr_t)*(uint64_t *)root;
            root = (AppInfo *)((uint64_t *)root + 1);
            table = (StringTable *)(&m_data[strtable_pos]);

            // Valve is using 64-bit offsets, if this file is larger than
            // 4 GiB SKIF32 is fundamentally inoperable!
            if (*(uint64_t *)root > std::numeric_limits<uintptr_t>::max())
                qCritical() << "VDF File is Too Large!";

            m_strs.reserve(table->num_strings);
            m_strs.push_back((char *)table->strings);

            char *str = (char *)table->strings;
            char *end_tbl = (char *)m_data.data() + m_data.size();

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

                m_strs.push_back(str);
            }
        }
    }
}

void AppInfoVDF::dumpAppInfo()
{
    qCInfo(VDFLog) << "Dumping app info to"
                   << QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/appinfo/"_L1;

    AppInfoVDF::AppInfo *info = root;
    while (info && info->appid != LAST_STEAM_APP)
    {
        QDir{QStandardPaths::writableLocation(QStandardPaths::CacheLocation)}.mkdir("appinfo"_L1);

        QFile f{QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/appinfo/"_L1 +
                QString::number(info->appid)};
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        QTextStream s{&f};

        static AppInfo::Section section;
        AppInfo::SectionDesc app_desc{};

        section.finished_sections.clear();
        app_desc.blob = info->getRootSection(&app_desc.size);
        section.parse(app_desc);

        for (auto &finished_section : section.finished_sections)
        {
            s << finished_section.name << '\n';
            for (const auto &[key, value] : std::as_const(finished_section.keys))
            {
                switch (value.first)
                {
                case AppInfo::Section::Int32:
                    s << "\ti32: "_L1 << key << " = "_L1 << *static_cast<int32_t *>(value.second);
                    break;
                case AppInfo::Section::Int64:
                    s << "\ti64: "_L1 << key << " = "_L1 << *static_cast<int64_t *>(value.second);
                    break;
                case AppInfo::Section::String:
                    s << "\tstr: "_L1 << key << " = "_L1 << static_cast<const char *>(value.second);
                    break;
                default:
                    break;
                }
                s << '\n';
            }
        }

        info = info->getNextApp();
    }
}

void AppInfoVDF::AppInfo::Section::parse(SectionDesc &desc)
{
    static std::map<_TokenOp, size_t> operand_sizes = {{Int32, sizeof(int32_t)}, {Int64, sizeof(int64_t)}};
    std::vector<SectionData> raw_sections;
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
                if (AppInfoVDF::instance()->vdf_version >= 0x29)
                {
                    name = (char *)AppInfoVDF::instance()->table->strings;

                    const auto str_idx = *(uint32_t *)(cur + 1);

#ifdef DEBUG
                    qCDebug(VDFLog) << "String Table Index:  " << str_idx << ", op=" << op;
#endif

                    if (str_idx < AppInfoVDF::instance()->table->num_strings)
                    {
                        name = AppInfoVDF::instance()->m_strs[str_idx];
#ifdef DEBUG
                        qCDebug(VDFLog) << "String=" << name;
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

void *AppInfoVDF::AppInfo::getRootSection(size_t *pSize)
{
    size_t vdf_header_size = (vdf_version > 0x27 ? sizeof(AppInfo) : sizeof(AppInfo27));

    static bool runOnce = true;
    if (runOnce)
    {
        runOnce = false;

        switch (vdf_version)
        {
        case 0x29: // v41
            qCDebug(VDFLog) << "appinfo.vdf version: " << vdf_version << " (June 2024)";
            break;
        case 0x28: // v40
            qCDebug(VDFLog) << "appinfo.vdf version: " << vdf_version << " (December 2022)";
            break;
        case 0x27: // v39
            qCDebug(VDFLog) << "appinfo.vdf version: " << vdf_version << " (pre-December 2022)";
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

AppInfoVDF::AppInfo *AppInfoVDF::AppInfo::getNextApp(void)
{
    SectionDesc root_sec{};

    root_sec.blob = getRootSection(&root_sec.size);

    auto *pNext = (AppInfo *)((uint8_t *)root_sec.blob + root_sec.size);

    return (pNext->appid == LAST_STEAM_APP) ? nullptr : pNext;
}

AppInfoVDF *AppInfoVDF::instance()
{
    if (!s_instance)
        s_instance = new AppInfoVDF;
    return s_instance;
}

AppInfoVDF::AppInfo *AppInfoVDF::game(int steamId)
{
    AppInfoVDF::AppInfo *info = root;
    while (info && info->appid != LAST_STEAM_APP)
    {
        if (info->appid == steamId)
            return info;
        info = info->getNextApp();
    }
    return nullptr;
}
