#pragma once
//
// The "formula" condition: one formula in the style of the game's txt calcs,
// compiled once when the filter loads and worked out for every item.
//
//     ((stat('item_fastercastrate')>=10)&&((stat('fireresist')+stat('coldresist'))>=40))
//
// Whole numbers. Comparisons give 1 or 0, and a result other than 0 means the
// condition holds. Operators, lowest first: ?:  ||  &&  == !=  < <= > >=  + -
// * /  and the unary ! and -. Division by 0 gives 0.
//
// Stat calls. A stat is an itemstatcost.txt name in single quotes, or its row
// number; every value is the number the item tooltip prints (stat_values.h):
//     stat('name')            the item's stat (param 0)
//     stat('name', param)     a param: a whole number, or a skills.txt skill name
//                             such as 'Teleport'
//     playerStat('name'), playerStat('name', param)   your character's stat
//     statMax('name')         the highest value over every param of the item's stat
//     statSum('name')         the total over every param
// For stats whose param holds a skill and its level (itemstatcost Encode 2 and
// 3: chance-to-cast and charges), a skill name matches that skill at any level.
//
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lootfilter {

struct FormulaStat {
    enum class Source : std::uint8_t { Item, Player };
    enum class Mode : std::uint8_t { Exact, Max, Sum, SkillAnyLevel };
    Source        source{ Source::Item };
    Mode          mode{ Mode::Exact };
    int           stat{ -1 };   // itemstatcost.txt row; set by ResolveNames when written as a name
    std::string   statName;
    int           param{};      // the param, or the skills.txt row of skillName
    std::string   skillName;    // a param written as a skill name, resolved by ResolveNames
    std::uint32_t column{};     // where the call starts in the formula, for messages
};

struct FormulaNode {
    enum class Kind : std::uint8_t {
        Number, Stat, Negate, Not, Multiply, Divide, Add, Subtract,
        Less, LessOrEqual, Greater, GreaterOrEqual, Equal, NotEqual, And, Or, Choose,
    };
    Kind         kind{ Kind::Number };
    std::int64_t number{};
    int          a{ -1 };  // operands: node indices; for Stat, `a` indexes `stats`
    int          b{ -1 };
    int          c{ -1 };
};

struct Formula {
    std::vector<FormulaNode> nodes;
    std::vector<FormulaStat> stats;
    int                      root{ -1 };
    bool                     readsPlayer{};
};

// Supplies the value of each stat call while a formula is worked out.
class FormulaValues {
public:
    virtual ~FormulaValues() = default;
    [[nodiscard]] virtual auto Value(const FormulaStat& call) const -> std::int64_t = 0;
};

// False when the text is not a formula; `error` says why and `column` (1-based) where.
auto CompileFormula(std::string_view text, Formula& out, std::string& error, std::uint32_t& column) -> bool;
auto EvaluateFormula(const Formula& formula, const FormulaValues& values) -> std::int64_t;

}  // namespace lootfilter
