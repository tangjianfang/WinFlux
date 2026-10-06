#include <flux/Settings.hpp>

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace flux::settings {
namespace {

namespace fs = std::filesystem;

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                        static_cast<int>(w.size()),
                                        nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                        static_cast<int>(s.size()),
                                        nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), len);
    return out;
}

std::wstring trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

} // namespace

Kv load(const std::wstring& path) {
    Kv kv;
    std::error_code ec;
    const fs::path p{path};
    if (!fs::exists(p, ec) || ec) return kv;

    std::ifstream in{p, std::ios::binary};
    if (!in) return kv;

    std::stringstream buf;
    buf << in.rdbuf();
    std::wstring text = to_wide(buf.str());
    if (!text.empty() && text[0] == L'﻿') {
        text.erase(0, 1);   // our own save() writes a BOM — don't glue it to the first key
    }

    std::wstring line;
    for (size_t pos = 0; pos <= text.size();) {
        const size_t nl = text.find(L'\n', pos);
        line = text.substr(pos, nl == std::wstring::npos
                                    ? std::wstring::npos : nl - pos);
        pos = nl == std::wstring::npos ? text.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        if (line.empty() || line[0] == L'#' || line[0] == L';') continue;
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;   // malformed line: skip, keep going
        const std::wstring key = trim(line.substr(0, eq));
        if (key.empty()) continue;
        put(kv, key, trim(line.substr(eq + 1)));
    }
    return kv;
}

bool save(const std::wstring& path, const Kv& kv) {
    std::error_code ec;
    const fs::path p{path};
    const fs::path tmp = p.wstring() + L".tmp";

    {
        std::ofstream out{tmp, std::ios::binary | std::ios::trunc};
        if (!out) return false;
        out.write("\xEF\xBB\xBF", 3);   // BOM: notepad round-trips
        for (const auto& [k, v] : kv) {
            out << to_utf8(k) << " = " << to_utf8(v) << "\n";
        }
        out.flush();
        if (!out.good()) return false;
    }
    if (!MoveFileExW(tmp.c_str(), p.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

std::optional<std::wstring> get(const Kv& kv, const std::wstring& key) {
    for (const auto& [k, v] : kv) {
        if (k == key) return v;
    }
    return std::nullopt;
}

void put(Kv& kv, const std::wstring& key, std::wstring value) {
    for (auto& [k, v] : kv) {
        if (k == key) {
            v = std::move(value);
            return;
        }
    }
    kv.emplace_back(key, std::move(value));
}

int get_int(const Kv& kv, const std::wstring& key, int fallback) {
    const auto v = get(kv, key);
    if (!v.has_value() || v->empty()) return fallback;
    try {
        size_t used = 0;
        const long parsed = std::stol(*v, &used);
        if (used != v->size()) return fallback;
        return static_cast<int>(parsed);
    } catch (...) {
        return fallback;
    }
}

void put_int(Kv& kv, const std::wstring& key, int value) {
    put(kv, key, std::to_wstring(value));
}

} // namespace flux::settings
