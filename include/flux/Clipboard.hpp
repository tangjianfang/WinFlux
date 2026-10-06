#pragma once

// Text clipboard primitives (review 2026-10-06, F1). CF_UNICODETEXT only —
// the cases a text input needs (copy/cut/paste). Ownership of the clipboard
// is taken just long enough to read/write, so these are safe to call from a
// key handler; a busy clipboard yields empty/false rather than hanging.

#include <string>

namespace flux {

// Returns an empty string when the clipboard is unavailable or holds no
// text (or the text is not representable — never throws).
[[nodiscard]] std::wstring clipboard_get_text();

// Returns false when the clipboard could not be opened or the data
// could not be handed over.
[[nodiscard]] bool clipboard_set_text(const std::wstring& text);

} // namespace flux
