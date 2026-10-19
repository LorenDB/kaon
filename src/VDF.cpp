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

#include <limits>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QStandardPaths>

#include "stores/Steam.h"

Q_LOGGING_CATEGORY(VDFLog, "vdf")

namespace
{
    constexpr uint32_t LAST_STEAM_APP = 0;
    // One app's binary KV. Larger than this is a corrupt size field; scanning it walks off the record
    // and looks like a hang on that app id.
    constexpr size_t MaxAppKvBytes = 64 * 1024 * 1024;

    // Which library's string table Section::parse should read. appinfo.vdf v0x29 keeps
    // strings in a table that belongs to that file, and two Steam installs can differ.
    AppInfoVDF *g_parsing = nullptr;
    QList<AppInfoVDF *> g_libraries;

    AppInfoVDF *currentLibrary()
    {
        return g_parsing != nullptr ? g_parsing : AppInfoVDF::instance();
    }
} // namespace

uint32_t AppInfoVDF::vdf_version = 0x27; // Default to Pre-December 2022

QRecursiveMutex &appInfoVdfMutex()
{
    static QRecursiveMutex mutex;
    return mutex;
}

AppInfoVDF::AppInfoVDF(const QString &path)
    : m_appInfoPath{path}
{
    if (path.isEmpty() || !QFileInfo::exists(m_appInfoPath))
        return;

    refresh();
}

void AppInfoVDF::refresh()
{
    const QFileInfo fi{m_appInfoPath};
    if (!fi.exists() || !fi.isFile())
        return;
    // Same size and mtime means Steam has not replaced the cache since we read it.
    if (base != nullptr && fi.size() == m_loadedSize && fi.lastModified() == m_loadedMtime)
        return;

    QFile dataFile{m_appInfoPath};
    if (!dataFile.open(QIODevice::ReadOnly))
        return;
    QByteArray data = dataFile.readAll();
    if (data.isEmpty())
        return;

    const bool hadCopy = base != nullptr;
    QByteArray previous = std::move(m_data);
    const auto *previousBase = base;
    const auto *previousRoot = root;
    const auto *previousTable = table;
    const auto previousStrs = m_strs;
    const auto previousVersion = m_fileVersion;

    m_data = std::move(data);
    m_strs.clear();
    base = nullptr;
    root = nullptr;
    table = nullptr;
    if (!adoptBuffer())
    {
        m_data = std::move(previous);
        base = const_cast<Header *>(previousBase);
        root = const_cast<AppInfo *>(previousRoot);
        table = const_cast<StringTable *>(previousTable);
        m_strs = previousStrs;
        m_fileVersion = previousVersion;
        vdf_version = m_fileVersion;
        if (hadCopy)
            qCWarning(VDFLog) << "appinfo.vdf changed but could not be parsed; keeping the previous copy:" << m_appInfoPath;
        else
            qCWarning(VDFLog) << "appinfo.vdf could not be parsed:" << m_appInfoPath;
        return;
    }

    m_loadedSize = m_data.size();
    m_loadedMtime = fi.lastModified();
    m_gameIndex.clear();
    m_gameIndexValid = false;
    if (hadCopy)
        qCInfo(VDFLog) << "Reloaded appinfo" << m_appInfoPath << "version" << m_fileVersion;
}

bool AppInfoVDF::adoptBuffer()
{
    if (m_data.size() < static_cast<qsizetype>(sizeof(Header)))
        return false;

    base = reinterpret_cast<Header *>(m_data.data());
    m_fileVersion = (reinterpret_cast<uint8_t *>(&base->version))[0];
    vdf_version = m_fileVersion;
    root = &base->head;
    table = nullptr;
    m_strs.clear();

    // A string table was added in June of 2024 (0x29)
    if (m_fileVersion < 0x29)
        return true;

    if (static_cast<size_t>(m_data.size()) < sizeof(uint64_t))
        return false;
    const auto strtable_pos = static_cast<uintptr_t>(*reinterpret_cast<uint64_t *>(root));
    if (strtable_pos >= static_cast<uintptr_t>(m_data.size()) ||
        strtable_pos + sizeof(uint32_t) > static_cast<uintptr_t>(m_data.size()))
        return false;

    root = reinterpret_cast<AppInfo *>(reinterpret_cast<uint64_t *>(root) + 1);
    table = reinterpret_cast<StringTable *>(&m_data[static_cast<qsizetype>(strtable_pos)]);

    // Valve is using 64-bit offsets, if this file is larger than
    // 4 GiB SKIF32 is fundamentally inoperable!
    if (*reinterpret_cast<uint64_t *>(root) > std::numeric_limits<uintptr_t>::max())
    {
        qCritical() << "VDF File is Too Large!";
        return false;
    }

    if (table->num_strings == 0)
        return true;

    m_strs.reserve(table->num_strings);
    m_strs.push_back(reinterpret_cast<char *>(table->strings));

    char *str = reinterpret_cast<char *>(table->strings);
    char *end_tbl = m_data.data() + m_data.size();

    for (uint32_t i = 1; i < table->num_strings; ++i)
    {
        while (str < end_tbl && *str++ != '\0')
            ;

        if (str > end_tbl)
        {
            qCritical() << "Malformed string table detected!";
            break;
        }

        m_strs.push_back(str);
    }
    return true;
}

