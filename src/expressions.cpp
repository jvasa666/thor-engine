#include "thor/expressions.hpp"
#include "thor/errors.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace thor {

namespace {

// ---------- tokenizer ----------

struct Token {
    enum Kind { End, Num, Str, True_, False_, Not, And, Or, LP, RP, Op, PH } kind = End;
    std::string text;
    Value lit;
};

bool ident_s(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool ident_c(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

std::vector<Token> lex(const std::string& s) {
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (std::isspace((unsigned char)c)) { ++i; continue; }
        if (c == '$' && i + 1 < s.size() && s[i + 1] == '{') {
            auto end = s.find('}', i + 2);
            if (end == std::string::npos)
                throw ExpressionSyntaxError("Unterminated placeholder");
            Token t; t.kind = Token::PH; t.text = s.substr(i + 2, end - i - 2);
            out.push_back(std::move(t)); i = end + 1; continue;
        }
        if (c == '"' || c == '\'') {
            char q = c; ++i; std::string buf;
            while (i < s.size() && s[i] != q) {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    char n = s[i + 1];
                    switch (n) {
                        case 'n':  buf.push_back('\n'); break;
                        case 't':  buf.push_back('\t'); break;
                        case '\\': buf.push_back('\\'); break;
                        case '"':  buf.push_back('"');  break;
                        case '\'': buf.push_back('\''); break;
                        default:   buf.push_back(n);    break;
                    }
                    i += 2;
                } else buf.push_back(s[i++]);
            }
            if (i >= s.size()) throw ExpressionSyntaxError("Unterminated string");
            ++i;
            Token t; t.kind = Token::Str; t.lit = Value{buf};
            out.push_back(std::move(t)); continue;
        }
        if (std::isdigit((unsigned char)c) ||
            (c == '.' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1]))) {
            std::size_t start = i; bool dot = false;
            while (i < s.size() &&
                   (std::isdigit((unsigned char)s[i]) || (s[i] == '.' && !dot))) {
                if (s[i] == '.') dot = true;
                ++i;
            }
            std::string num = s.substr(start, i - start);
            Token t; t.kind = Token::Num;
            try {
                t.lit = dot
                    ? Value{std::stod(num)}
                    : Value{static_cast<std::int64_t>(std::stoll(num))};
            } catch (...) {
                throw ExpressionSyntaxError("Bad number '" + num + "'");
            }
            out.push_back(std::move(t)); continue;
        }
        if (ident_s(c)) {
            std::size_t st = i;
            while (i < s.size() && ident_c(s[i])) ++i;
            std::string id = s.substr(st, i - st);
            Token t;
            if      (id == "true")  { t.kind = Token::True_;  t.lit = Value{true};  }
            else if (id == "false") { t.kind = Token::False_; t.lit = Value{false}; }
            else if (id == "not")   { t.kind = Token::Not; }
            else if (id == "and")   { t.kind = Token::And; }
            else if (id == "or")    { t.kind = Token::Or;  }
            else throw UnsafeExpressionError("Bare identifier '" + id + "'");
            out.push_back(std::move(t)); continue;
        }
        if (c == '(') { Token t; t.kind = Token::LP; out.push_back(std::move(t)); ++i; continue; }
        if (c == ')') { Token t; t.kind = Token::RP; out.push_back(std::move(t)); ++i; continue; }

        static const char* two[] = {"==", "!=", "<=", ">="};
        bool m2 = false;
        for (auto op : two) {
            if (i + 1 < s.size() && s[i] == op[0] && s[i + 1] == op[1]) {
                Token t; t.kind = Token::Op; t.text = op;
                out.push_back(std::move(t));
                i += 2; m2 = true; break;
            }
        }
        if (m2) continue;

        if (std::strchr("+-*/%<>", c)) {
            Token t; t.kind = Token::Op; t.text = std::string(1, c);
            out.push_back(std::move(t)); ++i; continue;
        }
        throw ExpressionSyntaxError(std::string("Unexpected '") + c + "'");
    }
    out.push_back(Token{});
    return out;
}

// ---------- recursive-descent parser ----------

struct Parser {
    const std::vector<Token>& t;
    std::size_t i = 0;

    const Token& peek() const { return t[i]; }
    const Token& next() { return t[i++]; }
    bool is_k(Token::Kind k) const { return t[i].kind == k; }
    bool is_op(const char* o) const { return t[i].kind == Token::Op && t[i].text == o; }
    bool eat(Token::Kind k) { if (is_k(k)) { ++i; return true; } return false; }
    bool eat_op(const char* o) { if (is_op(o)) { ++i; return true; } return false; }

