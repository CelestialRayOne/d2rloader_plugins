#include "formula.h"

#include <limits>
#include <utility>

namespace lootfilter {
namespace {

constexpr int         MaxDepth = 64;
constexpr std::size_t MaxNodes = 1024;

enum class Tok : std::uint8_t {
    End, Number, Name, Text, LParen, RParen, Comma, Question, Colon,
    Plus, Minus, Star, Slash, Bang, Less, LessEq, Greater, GreaterEq, Equal, NotEqual, And, Or, Bad,
};

struct Token {
    Tok           kind{ Tok::End };
    std::int64_t  number{};
    std::string   text;
    std::uint32_t column{};
};

auto Lower(std::string_view text) -> std::string {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

auto Describe(const Token& token) -> std::string {
    switch (token.kind) {
    case Tok::End:    return "end of the formula";
    case Tok::Number: return "number " + std::to_string(token.number);
    case Tok::Name:   return "\"" + token.text + "\"";
    default:          return "'" + token.text + "'";
    }
}

using Kind = FormulaNode::Kind;

class Parser {
public:
    Parser(std::string_view text, Formula& out) : text_(text), out_(out) {}

    auto Run(std::string& error, std::uint32_t& column) -> bool {
        Next();
        const int root = ParseChoose();
        if (root >= 0 && current_.kind != Tok::End) Error(current_.column, "unexpected " + Describe(current_));
        if (!error_.empty()) {
            error  = error_;
            column = errorColumn_;
            return false;
        }
        out_.root = root;
        return true;
    }

private:
    struct Depth {
        explicit Depth(Parser& parser) : parser_(parser) {
            ok = ++parser_.depth_ <= MaxDepth;
            if (!ok) parser_.Error(parser_.current_.column, "the formula nests too deep");
        }
        ~Depth() { --parser_.depth_; }
        Depth(const Depth&) = delete;
        auto operator=(const Depth&) -> Depth& = delete;
        bool ok{};

    private:
        Parser& parser_;
    };

    void Error(std::uint32_t column, std::string message) {
        if (!error_.empty()) return;
        error_       = std::move(message);
        errorColumn_ = column;
    }

    void Next() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\r' || text_[pos_] == '\n')) ++pos_;
        current_        = Token{};
        current_.column = static_cast<std::uint32_t>(pos_ + 1);
        if (pos_ >= text_.size()) return;
        const char c = text_[pos_];
        if (c >= '0' && c <= '9') {
            std::int64_t value = 0;
            std::size_t  digits = 0;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                if (++digits > 18) {
                    Error(current_.column, "the number is too long");
                    current_.kind = Tok::Bad;
                    return;
                }
                value = value * 10 + (text_[pos_] - '0');
                ++pos_;
            }
            current_.kind   = Tok::Number;
            current_.number = value;
            return;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            const std::size_t start = pos_;
            while (pos_ < text_.size()) {
                const char n = text_[pos_];
                if (!((n >= 'a' && n <= 'z') || (n >= 'A' && n <= 'Z') || (n >= '0' && n <= '9') || n == '_')) break;
                ++pos_;
            }
            current_.kind = Tok::Name;
            current_.text.assign(text_.substr(start, pos_ - start));
            return;
        }
        if (c == '\'') {
            const std::size_t close = text_.find('\'', pos_ + 1);
            if (close == std::string_view::npos) {
                Error(current_.column, "a name in quotes is not closed");
                current_.kind = Tok::Bad;
                return;
            }
            current_.kind = Tok::Text;
            current_.text.assign(text_.substr(pos_ + 1, close - pos_ - 1));
            pos_ = close + 1;
            return;
        }
        struct Op {
            std::string_view text;
            Tok              kind;
        };
        static constexpr Op Ops[]{
            { "<=", Tok::LessEq }, { ">=", Tok::GreaterEq }, { "==", Tok::Equal }, { "!=", Tok::NotEqual },
            { "&&", Tok::And }, { "||", Tok::Or }, { "<", Tok::Less }, { ">", Tok::Greater }, { "+", Tok::Plus },
            { "-", Tok::Minus }, { "*", Tok::Star }, { "/", Tok::Slash }, { "!", Tok::Bang }, { "(", Tok::LParen },
            { ")", Tok::RParen }, { ",", Tok::Comma }, { "?", Tok::Question }, { ":", Tok::Colon },
        };
        for (const Op& op : Ops) {
            if (text_.substr(pos_, op.text.size()) == op.text) {
                current_.kind = op.kind;
                current_.text.assign(op.text);
                pos_ += op.text.size();
                return;
            }
        }
        if (c == '"') Error(current_.column, "names go in single quotes, e.g. stat('energy')");
        else if (c == '=') Error(current_.column, "use == to compare");
        else if (c == '&') Error(current_.column, "use && for \"and\"");
        else if (c == '|') Error(current_.column, "use || for \"or\"");
        else Error(current_.column, std::string("unexpected character '") + c + "'");
        current_.kind = Tok::Bad;
    }