void AppInfoVDF::dumpAppInfo()
{
    QMutexLocker lock{&appInfoVdfMutex()};
    g_parsing = this;
    vdf_version = m_fileVersion;

    qCInfo(VDFLog) << "Dumping app info to"
                   << QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/appinfo/"_L1;

    QDir d{QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/appinfo/"_L1};
    if (d.exists())
        d.removeRecursively();

    AppInfoVDF::AppInfo *info = root;
    while (info && info->appid != LAST_STEAM_APP)
    {
        QDir{QStandardPaths::writableLocation(QStandardPaths::CacheLocation)}.mkdir("appinfo"_L1);

        QFile f{QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/appinfo/"_L1 +
                QString::number(info->appid)};
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
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
        }

        info = info->getNextApp();
    }
}

void AppInfoVDF::AppInfo::Section::parse(SectionDesc &desc)
{
    if (desc.blob == nullptr || desc.size == 0)
        return;

    // Stay inside this app's blob. A missing NUL or an unknown token used to keep reading, and a
    // static flag then made every later app skip parsing for the rest of the process.
    const auto *const end = static_cast<const uint8_t *>(desc.blob) + desc.size;
    auto *cur = static_cast<uint8_t *>(desc.blob);
    std::vector<SectionData> raw_sections;

    while (cur < end)
    {
        const auto op = static_cast<_TokenOp>(*cur);
        const char *name = reinterpret_cast<const char *>(cur + 1);
        if (op != SectionEnd)
        {
            // String Table Lookup (June 2024+)
            if (currentLibrary()->vdf_version >= 0x29)
            {
                if (cur + 5 > end || currentLibrary()->table == nullptr)
                    return;
                name = reinterpret_cast<char *>(currentLibrary()->table->strings);

                const auto str_idx = *reinterpret_cast<uint32_t *>(cur + 1);

#ifdef DEBUG
                qCDebug(VDFLog) << "String Table Index:  " << str_idx << ", op=" << op;
#endif

                if (str_idx < currentLibrary()->m_strs.size())
                {
                    name = currentLibrary()->m_strs[static_cast<qsizetype>(str_idx)];
#ifdef DEBUG
                    qCDebug(VDFLog) << "String=" << name;
#endif
                }
                else
                    qCritical() << "String Table Index (" << str_idx << ") Out-of-Range!";

                cur += 4;
            }
            // Legacy: null-terminated name is serialized inline after token type.
            // SectionEnd has no name.
            else
            {
                if (cur + 1 >= end)
                    return;
                ++cur;
                while (cur < end && *cur != '\0')
                    ++cur;
                if (cur >= end)
                    return;
            }
        }

        if (op == SectionBegin)
        {
            if (!raw_sections.empty())
                raw_sections.push_back({raw_sections.back().name + '.' + name, {cur, 0}});
            else
                raw_sections.push_back({name, {cur, 0}});
        }
        else if (op == SectionEnd)
        {
            if (!raw_sections.empty())
            {
                raw_sections.back().desc.size = static_cast<size_t>(
                    reinterpret_cast<uintptr_t>(cur) - reinterpret_cast<uintptr_t>(raw_sections.back().desc.blob));
                finished_sections.push_back(raw_sections.back());
                raw_sections.pop_back();
            }
        }
        else
        {
            if (cur + 1 >= end)
                return;
            ++cur;

            auto width = size_t{0};
            switch (op)
            {
            case String:
                if (!raw_sections.empty())
                    raw_sections.back().keys.push_back({name, {String, cur}});
                while (cur < end && *cur != '\0')
                    ++cur;
                if (cur >= end)
                    return;
                break;

            case Int32:
                width = sizeof(int32_t);
                break;
            case Int64:
                width = sizeof(int64_t);
                break;

            default:
                qWarning() << "Unknown VDF Token Operator: " << static_cast<int>(op);
                return;
            }

            if (op == Int32 || op == Int64)
            {
                if (cur + width > end)
                    return;
                if (!raw_sections.empty())
                    raw_sections.back().keys.push_back({name, {op, cur}});
                cur += width - 1;
            }
        }

        if (cur >= end)
            return;
        ++cur;
    }
}

