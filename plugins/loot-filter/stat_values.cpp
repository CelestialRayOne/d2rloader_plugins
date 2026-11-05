#include "stat_values.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace lootfilter {
namespace {

auto Cells(std::string_view line) -> std::vector<std::string_view> {
    std::vector<std::string_view> cells;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        cells.push_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
        if (tab == std::string_view::npos) break;
        start = tab + 1;
    }
    return cells;
}

auto Number(std::string_view text, int fallback) -> int {
    if (text.empty()) return fallback;
    bool negative = false;
    std::size_t i = 0;
    if (text[0] == '-') {
        negative = true;
        i = 1;
    }
    if (i >= text.size()) return fallback;
    long long value = 0;
    for (; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') return fallback;
        value = value * 10 + (text[i] - '0');
        if (value > 1000000) return fallback;
    }
    return static_cast<int>(negative ? -value : value);
}

auto Byte(std::string_view text) -> std::uint8_t { return static_cast<std::uint8_t>(std::clamp(Number(text, 0), 0, 255)); }

}  // namespace

auto ParseStatDisplayTable(std::string_view text, StatDisplayTable& table) -> bool {
    table = StatDisplayTable{};
    std::vector<std::vector<std::string_view>> rows;
    std::vector<std::string_view> header;
    std::size_t start = 0;
    bool first = true;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (first) {
            header = Cells(line);
            first  = false;
            continue;
        }
        std::vector<std::string_view> cells = Cells(line);
        // D2RLoader's txt compiler skips "Expansion" separator rows, so they take no row number.
        if (cells.empty() || cells[0] == "Expansion") continue;
        if (cells.size() == 1 && cells[0].empty()) continue;
        rows.push_back(std::move(cells));
    }
    const auto column = [&](std::string_view name) -> int {
        const auto it = std::find(header.begin(), header.end(), name);
        return it == header.end() ? -1 : static_cast<int>(it - header.begin());
    };
    const int stat = column("Stat"), valShift = column("ValShift"), op = column("op"), opParam = column("op param");
    const int opBase = column("op base"), descFunc = column("descfunc"), encode = column("Encode"), stuff = column("stuff");
    if (stat < 0 || valShift < 0 || op < 0 || opParam < 0 || opBase < 0 || descFunc < 0 || encode < 0) return false;
    const auto cell = [](const std::vector<std::string_view>& row, int index) -> std::string_view {
        return index >= 0 && static_cast<std::size_t>(index) < row.size() ? row[static_cast<std::size_t>(index)] : std::string_view{};
    };
    std::unordered_map<std::string, int> names;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::string_view name = cell(rows[i], stat);
        if (!name.empty()) names.emplace(std::string(name), static_cast<int>(i));
    }
    table.rows.resize(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        StatDisplay& out = table.rows[i];
        out.valShift = Byte(cell(rows[i], valShift));
        out.op       = Byte(cell(rows[i], op));
        out.opParam  = Byte(cell(rows[i], opParam));
        out.descFunc = Byte(cell(rows[i], descFunc));
        out.encode   = Byte(cell(rows[i], encode));
        const std::string_view base = cell(rows[i], opBase);
        if (!base.empty()) {
            const auto it = names.find(std::string(base));
            out.opBase = it == names.end() ? -1 : it->second;
        }
    }
    // Only row 0 counts, and the game clamps it to 1..8 (6 when it is empty).
    if (!rows.empty()) {
        const int bits = Number(cell(rows[0], stuff), 6);
        table.skillShift = std::clamp(bits, 1, 8);
    }
    return true;
}

auto TooltipNumber(const StatDisplayTable& table, int stat, std::uint32_t param, std::int32_t stored, std::int32_t baseValue) -> std::int32_t {
    const StatDisplay* row = table.Find(stat);
    if (row == nullptr || stored == 0) return stored;
    if (row->encode == 3) return static_cast<std::int32_t>(table.LevelOf(param));
    std::int32_t value = stored;
    if (row->op >= 2 && row->op <= 5) {
        if (const StatDisplay* base = table.Find(row->opBase)) {
            const std::int32_t scale = baseValue >> (base->valShift & 31);
            value = static_cast<std::int32_t>(static_cast<std::uint32_t>(value) * static_cast<std::uint32_t>(scale));
            value >>= (row->opParam & 31);
        }
    }
    value >>= (row->valShift & 31);
    if (row->descFunc == 5 || row->descFunc == 10) value = static_cast<std::int32_t>(static_cast<std::uint32_t>(value) * 100U) / 128;
    return value;
}

}  // namespace lootfilter