    auto Push(const FormulaNode& node) -> int {
        if (out_.nodes.size() >= MaxNodes) {
            Error(current_.column, "the formula is too long");
            return -1;
        }
        out_.nodes.push_back(node);
        return static_cast<int>(out_.nodes.size() - 1);
    }

    auto Node(Kind kind, int a, int b = -1, int c = -1) -> int {
        FormulaNode node;
        node.kind = kind;
        node.a    = a;
        node.b    = b;
        node.c    = c;
        return Push(node);
    }

    // a ? b : c, right to left, lowest of all.
    auto ParseChoose() -> int {
        const Depth depth(*this);
        if (!depth.ok) return -1;
        const int condition = ParseOr();
        if (condition < 0 || current_.kind != Tok::Question) return condition;
        const std::uint32_t question = current_.column;
        Next();
        const int yes = ParseChoose();
        if (yes < 0) return -1;
        if (current_.kind != Tok::Colon) {
            Error(current_.column, "expected ':' for the '?' at column " + std::to_string(question));
            return -1;
        }
        Next();
        const int no = ParseChoose();
        if (no < 0) return -1;
        return Node(Kind::Choose, condition, yes, no);
    }

    template <typename Operand, typename Map>
    auto Chain(Operand operand, Map map) -> int {
        int left = (this->*operand)();
        while (left >= 0) {
            const Kind kind = map(current_.kind);
            if (kind == Kind::Number) break;  // not one of this level's operators
            Next();
            const int right = (this->*operand)();
            if (right < 0) return -1;
            left = Node(kind, left, right);
        }
        return left;
    }

    auto ParseOr() -> int {
        return Chain(&Parser::ParseAnd, [](Tok t) { return t == Tok::Or ? Kind::Or : Kind::Number; });
    }
    auto ParseAnd() -> int {
        return Chain(&Parser::ParseEquality, [](Tok t) { return t == Tok::And ? Kind::And : Kind::Number; });
    }
    auto ParseEquality() -> int {
        return Chain(&Parser::ParseRelation, [](Tok t) {
            return t == Tok::Equal ? Kind::Equal : t == Tok::NotEqual ? Kind::NotEqual : Kind::Number;
        });
    }
    auto ParseRelation() -> int {
        return Chain(&Parser::ParseSum, [](Tok t) {
            switch (t) {
            case Tok::Less:      return Kind::Less;
            case Tok::LessEq:    return Kind::LessOrEqual;
            case Tok::Greater:   return Kind::Greater;
            case Tok::GreaterEq: return Kind::GreaterOrEqual;
            default:             return Kind::Number;
            }
        });
    }
    auto ParseSum() -> int {
        return Chain(&Parser::ParseProduct, [](Tok t) {
            return t == Tok::Plus ? Kind::Add : t == Tok::Minus ? Kind::Subtract : Kind::Number;
        });
    }
    auto ParseProduct() -> int {
        return Chain(&Parser::ParseUnary, [](Tok t) {
            return t == Tok::Star ? Kind::Multiply : t == Tok::Slash ? Kind::Divide : Kind::Number;
        });
    }

    auto ParseUnary() -> int {
        const Depth depth(*this);
        if (!depth.ok) return -1;
        if (current_.kind == Tok::Bang || current_.kind == Tok::Minus) {
            const Kind kind = current_.kind == Tok::Bang ? Kind::Not : Kind::Negate;
            Next();
            const int operand = ParseUnary();
            return operand < 0 ? -1 : Node(kind, operand);
        }
        if (current_.kind == Tok::Plus) {
            Next();
            return ParseUnary();
        }
        return ParsePrimary();
    }

    auto ParsePrimary() -> int {
        switch (current_.kind) {
        case Tok::Number: {
            FormulaNode node;
            node.number = current_.number;
            Next();
            return Push(node);
        }
        case Tok::LParen: {
            const std::uint32_t open = current_.column;
            Next();
            const int inner = ParseChoose();
            if (inner < 0) return -1;
            if (current_.kind != Tok::RParen) {
                Error(current_.column, "expected ')' for the '(' at column " + std::to_string(open));
                return -1;
            }
            Next();
            return inner;
        }
        case Tok::Name: return ParseCall();
        case Tok::Bad:  return -1;
        case Tok::End:  Error(current_.column, "the formula ends too early"); return -1;
        default:        Error(current_.column, "unexpected " + Describe(current_)); return -1;
        }
    }