void *AppInfoVDF::AppInfo::getRootSection(size_t *pSize)
{
    const size_t vdf_header_size = (vdf_version > 0x27 ? sizeof(AppInfo) : sizeof(AppInfo27));

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

    // `size` counts the bytes after the appid and size fields, so the record is size + 8 bytes from appid.
    // Subtracting an unsigned header larger than that used to wrap and parse until the process fell over.
    size_t kv_size = 0;
    if (static_cast<size_t>(size) + 8 >= vdf_header_size)
        kv_size = static_cast<size_t>(size) + 8 - vdf_header_size;
    if (kv_size > MaxAppKvBytes)
        kv_size = 0;

    if (pSize != nullptr)
        *pSize = kv_size;

    return reinterpret_cast<uint8_t *>(&appid) + vdf_header_size;
}

AppInfoVDF::AppInfo *AppInfoVDF::AppInfo::getNextApp(void)
{
    const size_t header = vdf_version > 0x27 ? sizeof(AppInfo) : sizeof(AppInfo27);
    const auto record = static_cast<size_t>(size) + 8;
    // A zero-length step here loops on the same app forever.
    if (record < header || record > header + MaxAppKvBytes)
        return nullptr;

    auto *pNext = reinterpret_cast<AppInfo *>(reinterpret_cast<uint8_t *>(&appid) + record);
    return pNext->appid == LAST_STEAM_APP ? nullptr : pNext;
}

AppInfoVDF *AppInfoVDF::load(const QString &path)
{
    QMutexLocker lock{&appInfoVdfMutex()};
    if (path.isEmpty() || !QFileInfo::exists(path))
        return nullptr;

    const auto canonical = QFileInfo{path}.canonicalFilePath();
    if (canonical.isEmpty())
        return nullptr;

    for (auto *library : g_libraries)
    {
        if (library->m_appInfoPath == canonical)
        {
            library->refresh();
            return library->base != nullptr ? library : nullptr;
        }
    }

    auto *library = new AppInfoVDF{canonical};
    if (library->base == nullptr)
    {
        delete library;
        return nullptr;
    }

    g_libraries.push_back(library);
    qCInfo(VDFLog) << "Loaded appinfo" << canonical << "version" << library->m_fileVersion;
    return library;
}

AppInfoVDF *AppInfoVDF::instance()
{
    if (const auto rootPath = Steam::instance()->storeRoot(); !rootPath.isEmpty())
        if (auto *library = load(rootPath + "/appcache/appinfo.vdf"_L1))
            return library;
    if (!g_libraries.isEmpty())
        return g_libraries.constFirst();

    static auto *empty = new AppInfoVDF{QString{}};
    return empty;
}

AppInfoVDF::AppInfo *AppInfoVDF::game(int steamId)
{
    QMutexLocker lock{&appInfoVdfMutex()};
    if (g_libraries.isEmpty())
        instance();

    for (auto *library : g_libraries)
    {
        // The file holds every app Steam knows about, so a linear walk per installed game made scans
        // quadratic. Index each library once and look the id up instead.
        if (!library->m_gameIndexValid)
        {
            library->m_gameIndex.clear();
            const auto *const fileBegin = library->m_data.constData();
            const auto *const fileEnd = fileBegin + library->m_data.size();
            const size_t header = library->m_fileVersion > 0x27 ? sizeof(AppInfo) : sizeof(AppInfo27);
            for (auto *info = library->root;;)
            {
                const auto *at = reinterpret_cast<const char *>(info);
                if (at < fileBegin || at + sizeof(uint32_t) * 2 > fileEnd)
                    break;
                if (info->appid == LAST_STEAM_APP)
                    break;

                const auto record = static_cast<size_t>(info->size) + 8;
                if (record < header || record > header + MaxAppKvBytes || at + record > fileEnd)
                {
                    qCWarning(VDFLog) << "Stopped reading appinfo at app" << info->appid << "in" << library->m_appInfoPath
                                      << "- that record is truncated or larger than expected. Later apps in this file "
                                         "are skipped.";
                    break;
                }
                if (!library->m_gameIndex.contains(info->appid))
                    library->m_gameIndex.insert(info->appid, info);
                info = reinterpret_cast<AppInfo *>(const_cast<char *>(at + record));
            }
            library->m_gameIndexValid = true;
        }
        if (auto *info = library->m_gameIndex.value(static_cast<AppId_t>(steamId), nullptr))
        {
            g_parsing = library;
            vdf_version = library->m_fileVersion;
            return info;
        }
    }
    return nullptr;
}