    ExprPtr parse() {
        auto e = or_();
        if (!is_k(Token::End)) throw ExpressionSyntaxError("Trailing token");
        return e;
    }
    ExprPtr or_() {
        auto l = and_();
        while (eat(Token::Or)) {
            auto r = and_();
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Bin; e->bop = BinOp::Or;
            e->l = std::move(l); e->r = std::move(r);
            l = std::move(e);
        }
        return l;
    }
    ExprPtr and_() {
        auto l = eq_();
        while (eat(Token::And)) {
            auto r = eq_();
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Bin; e->bop = BinOp::And;
            e->l = std::move(l); e->r = std::move(r);
            l = std::move(e);
        }
        return l;
    }
    ExprPtr eq_() {
        auto l = cmp_();
        for (;;) {
            if (eat_op("==") || eat_op("!=")) {
                bool eq = t[i - 1].text == "==";
                auto r = cmp_();
                auto e = std::make_unique<Expr>();
                e->kind = ExprKind::Bin;
                e->bop = eq ? BinOp::Eq : BinOp::Ne;
                e->l = std::move(l); e->r = std::move(r);
                l = std::move(e);
            } else break;
        }
        return l;
    }
    ExprPtr cmp_() {
        auto l = add_();
        for (;;) {
            BinOp b;
            if      (eat_op("<"))  b = BinOp::Lt;
            else if (eat_op("<=")) b = BinOp::Le;
            else if (eat_op(">"))  b = BinOp::Gt;
            else if (eat_op(">=")) b = BinOp::Ge;
            else break;
            auto r = add_();
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Bin; e->bop = b;
            e->l = std::move(l); e->r = std::move(r);
            l = std::move(e);
        }
        return l;
    }
    ExprPtr add_() {
        auto l = mul_();
        for (;;) {
            BinOp b;
            if      (eat_op("+")) b = BinOp::Add;
            else if (eat_op("-")) b = BinOp::Sub;
            else break;
            auto r = mul_();
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Bin; e->bop = b;
            e->l = std::move(l); e->r = std::move(r);
            l = std::move(e);
        }
        return l;
    }
    ExprPtr mul_() {
        auto l = un_();
        for (;;) {
            BinOp b;
            if      (eat_op("*")) b = BinOp::Mul;
            else if (eat_op("/")) b = BinOp::Div;
            else if (eat_op("%")) b = BinOp::Mod;
            else break;
            auto r = un_();
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Bin; e->bop = b;
            e->l = std::move(l); e->r = std::move(r);
            l = std::move(e);
        }
        return l;
    }
    ExprPtr un_() {
        if (eat(Token::Not)) {
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Un; e->uop = UnOp::Not;
            e->l = un_(); return e;
        }
        if (eat_op("-")) {
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Un; e->uop = UnOp::Neg;
            e->l = un_(); return e;
        }
        if (eat_op("+")) {
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Un; e->uop = UnOp::Pos;
            e->l = un_(); return e;
        }
        return prim_();
    }
    ExprPtr prim_() {
        Token tok = next();
        if (tok.kind == Token::Num || tok.kind == Token::Str ||
            tok.kind == Token::True_ || tok.kind == Token::False_) {
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Lit; e->lit = tok.lit; return e;
        }
        if (tok.kind == Token::PH) {
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::PH; e->ph = tok.text; return e;
        }
        if (tok.kind == Token::LP) {
            auto e = or_();
            if (!eat(Token::RP)) throw ExpressionSyntaxError("Expected ')'");
            return e;
        }
        throw ExpressionSyntaxError("Expected expression operand");
    }
};

// ---------- value helpers ----------

double as_dbl(const Value& v) {
    if (auto p = std::get_if<double>(&v.data)) return *p;
    if (auto p = std::get_if<std::int64_t>(&v.data)) return static_cast<double>(*p);
    throw ExpressionTypeError("Not numeric");
}

std::int64_t as_int(const Value& v) {
    if (auto p = std::get_if<std::int64_t>(&v.data)) return *p;
    if (auto p = std::get_if<double>(&v.data)) {
        double d = *p;
        constexpr double min_i = -9223372036854775808.0;
        constexpr double max_i_excl = 9223372036854775808.0;
        if (!std::isfinite(d) || std::trunc(d) != d || d < min_i || d >= max_i_excl)
            throw ExpressionTypeError("Not a finite integral int64");
        return static_cast<std::int64_t>(d);
    }
    throw ExpressionTypeError("Not an integer");
}

bool truthy(const Value& v) {
    if (auto p = std::get_if<bool>(&v.data)) return *p;
    if (auto p = std::get_if<std::int64_t>(&v.data)) return *p != 0;
    if (auto p = std::get_if<double>(&v.data)) return *p != 0.0;
    if (auto p = std::get_if<std::string>(&v.data)) return !p->empty();
    if (auto p = std::get_if<Value::MapPtr>(&v.data)) return *p && !(*p)->empty();
    return false;
}

std::string interpolate(const std::string& tmpl, const RuntimeState& st) {
    std::string out;
    std::size_t i = 0;
    while (i < tmpl.size()) {
        if (tmpl[i] == '$' && i + 1 < tmpl.size() && tmpl[i + 1] == '{') {
            auto end = tmpl.find('}', i + 2);
            if (end == std::string::npos) { out.push_back(tmpl[i++]); continue; }
            std::string path = tmpl.substr(i + 2, end - i - 2);
            Value v = (st.mode() == StateMode::Strict)
                        ? st.require_path(path)
                        : st.get_path(path);
            if (auto p = std::get_if<std::string>(&v.data))       out += *p;
            else if (auto p = std::get_if<std::int64_t>(&v.data)) out += std::to_string(*p);
            else if (auto p = std::get_if<double>(&v.data))       out += std::to_string(*p);
            else if (auto p = std::get_if<bool>(&v.data))         out += (*p ? "true" : "false");
            i = end + 1;
        } else {
            out.push_back(tmpl[i++]);
        }
    }
    return out;
}