    auto ParseCall() -> int {
        FormulaStat call;
        call.column = current_.column;
        const std::string name = current_.text;
        const std::string lower = Lower(name);
        bool takesParam = true;
        if (lower == "stat") {
        } else if (lower == "playerstat") {
            call.source = FormulaStat::Source::Player;
        } else if (lower == "statmax") {
            call.mode  = FormulaStat::Mode::Max;
            takesParam = false;
        } else if (lower == "statsum") {
            call.mode  = FormulaStat::Mode::Sum;
            takesParam = false;
        } else {
            Error(current_.column, "unknown name \"" + name + "\"; use stat, playerStat, statMax or statSum");
            return -1;
        }
        Next();
        if (current_.kind != Tok::LParen) {
            Error(current_.column, "expected '(' after " + name);
            return -1;
        }
        Next();
        if (current_.kind == Tok::Text && !current_.text.empty()) {
            call.statName = current_.text;
        } else if (current_.kind == Tok::Number && current_.number < 65536) {
            call.stat = static_cast<int>(current_.number);
        } else {
            if (current_.kind != Tok::Bad) Error(current_.column, name + " needs an itemstatcost.txt stat name in quotes or a row number");
            return -1;
        }
        Next();
        if (current_.kind == Tok::Comma) {
            if (!takesParam) {
                Error(current_.column, name + " takes only the stat: it reads every param itself");
                return -1;
            }
            Next();
            if (current_.kind == Tok::Number && current_.number <= 0x7FFFFFFF) {
                call.param = static_cast<int>(current_.number);
            } else if (current_.kind == Tok::Text && !current_.text.empty()) {
                call.skillName = current_.text;
            } else {
                if (current_.kind != Tok::Bad) Error(current_.column, "the param must be a whole number or a skill name in quotes");
                return -1;
            }
            Next();
        }
        if (current_.kind != Tok::RParen) {
            if (current_.kind != Tok::Bad) Error(current_.column, "expected ')' to close " + name);
            return -1;
        }
        Next();
        if (call.source == FormulaStat::Source::Player) out_.readsPlayer = true;
        out_.stats.push_back(std::move(call));
        FormulaNode node;
        node.kind = Kind::Stat;
        node.a    = static_cast<int>(out_.stats.size() - 1);
        return Push(node);
    }

    std::string_view text_;
    Formula&         out_;
    std::size_t      pos_{};
    Token            current_;
    int              depth_{};
    std::string      error_;
    std::uint32_t    errorColumn_{};
};

auto Wrap(std::uint64_t value) -> std::int64_t { return static_cast<std::int64_t>(value); }

auto Eval(const Formula& formula, int index, const FormulaValues& values) -> std::int64_t {
    const FormulaNode& node = formula.nodes[static_cast<std::size_t>(index)];
    switch (node.kind) {
    case Kind::Number: return node.number;
    case Kind::Stat:   return values.Value(formula.stats[static_cast<std::size_t>(node.a)]);
    case Kind::Negate: return Wrap(0 - static_cast<std::uint64_t>(Eval(formula, node.a, values)));
    case Kind::Not:    return Eval(formula, node.a, values) == 0 ? 1 : 0;
    case Kind::And:    return Eval(formula, node.a, values) != 0 && Eval(formula, node.b, values) != 0 ? 1 : 0;
    case Kind::Or:     return Eval(formula, node.a, values) != 0 || Eval(formula, node.b, values) != 0 ? 1 : 0;
    case Kind::Choose: return Eval(formula, node.a, values) != 0 ? Eval(formula, node.b, values) : Eval(formula, node.c, values);
    default:           break;
    }
    const std::int64_t x = Eval(formula, node.a, values);
    const std::int64_t y = Eval(formula, node.b, values);
    switch (node.kind) {
    case Kind::Multiply:       return Wrap(static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(y));
    case Kind::Divide:         return y == 0 ? 0 : (x == std::numeric_limits<std::int64_t>::min() && y == -1) ? x : x / y;
    case Kind::Add:            return Wrap(static_cast<std::uint64_t>(x) + static_cast<std::uint64_t>(y));
    case Kind::Subtract:       return Wrap(static_cast<std::uint64_t>(x) - static_cast<std::uint64_t>(y));
    case Kind::Less:           return x < y ? 1 : 0;
    case Kind::LessOrEqual:    return x <= y ? 1 : 0;
    case Kind::Greater:        return x > y ? 1 : 0;
    case Kind::GreaterOrEqual: return x >= y ? 1 : 0;
    case Kind::Equal:          return x == y ? 1 : 0;
    case Kind::NotEqual:       return x != y ? 1 : 0;
    default:                   return 0;
    }
}

}  // namespace

auto CompileFormula(std::string_view text, Formula& out, std::string& error, std::uint32_t& column) -> bool {
    out = Formula{};
    Parser parser(text, out);
    if (parser.Run(error, column)) return true;
    out = Formula{};
    return false;
}

auto EvaluateFormula(const Formula& formula, const FormulaValues& values) -> std::int64_t {
    return formula.root < 0 ? 0 : Eval(formula, formula.root, values);
}

}  // namespace lootfilter
