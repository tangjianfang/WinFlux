#pragma once

// Persistent settings primitives (review 2026-10-06, F5). WinFlux itself
// has no configuration surface — this is the opt-in building block apps use
// to remember window placement, theme, and their own State across runs.
//
// Files are `key = value` lines with `#` comments, stored as UTF-8 with a
// BOM. Loading is corrupt-tolerant in the nfui tradition: an unreadable or
// malformed file yields an empty or partial table, never a throw. Saving is
// atomic (temp file + MoveFileEx replace) so a crash mid-write can't destroy
// the previous settings.

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace flux::settings {

using Kv = std::vector<std::pair<std::wstring, std::wstring>>;

// Corrupt-tolerant load. `path` may not exist (fresh install) — that is an
// empty table, not an error.
[[nodiscard]] Kv load(const std::wstring& path);

// Atomic save: writes `path` + ".tmp", flushes, then replaces `path`.
// Returns false (without throwing) when the file can't be written.
[[nodiscard]] bool save(const std::wstring& path, const Kv& kv);

[[nodiscard]] std::optional<std::wstring> get(const Kv& kv, const std::wstring& key);
void put(Kv& kv, const std::wstring& key, std::wstring value);
[[nodiscard]] int get_int(const Kv& kv, const std::wstring& key, int fallback);
void put_int(Kv& kv, const std::wstring& key, int value);

} // namespace flux::settings
