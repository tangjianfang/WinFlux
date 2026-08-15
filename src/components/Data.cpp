// flux::ui data display — data table (sortable), tree view, and the
// virtualized list view (visible-window builder over scroll_view +
// Props::on_scroll).

#include <flux/Components.hpp>

#include <algorithm>

namespace flux::ui {

// ---- Virtualization window (pure) -------------------------------------------

VisibleRange visible_range(float offset_y, float viewport_h, float row_h,
                           int total_rows, int overscan) noexcept {
    if (row_h <= 0.0f || total_rows <= 0) return {0, 0};
    const int first = std::clamp(static_cast<int>(offset_y / row_h) - overscan, 0,
                                 total_rows - 1);
    const int count = std::clamp(
        static_cast<int>(viewport_h / row_h) + 1 + 2 * overscan, 0,
        total_rows - first);
    return {first, count};
}

// ---- Data table ---------------------------------------------------------------

namespace {

// Sorts a column lexicographically (v1); toggle flips the direction.
void sort_rows(std::vector<std::vector<std::wstring>>& rows, int col, bool asc) {
    std::stable_sort(rows.begin(), rows.end(), [col, asc](const auto& a, const auto& b) {
        if (col >= static_cast<int>(a.size()) || col >= static_cast<int>(b.size()))
            return false;
        return asc ? a[static_cast<size_t>(col)] < b[static_cast<size_t>(col)]
                   : b[static_cast<size_t>(col)] < a[static_cast<size_t>(col)];
    });
}

} // namespace

std::vector<std::vector<std::wstring>> table_sorted(
    const std::vector<std::vector<std::wstring>>& rows, int col, bool asc) noexcept {
    auto out = rows;
    sort_rows(out, col, asc);
    return out;
}

ElementPtr data_table(const Palette& pal, std::vector<std::wstring> columns,
                      std::vector<std::vector<std::wstring>> rows, int sort_col,
                      bool sort_asc, std::function<void(int)> on_sort,
                      int selected_row, std::function<void(int)> on_select, Props p) {
    rows = table_sorted(rows, sort_col, sort_asc);

    Props root_p = std::move(p);
    root_p.direction = Direction::column;
    auto root = view(std::move(root_p));

    // Header row: click toggles the sort.
    Props head_p;
    head_p.direction = Direction::row;
    head_p.background = pal.surface_variant;
    head_p.radius = static_cast<float>(tokens::radius_sm);
    auto head = view(std::move(head_p));
    for (size_t c = 0; c < columns.size(); ++c) {
        const int col = static_cast<int>(c);
        const bool sorted = col == sort_col;
        Props hp;
        hp.text = columns[c] + (sorted ? (sort_asc ? L"  ↑" : L"  ↓") : L"");
        hp.text_color = sorted ? pal.accent : pal.text_secondary;
        hp.font_size_pt = tokens::font_caption;
        hp.bold = sorted;
        hp.text_align = Align::start;
        hp.flex_grow = 1.0f;
        hp.padding = EdgeInsets{static_cast<float>(tokens::spacing_sm),
                                static_cast<float>(tokens::spacing_sm),
                                static_cast<float>(tokens::spacing_sm),
                                static_cast<float>(tokens::spacing_sm)};
        hp.on_click = [on_sort, col] { if (on_sort) on_sort(col); };
        hp.tooltip_text = L"Sort by " + columns[c];
        head->children.push_back(flux::button(columns[c], hp.on_click, hp));
    }
    root->children.push_back(std::move(head));

    // Body rows: zebra + selection.
    for (size_t r = 0; r < rows.size(); ++r) {
        const int row_idx = static_cast<int>(r);
        const bool is_sel = row_idx == selected_row;
        Props row_p;
        row_p.direction = Direction::row;
        row_p.background = is_sel ? std::optional<Color>{pal.selection}
                                  : (r % 2 == 1 ? std::optional<Color>{pal.surface_variant}
                                                : std::nullopt);
        row_p.on_click = [on_select, row_idx] { if (on_select) on_select(row_idx); };
        auto row_view = view(std::move(row_p));
        for (const auto& cell : rows[r]) {
            Props cp;
            cp.text = cell;
            cp.text_color = is_sel ? pal.text : pal.text;
            cp.font_size_pt = tokens::font_body;
            cp.text_align = Align::start;
            cp.flex_grow = 1.0f;
            cp.padding = EdgeInsets{static_cast<float>(tokens::spacing_sm),
                                    static_cast<float>(tokens::spacing_sm),
                                    static_cast<float>(tokens::spacing_sm),
                                    static_cast<float>(tokens::spacing_sm)};
            row_view->children.push_back(label(cell, cp));
        }
        root->children.push_back(std::move(row_view));
    }
    return root;
}

// ---- Tree view -----------------------------------------------------------------

namespace {

void push_tree_rows(const Palette& pal, const std::vector<TreeItem>& items,
                    const std::vector<int>& path,
                    const std::set<std::vector<int>>& open_set, int depth,
                    std::vector<ElementPtr>& out,
                    const std::function<void(std::vector<int>)>& on_toggle,
                    const std::function<void(std::vector<int>)>& on_select) {
    for (size_t i = 0; i < items.size(); ++i) {
        std::vector<int> child_path = path;
        child_path.push_back(static_cast<int>(i));
        const TreeItem& item = items[i];
        const bool open = open_set.count(child_path) != 0;
        const bool has_kids = !item.children.empty();

        Props row_p;
        row_p.direction = Direction::row;
        row_p.align = Align::center;
        row_p.gap = static_cast<float>(tokens::spacing_xs);
        row_p.padding = EdgeInsets{static_cast<float>(depth * tokens::spacing_md),
                                    4.0f, 4.0f, 4.0f};
        row_p.hover_background = pal.surface_hover;
        row_p.radius = static_cast<float>(tokens::radius_sm);
        row_p.on_click = [on_toggle, on_select, child_path, has_kids] {
            if (has_kids) {
                if (on_toggle) on_toggle(child_path);
            } else if (on_select) {
                on_select(child_path);
            }
        };
        auto row = view(std::move(row_p));

        if (has_kids) {
            Props chev_p;
            chev_p.width = 16.0f;
            chev_p.height = 16.0f;
            chev_p.icon = open ? IconKind::chevron_down : IconKind::chevron_right;
            chev_p.text_color = pal.text_secondary;
            row->children.push_back(flux::icon(*chev_p.icon, std::move(chev_p)));
        } else {
            Props leaf_p;
            leaf_p.width = 16.0f;
            row->children.push_back(view(std::move(leaf_p)));
        }

        Props lbl_p;
        lbl_p.text = item.label;
        lbl_p.text_color = pal.text;
        lbl_p.font_size_pt = tokens::font_body;
        lbl_p.text_align = Align::start;
        row->children.push_back(label(item.label, std::move(lbl_p)));
        out.push_back(std::move(row));

        if (open) {
            push_tree_rows(pal, item.children, child_path, open_set, depth + 1, out,
                           on_toggle, on_select);
        }
    }
}

} // namespace

ElementPtr tree_view(const Palette& pal, std::vector<TreeItem> items,
                     std::set<std::vector<int>> expanded,
                     std::function<void(std::vector<int>)> on_toggle,
                     std::function<void(std::vector<int>)> on_select, Props p) {
    Props root_p = std::move(p);
    root_p.direction = Direction::column;
    root_p.gap = 1.0f;
    auto root = view(std::move(root_p));
    push_tree_rows(pal, items, {}, expanded, 0, root->children, on_toggle, on_select);
    return root;
}

// ---- Virtualized list view -------------------------------------------------------

ElementPtr virtual_list(const Palette& pal, int total_rows, float row_h,
                        float viewport_h, float offset_y,
                        std::function<ElementPtr(int)> make_row, Props /*p*/) {
    // Visible window + leading spacer. The trailing space is implicit:
    // the content column's natural height matches the full row count
    // because the leading spacer carries the scrolled-out prefix.
    const VisibleRange vis = visible_range(offset_y, viewport_h, row_h, total_rows, 4);

    Props col_p;
    col_p.direction = Direction::column;
    auto col = view(std::move(col_p));

    if (vis.first > 0) {
        Props lead_p;
        lead_p.height = static_cast<float>(vis.first) * row_h;
        col->children.push_back(view(std::move(lead_p)));
    }
    for (int i = vis.first; i < vis.first + vis.count; ++i) {
        if (ElementPtr row = make_row(i)) col->children.push_back(std::move(row));
    }
    (void)pal;
    return col;
}

} // namespace flux::ui