// ---------- evaluator ----------

Value eval_expr(const Expr& e, const RuntimeState& st) {
    if (e.kind == ExprKind::Lit) {
        if (auto p = std::get_if<std::string>(&e.lit.data)) {
            if (p->find("${") != std::string::npos)
                return Value{interpolate(*p, st)};
        }
        return e.lit;
    }
    if (e.kind == ExprKind::PH) {
        return st.mode() == StateMode::Strict
            ? st.require_path(e.ph)
            : st.get_path(e.ph);
    }
    if (e.kind == ExprKind::Un) {
        auto v = eval_expr(*e.l, st);
        if (e.uop == UnOp::Not) return Value{!truthy(v)};
        if (e.uop == UnOp::Neg) {
            if (auto p = std::get_if<std::int64_t>(&v.data)) {
                if (*p == std::numeric_limits<std::int64_t>::min())
                    throw ExpressionTypeError("Negation overflow");
                return Value{-*p};
            }
            return Value{-as_dbl(v)};
        }
        if (!std::holds_alternative<std::int64_t>(v.data) &&
            !std::holds_alternative<double>(v.data))
            throw ExpressionTypeError("Unary + needs numeric");
        return v;
    }

    // Binary
    if (e.bop == BinOp::And) {
        auto l = eval_expr(*e.l, st);
        if (!truthy(l)) return Value{false};
        return Value{truthy(eval_expr(*e.r, st))};
    }
    if (e.bop == BinOp::Or) {
        auto l = eval_expr(*e.l, st);
        if (truthy(l)) return Value{true};
        return Value{truthy(eval_expr(*e.r, st))};
    }

    auto l = eval_expr(*e.l, st);
    auto r = eval_expr(*e.r, st);
    switch (e.bop) {
        case BinOp::Eq: return Value{value_equal(l, r)};
        case BinOp::Ne: return Value{!value_equal(l, r)};
        case BinOp::Lt: return Value{as_dbl(l) <  as_dbl(r)};
        case BinOp::Le: return Value{as_dbl(l) <= as_dbl(r)};
        case BinOp::Gt: return Value{as_dbl(l) >  as_dbl(r)};
        case BinOp::Ge: return Value{as_dbl(l) >= as_dbl(r)};
        case BinOp::Add: {
            if (std::holds_alternative<std::string>(l.data) ||
                std::holds_alternative<std::string>(r.data)) {
                auto ls = std::get_if<std::string>(&l.data);
                auto rs = std::get_if<std::string>(&r.data);
                return Value{(ls ? *ls : std::string{}) + (rs ? *rs : std::string{})};
            }
            if (auto li = std::get_if<std::int64_t>(&l.data)) {
                if (auto ri = std::get_if<std::int64_t>(&r.data)) return Value{*li + *ri};
            }
            return Value{as_dbl(l) + as_dbl(r)};
        }
        case BinOp::Sub: {
            if (auto li = std::get_if<std::int64_t>(&l.data))
                if (auto ri = std::get_if<std::int64_t>(&r.data)) return Value{*li - *ri};
            return Value{as_dbl(l) - as_dbl(r)};
        }
        case BinOp::Mul: {
            if (auto li = std::get_if<std::int64_t>(&l.data))
                if (auto ri = std::get_if<std::int64_t>(&r.data)) return Value{*li * *ri};
            return Value{as_dbl(l) * as_dbl(r)};
        }
        case BinOp::Div: {
            double d = as_dbl(r);
            if (d == 0.0) throw ExpressionTypeError("Division by zero");
            return Value{as_dbl(l) / d};
        }
        case BinOp::Mod: {
            auto a = as_int(l); auto b = as_int(r);
            if (b == 0) throw ExpressionTypeError("Modulo by zero");
            return Value{a % b};
        }
        default: break;
    }
    throw ExpressionSyntaxError("Internal binary op");
}

ExprPtr parse_text(const std::string& s) {
    bool empty = true;
    for (char c : s) if (!std::isspace((unsigned char)c)) { empty = false; break; }
    if (empty) {
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Lit; e->lit = Value{};
        return e;
    }
    auto toks = lex(s);
    Parser p{toks};
    return p.parse();
}

} // namespace

// ---------- public API ----------

Value ExpressionEvaluator::eval(const std::string& s) const {
    auto ast = parse_text(s);
    return eval_expr(*ast, st_);
}

bool ExpressionEvaluator::eval_bool(const std::string& s) const {
    return truthy(eval(s));
}

std::int64_t ExpressionEvaluator::eval_int(const std::string& s) const {
    return as_int(eval(s));
}

} // namespace thor
