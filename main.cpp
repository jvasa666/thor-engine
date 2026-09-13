// THOR Engine - single-file build
// Copyright (c) 2026 Joseph Vasapolli. All rights reserved.

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>


#include "thor/errors.hpp"
#include "thor/version.hpp"

using namespace thor;


#include "thor/models.hpp"
#include "thor/state.hpp"
// ---------- expression evaluator ----------
struct Token {
    enum Kind { End, Num, Str, True_, False_, Not, And, Or, LP, RP, Op, PH } kind = End;
    std::string text;
    Value lit;
};
enum class ExprKind { Lit, PH, Un, Bin };
enum class UnOp { Not, Neg, Pos };
enum class BinOp { Add, Sub, Mul, Div, Mod, Eq, Ne, Lt, Le, Gt, Ge, And, Or };
struct Expr;
using ExprPtr = std::unique_ptr<Expr>;
struct Expr {
    ExprKind kind = ExprKind::Lit;
    Value lit;
    std::string ph;
    UnOp uop = UnOp::Not;
    BinOp bop = BinOp::Add;
    ExprPtr l, r;
};

class ExpressionEvaluator {
public:
    explicit ExpressionEvaluator(const RuntimeState& s) : st_(s) {}
    Value eval(const std::string& s) const { return eval_(*parse_(s), st_); }
    bool  eval_bool(const std::string& s) const { return truthy_(eval(s)); }
    std::int64_t eval_int(const std::string& s) const { return as_int_(eval(s)); }

private:
    const RuntimeState& st_;

    static bool ident_s(char c){ return std::isalpha((unsigned char)c) || c == '_'; }
    static bool ident_c(char c){ return std::isalnum((unsigned char)c) || c == '_'; }

    std::vector<Token> lex_(const std::string& s) const {
        std::vector<Token> out; std::size_t i = 0;
        while (i < s.size()) {
            char c = s[i];
            if (std::isspace((unsigned char)c)) { ++i; continue; }
            if (c == '$' && i+1 < s.size() && s[i+1] == '{') {
                auto end = s.find('}', i+2);
                if (end == std::string::npos) throw ExpressionSyntaxError("Unterminated placeholder");
                Token t; t.kind = Token::PH; t.text = s.substr(i+2, end-i-2);
                out.push_back(std::move(t)); i = end + 1; continue;
            }
            if (c == '"' || c == '\'') {
                char q = c; ++i; std::string buf;
                while (i < s.size() && s[i] != q) {
                    if (s[i] == '\\' && i+1 < s.size()) {
                        char n = s[i+1];
                        switch (n) {
                            case 'n': buf.push_back('\n'); break;
                            case 't': buf.push_back('\t'); break;
                            case '\\': buf.push_back('\\'); break;
                            case '"': buf.push_back('"'); break;
                            case '\'': buf.push_back('\''); break;
                            default: buf.push_back(n); break;
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
                (c == '.' && i+1 < s.size() && std::isdigit((unsigned char)s[i+1]))) {
                std::size_t start = i; bool dot = false;
                while (i < s.size() && (std::isdigit((unsigned char)s[i]) || (s[i]=='.' && !dot))) {
                    if (s[i] == '.') dot = true;
                    ++i;
                }
                std::string num = s.substr(start, i-start);
                Token t; t.kind = Token::Num;
                try {
                    t.lit = dot ? Value{std::stod(num)} : Value{static_cast<std::int64_t>(std::stoll(num))};
                } catch (...) { throw ExpressionSyntaxError("Bad number '" + num + "'"); }
                out.push_back(std::move(t)); continue;
            }
            if (ident_s(c)) {
                std::size_t st = i;
                while (i < s.size() && ident_c(s[i])) ++i;
                std::string id = s.substr(st, i-st);
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
            static const char* two[] = {"==","!=","<=",">="};
            bool m2 = false;
            for (auto op : two) if (i+1 < s.size() && s[i]==op[0] && s[i+1]==op[1]) {
                Token t; t.kind = Token::Op; t.text = op; out.push_back(std::move(t));
                i += 2; m2 = true; break;
            }
            if (m2) continue;
            if (std::strchr("+-*/%<>", c)) {
                Token t; t.kind = Token::Op; t.text = std::string(1,c);
                out.push_back(std::move(t)); ++i; continue;
            }
            throw ExpressionSyntaxError(std::string("Unexpected '") + c + "'");
        }
        out.push_back(Token{});
        return out;
    }

    struct P {
        const std::vector<Token>& t; std::size_t i = 0;
        const Token& peek() const { return t[i]; }
        const Token& next() { return t[i++]; }
        bool is_k(Token::Kind k) const { return t[i].kind == k; }
        bool is_op(const char* o) const { return t[i].kind == Token::Op && t[i].text == o; }
        bool eat(Token::Kind k) { if (is_k(k)) { ++i; return true; } return false; }
        bool eat_op(const char* o) { if (is_op(o)) { ++i; return true; } return false; }

        ExprPtr parse() { auto e = or_(); if (!is_k(Token::End)) throw ExpressionSyntaxError("Trailing token"); return e; }
        ExprPtr or_() {
            auto l = and_();
            while (eat(Token::Or)) {
                auto r = and_();
                auto e = std::make_unique<Expr>(); e->kind = ExprKind::Bin; e->bop = BinOp::Or;
                e->l = std::move(l); e->r = std::move(r); l = std::move(e);
            }
            return l;
        }
        ExprPtr and_() {
            auto l = eq_();
            while (eat(Token::And)) {
                auto r = eq_();
                auto e = std::make_unique<Expr>(); e->kind = ExprKind::Bin; e->bop = BinOp::And;
                e->l = std::move(l); e->r = std::move(r); l = std::move(e);
            }
            return l;
        }
        ExprPtr eq_() {
            auto l = cmp_();
            for (;;) {
                if (eat_op("==") || eat_op("!=")) {
                    bool eq = t[i-1].text == "==";
                    auto r = cmp_();
                    auto e = std::make_unique<Expr>(); e->kind = ExprKind::Bin;
                    e->bop = eq ? BinOp::Eq : BinOp::Ne;
                    e->l = std::move(l); e->r = std::move(r); l = std::move(e);
                } else break;
            }
            return l;
        }
        ExprPtr cmp_() {
            auto l = add_();
            for (;;) {
                BinOp b;
                if (eat_op("<")) b = BinOp::Lt;
                else if (eat_op("<=")) b = BinOp::Le;
                else if (eat_op(">")) b = BinOp::Gt;
                else if (eat_op(">=")) b = BinOp::Ge;
                else break;
                auto r = add_();
                auto e = std::make_unique<Expr>(); e->kind = ExprKind::Bin; e->bop = b;
                e->l = std::move(l); e->r = std::move(r); l = std::move(e);
            }
            return l;
        }
        ExprPtr add_() {
            auto l = mul_();
            for (;;) {
                BinOp b;
                if (eat_op("+")) b = BinOp::Add;
                else if (eat_op("-")) b = BinOp::Sub;
                else break;
                auto r = mul_();
                auto e = std::make_unique<Expr>(); e->kind = ExprKind::Bin; e->bop = b;
                e->l = std::move(l); e->r = std::move(r); l = std::move(e);
            }
            return l;
        }
        ExprPtr mul_() {
            auto l = un_();
            for (;;) {
                BinOp b;
                if (eat_op("*")) b = BinOp::Mul;
                else if (eat_op("/")) b = BinOp::Div;
                else if (eat_op("%")) b = BinOp::Mod;
                else break;
                auto r = un_();
                auto e = std::make_unique<Expr>(); e->kind = ExprKind::Bin; e->bop = b;
                e->l = std::move(l); e->r = std::move(r); l = std::move(e);
            }
            return l;
        }
        ExprPtr un_() {
            if (eat(Token::Not)) {
                auto e = std::make_unique<Expr>(); e->kind = ExprKind::Un; e->uop = UnOp::Not;
                e->l = un_(); return e;
            }
            if (eat_op("-")) {
                auto e = std::make_unique<Expr>(); e->kind = ExprKind::Un; e->uop = UnOp::Neg;
                e->l = un_(); return e;
            }
            if (eat_op("+")) {
                auto e = std::make_unique<Expr>(); e->kind = ExprKind::Un; e->uop = UnOp::Pos;
                e->l = un_(); return e;
            }
            return prim_();
        }
        ExprPtr prim_() {
            Token t = next();
            if (t.kind == Token::Num || t.kind == Token::Str ||
                t.kind == Token::True_ || t.kind == Token::False_) {
                auto e = std::make_unique<Expr>(); e->kind = ExprKind::Lit; e->lit = t.lit; return e;
            }
            if (t.kind == Token::PH) {
                auto e = std::make_unique<Expr>(); e->kind = ExprKind::PH; e->ph = t.text; return e;
            }
            if (t.kind == Token::LP) {
                auto e = or_();
                if (!eat(Token::RP)) throw ExpressionSyntaxError("Expected ')'");
                return e;
            }
            throw ExpressionSyntaxError("Expected expression operand");
        }
    };

    ExprPtr parse_(const std::string& s) const {
        bool empty = true;
        for (char c : s) if (!std::isspace((unsigned char)c)) { empty = false; break; }
        if (empty) { auto e = std::make_unique<Expr>(); e->kind = ExprKind::Lit; e->lit = Value{}; return e; }
        auto toks = lex_(s);
        P p{toks};
        return p.parse();
    }

    static double as_dbl_(const Value& v) {
        if (auto p = std::get_if<double>(&v.data)) return *p;
        if (auto p = std::get_if<std::int64_t>(&v.data)) return static_cast<double>(*p);
        throw ExpressionTypeError("Not numeric");
    }
    static std::int64_t as_int_(const Value& v) {
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
    static std::string interpolate_(const std::string& tmpl, const RuntimeState& st) {
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
                if (auto p = std::get_if<std::string>(&v.data)) out += *p;
                else if (auto p = std::get_if<std::int64_t>(&v.data)) out += std::to_string(*p);
                else if (auto p = std::get_if<double>(&v.data)) out += std::to_string(*p);
                else if (auto p = std::get_if<bool>(&v.data)) out += (*p ? "true" : "false");
                i = end + 1;
            } else {
                out.push_back(tmpl[i++]);
            }
        }
        return out;
    }

    static bool truthy_(const Value& v) {
        if (auto p = std::get_if<bool>(&v.data)) return *p;
        if (auto p = std::get_if<std::int64_t>(&v.data)) return *p != 0;
        if (auto p = std::get_if<double>(&v.data)) return *p != 0.0;
        if (auto p = std::get_if<std::string>(&v.data)) return !p->empty();
        if (auto p = std::get_if<Value::MapPtr>(&v.data)) return *p && !(*p)->empty();
        return false;
    }
    static Value eval_(const Expr& e, const RuntimeState& st) {
        if (e.kind == ExprKind::Lit) {
            if (auto p = std::get_if<std::string>(&e.lit.data)) {
                if (p->find("${") != std::string::npos)
                    return Value{interpolate_(*p, st)};
            }
            return e.lit;
        }
        if (e.kind == ExprKind::PH) {
            return st.mode() == StateMode::Strict ? st.require_path(e.ph) : st.get_path(e.ph);
        }
        if (e.kind == ExprKind::Un) {
            auto v = eval_(*e.l, st);
            if (e.uop == UnOp::Not) return Value{!truthy_(v)};
            if (e.uop == UnOp::Neg) {
                if (auto p = std::get_if<std::int64_t>(&v.data)) {
                    if (*p == std::numeric_limits<std::int64_t>::min())
                        throw ExpressionTypeError("Negation overflow");
                    return Value{-*p};
                }
                return Value{-as_dbl_(v)};
            }
            if (!std::holds_alternative<std::int64_t>(v.data) &&
                !std::holds_alternative<double>(v.data))
                throw ExpressionTypeError("Unary + needs numeric");
            return v;
        }
        // binary
        if (e.bop == BinOp::And) {
            auto l = eval_(*e.l, st);
            if (!truthy_(l)) return Value{false};
            return Value{truthy_(eval_(*e.r, st))};
        }
        if (e.bop == BinOp::Or) {
            auto l = eval_(*e.l, st);
            if (truthy_(l)) return Value{true};
            return Value{truthy_(eval_(*e.r, st))};
        }
        auto l = eval_(*e.l, st);
        auto r = eval_(*e.r, st);
        switch (e.bop) {
            case BinOp::Eq: return Value{value_equal(l, r)};
            case BinOp::Ne: return Value{!value_equal(l, r)};
            case BinOp::Lt: return Value{as_dbl_(l) <  as_dbl_(r)};
            case BinOp::Le: return Value{as_dbl_(l) <= as_dbl_(r)};
            case BinOp::Gt: return Value{as_dbl_(l) >  as_dbl_(r)};
            case BinOp::Ge: return Value{as_dbl_(l) >= as_dbl_(r)};
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
                return Value{as_dbl_(l) + as_dbl_(r)};
            }
            case BinOp::Sub: {
                if (auto li = std::get_if<std::int64_t>(&l.data))
                    if (auto ri = std::get_if<std::int64_t>(&r.data)) return Value{*li - *ri};
                return Value{as_dbl_(l) - as_dbl_(r)};
            }
            case BinOp::Mul: {
                if (auto li = std::get_if<std::int64_t>(&l.data))
                    if (auto ri = std::get_if<std::int64_t>(&r.data)) return Value{*li * *ri};
                return Value{as_dbl_(l) * as_dbl_(r)};
            }
            case BinOp::Div: {
                double d = as_dbl_(r);
                if (d == 0.0) throw ExpressionTypeError("Division by zero");
                return Value{as_dbl_(l) / d};
            }
            case BinOp::Mod: {
                auto a = as_int_(l); auto b = as_int_(r);
                if (b == 0) throw ExpressionTypeError("Modulo by zero");
                return Value{a % b};
            }
            default: break;
        }
        throw ExpressionSyntaxError("Internal binary op");
    }

public:
    Value eval_impl(const Expr& e) const { return eval_(e, st_); }
};

#include "thor/registry.hpp"
// ---------- built-in engines ----------
class DialogueEngine : public Engine {
public:
    std::string name() const override { return "Dialogue"; }
    std::string status() const override { return "ready"; }
    EngineResult execute(const EngineRequest& r) override {
        EngineResult out;
        if (r.action == "Say") {
            auto it = r.arguments.find("message");
            if (it != r.arguments.end())
                if (auto p = std::get_if<std::string>(&it->second.data))
                    std::cout << "[Dialogue] " << *p << "\n";
        }
        return out;
    }
};

class CodexEngine : public Engine {
public:
    std::string name() const override { return "Codex"; }
    std::string status() const override { return "in-memory"; }
    EngineResult execute(const EngineRequest& r) override {
        EngineResult out;
        auto get = [&](const char* k) -> std::string {
            auto it = r.arguments.find(k);
            if (it == r.arguments.end()) return {};
            if (auto p = std::get_if<std::string>(&it->second.data)) return *p;
            if (auto p = std::get_if<std::int64_t>(&it->second.data)) return std::to_string(*p);
            return {};
        };
        if (r.action == "Store_Knowledge") {
            std::lock_guard<std::mutex> lk(mu_);
            kb_[get("Key")] = get("Value");
        } else if (r.action == "Retrieve_Knowledge") {
            std::lock_guard<std::mutex> lk(mu_);
            auto it = kb_.find(get("Key"));
            if (it == kb_.end()) out.success = false;
            else out.output["value"] = Value{it->second};
        }
        return out;
    }
private:
    std::mutex mu_;
    std::map<std::string, std::string> kb_;
};

class ObserveEngine : public Engine {
public:
    std::string name() const override { return "Observe"; }
    std::string status() const override { return "monitoring"; }
    EngineResult execute(const EngineRequest&) override {
        EngineResult out;
        ValueMap m; m["Threats_Detected"] = Value{false};
        out.patch.metrics["Observe_Result"] = make_map(std::move(m));
        return out;
    }
};

// ---------- parser ----------
class Parser {
public:
    Program parse_text(const std::string& text) const {
        std::istringstream in(text);
        return parse(in);
    }
    Program parse(std::istream& in) const {
        Program prog;
        std::vector<std::size_t> loops;   // LoopStart indexes
        std::vector<std::size_t> ifs;     // IfStart indexes
        std::vector<bool> if_has_else;

        std::string raw; std::size_t line = 0;
        while (std::getline(in, raw)) {
            ++line;
            std::string s = strip_(raw);
            if (s.empty()) continue;

            if (s.rfind("Block ", 0) == 0) continue; // skip block markers

            if (s == "Break") {
                if (loops.empty()) throw ValidationError("Break outside loop at line " + std::to_string(line));
                Instruction i; i.kind = OpKind::Break; i.loc = {line, 1};
                prog.instructions.push_back(i); continue;
            }
            if (s == "Continue") {
                if (loops.empty()) throw ValidationError("Continue outside loop at line " + std::to_string(line));
                Instruction i; i.kind = OpKind::Continue; i.loc = {line, 1};
                prog.instructions.push_back(i); continue;
            }

            if (s.rfind("Loop Iterations:", 0) == 0) {
                std::string body = s.substr(16);
                auto as_pos = body.find(" AS:");
                if (as_pos == std::string::npos)
                    throw ParseError("Loop missing ' AS:' at line " + std::to_string(line));
                std::string count = trim_(body.substr(0, as_pos));
                std::string varname = trim_(body.substr(as_pos + 4));
                if (varname.size() >= 2 && varname.front() == '"' && varname.back() == '"')
                    varname = varname.substr(1, varname.size() - 2);
                if (count.empty())
                    throw ParseError("Loop count empty at line " + std::to_string(line));
                Instruction i; i.kind = OpKind::LoopStart; i.loc = {line, 1};
                i.loop_count_expr = count; i.loop_var = varname;
                loops.push_back(prog.instructions.size());
                prog.instructions.push_back(i); continue;
            }
            if (s == "Loop End") {
                if (loops.empty()) throw ValidationError("Loop End without Loop at line " + std::to_string(line));
                std::size_t st = loops.back(); loops.pop_back();
                Instruction i; i.kind = OpKind::LoopEnd; i.loc = {line, 1};
                i.jump = st;
                std::size_t end_idx = prog.instructions.size();
                prog.instructions.push_back(i);
                prog.instructions[st].jump = end_idx;
                continue;
            }

            if (s.rfind("If ", 0) == 0 && s.back() == ':') {
                std::string cond = s.substr(3, s.size() - 4);
                if (trim_(cond).empty())
                    throw ParseError("Empty If condition at line " + std::to_string(line));
                Instruction i; i.kind = OpKind::IfStart; i.loc = {line, 1};
                i.condition_expr = cond;
                ifs.push_back(prog.instructions.size());
                if_has_else.push_back(false);
                prog.instructions.push_back(i); continue;
            }
            if (s == "Else:") {
                if (ifs.empty()) throw ValidationError("Else without If at line " + std::to_string(line));
                if (if_has_else.back()) throw ValidationError("Duplicate Else at line " + std::to_string(line));
                if_has_else.back() = true;
                Instruction i; i.kind = OpKind::Else; i.loc = {line, 1};
                i.jump = ifs.back();
                std::size_t else_idx = prog.instructions.size();
                prog.instructions.push_back(i);
                prog.instructions[ifs.back()].alt = else_idx;
                continue;
            }
            if (s == "If End") {
                if (ifs.empty()) throw ValidationError("If End without If at line " + std::to_string(line));
                std::size_t st = ifs.back(); ifs.pop_back();
                bool has_else = if_has_else.back(); if_has_else.pop_back();
                Instruction i; i.kind = OpKind::IfEnd; i.loc = {line, 1};
                std::size_t end_idx = prog.instructions.size();
                prog.instructions.push_back(i);
                if (has_else) {
                    auto else_idx = *prog.instructions[st].alt;
                    prog.instructions[else_idx].jump = end_idx;
                    prog.instructions[st].jump = else_idx;
                } else {
                    prog.instructions[st].jump = end_idx;
                }
                continue;
            }

            // Pillar/Action statement
            std::istringstream ss(s);
            std::string pillar, action;
            ss >> pillar >> action;
            if (pillar.empty() || action.empty())
                throw ParseError("Cannot parse line " + std::to_string(line) + ": " + s);
            Instruction i; i.kind = OpKind::Call; i.loc = {line, 1};
            i.pillar = pillar; i.action = action;

            std::string rest; std::getline(ss, rest);
            // Very small arg parser: Key: "value" or Key: value
            std::size_t p = 0;
            while (p < rest.size()) {
                while (p < rest.size() && std::isspace((unsigned char)rest[p])) ++p;
                std::size_t kstart = p;
                while (p < rest.size() && (std::isalnum((unsigned char)rest[p]) || rest[p] == '_')) ++p;
                if (p == kstart) break;
                std::string key = rest.substr(kstart, p - kstart);
                while (p < rest.size() && std::isspace((unsigned char)rest[p])) ++p;
                if (p >= rest.size() || rest[p] != ':') break;
                ++p;
                while (p < rest.size() && std::isspace((unsigned char)rest[p])) ++p;
                std::string val;
                if (p < rest.size() && (rest[p] == '"' || rest[p] == '\'')) {
                    char q = rest[p];
                    val.push_back(q);              // keep opening quote
                    ++p;
                    while (p < rest.size() && rest[p] != q) val.push_back(rest[p++]);
                    if (p < rest.size()) { val.push_back(q); ++p; }  // keep closing quote
                } else {
                    while (p < rest.size() && !std::isspace((unsigned char)rest[p])) val.push_back(rest[p++]);
                }
                i.args.emplace_back(key, val);
            }
            prog.instructions.push_back(i);
        }
        if (!loops.empty()) throw ValidationError("Unterminated Loop");
        if (!ifs.empty())   throw ValidationError("Unterminated If");
        return prog;
    }

private:
    static std::string strip_(const std::string& raw) {
        // Remove # comment unless inside a quote
        std::string s; bool dq = false, sq = false;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            char c = raw[i];
            if (c == '\\' && i+1 < raw.size() && (dq || sq)) { s.push_back(c); s.push_back(raw[++i]); continue; }
            if (c == '"' && !sq) dq = !dq;
            else if (c == '\'' && !dq) sq = !sq;
            else if (c == '#' && !dq && !sq) break;
            s.push_back(c);
        }
        return trim_(s);
    }
    static std::string trim_(const std::string& s) {
        std::size_t a = 0, b = s.size();
        while (a < b && std::isspace((unsigned char)s[a])) ++a;
        while (b > a && std::isspace((unsigned char)s[b-1])) --b;
        return s.substr(a, b - a);
    }
};

// ---------- executor ----------
struct LoopFrame {
    std::string variable_name;
    std::int64_t iteration = 0, total = 0;
    std::size_t loop_start_index = 0, loop_end_index = 0;
    bool had_prior_variable = false;
    Value prior_value;
};
struct ExecutionSummary {
    bool success = true;
    std::size_t executed_instructions = 0, final_pc = 0;
    std::string error;
};

class Executor {
public:
    Executor(RuntimeState& s, EngineRegistry& r, const ExpressionEvaluator& e)
        : st_(s), reg_(r), ev_(e) {}

    ExecutionSummary run(const Program& prog, const std::string& exec_id) {
        ExecutionSummary sum;
        std::vector<LoopFrame> loops;
        std::size_t pc = 0;
        const auto& ins = prog.instructions;
        while (pc < ins.size()) {
            if (sum.executed_instructions >= prog.step_limit)
                throw ExecutionError("Step limit exceeded");
            const auto& cur = ins[pc];
            ++sum.executed_instructions;

            switch (cur.kind) {
                case OpKind::LoopStart: {
                    auto n = ev_.eval_int(cur.loop_count_expr);
                    if (n < 0) throw ExecutionError("Negative loop count");
                    if (n == 0) { pc = *cur.jump + 1; break; }
                    LoopFrame f;
                    f.variable_name = cur.loop_var;
                    f.total = n;
                    f.loop_start_index = pc;
                    f.loop_end_index = *cur.jump;
                    f.had_prior_variable = st_.has_variable(cur.loop_var);
                    if (f.had_prior_variable) f.prior_value = st_.get_variable(cur.loop_var);
                    st_.set_variable(cur.loop_var, Value{std::int64_t{0}});
                    loops.push_back(std::move(f));
                    ++pc; break;
                }
                case OpKind::LoopEnd: {
                    if (loops.empty()) throw ExecutionError("LoopEnd without frame");
                    auto& f = loops.back();
                    ++f.iteration;
                    if (f.iteration < f.total) {
                        st_.set_variable(f.variable_name, Value{f.iteration});
                        pc = *cur.jump + 1;
                    } else {
                        if (f.had_prior_variable) st_.set_variable(f.variable_name, f.prior_value);
                        else st_.erase_variable(f.variable_name);
                        loops.pop_back();
                        ++pc;
                    }
                    break;
                }
                case OpKind::IfStart: {
                    bool cond = ev_.eval_bool(cur.condition_expr);
                    if (cond) ++pc;
                    else pc = *cur.jump + 1;
                    break;
                }
                case OpKind::Else: {
                    pc = *cur.jump + 1; break;
                }
                case OpKind::IfEnd: { ++pc; break; }
                case OpKind::Break: {
                    if (loops.empty()) throw ExecutionError("Break without frame");
                    auto f = loops.back(); loops.pop_back();
                    if (f.had_prior_variable) st_.set_variable(f.variable_name, f.prior_value);
                    else st_.erase_variable(f.variable_name);
                    pc = f.loop_end_index + 1; break;
                }
                case OpKind::Continue: {
                    if (loops.empty()) throw ExecutionError("Continue without frame");
                    auto& f = loops.back();
                    ++f.iteration;
                    if (f.iteration < f.total) {
                        st_.set_variable(f.variable_name, Value{f.iteration});
                        pc = f.loop_start_index + 1;
                    } else {
                        if (f.had_prior_variable) st_.set_variable(f.variable_name, f.prior_value);
                        else st_.erase_variable(f.variable_name);
                        pc = f.loop_end_index + 1;
                        loops.pop_back();
                    }
                    break;
                }
                case OpKind::Call: {
                    EngineRequest rq;
                    rq.action = cur.action;
                    rq.execution_id = exec_id;
                    rq.instruction_id = pc;
                    for (auto& kv : cur.args)
                        rq.arguments.emplace(kv.first, ev_.eval(kv.second));
                    auto eng = reg_.resolve(cur.pillar);
                    auto res = eng->execute(rq);
                    if (res.success) st_.apply_patch(res.patch);
                    for (auto& w : res.warnings) std::cerr << "[WARN] " << w << "\n";
                    ++pc; break;
                }
            }
        }
        sum.final_pc = pc;
        return sum;
    }

private:
    RuntimeState& st_;
    EngineRegistry& reg_;
    const ExpressionEvaluator& ev_;
};

// ---------- runtime ----------
class Runtime {
public:
    explicit Runtime(StateMode m = StateMode::Strict)
        : st_(default_metric_schema(), m), ev_(st_) {}

    void register_default_engines() {
        reg_.add(std::make_shared<DialogueEngine>());
        reg_.add(std::make_shared<CodexEngine>());
        reg_.add(std::make_shared<ObserveEngine>());
    }

    ExecutionSummary execute_text(const std::string& src) {
        Program p = Parser{}.parse_text(src);
        Executor ex(st_, reg_, ev_);
        return ex.run(p, "exec");
    }

    ExecutionSummary execute_file(const std::string& path) {
        std::ifstream in(path);
        if (!in) throw ThorError("Cannot open " + path);
        std::stringstream ss; ss << in.rdbuf();
        return execute_text(ss.str());
    }

    RuntimeState& state() { return st_; }
    EngineRegistry& registry() { return reg_; }
    Value evaluate(const std::string& s) { return ev_.eval(s); }

private:
    RuntimeState st_;
    EngineRegistry reg_;
    ExpressionEvaluator ev_;
};


// ---------- CLI ----------
static void usage() {
    std::cout <<
R"(THOR Engine - VAS DSL runtime

Usage:
  thor run   <script.vas> [--permissive] [--fail-fast]
  thor check <script.vas>
  thor eval  "<expression>"
  thor version
  thor help
)";
}

int main(int argc, char** argv) {
    using namespace thor;
    if (argc < 2) { usage(); return 1; }
    std::string cmd = argv[1];

    try {
        if (cmd == "version") {
            std::cout << thor::VERSION << "\n" << thor::COPYRIGHT << "\n";
            return 0;
        }
        if (cmd == "help") { usage(); return 0; }

        if (cmd == "check" || cmd == "run") {
            if (argc < 3) { usage(); return 1; }
            StateMode mode = StateMode::Strict;
            for (int i = 3; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "--permissive") mode = StateMode::Permissive;
            }
            Runtime rt(mode);
            rt.register_default_engines();
            if (cmd == "check") {
                std::ifstream in(argv[2]);
                if (!in) { std::cerr << "Cannot open " << argv[2] << "\n"; return 2; }
                Parser{}.parse(in);
                std::cout << "OK: " << argv[2] << " parses cleanly.\n";
                return 0;
            }
            auto s = rt.execute_file(argv[2]);
            std::cout << "Executed " << s.executed_instructions << " instructions.\n";
            return s.success ? 0 : 4;
        }

        if (cmd == "eval") {
            if (argc < 3) { usage(); return 1; }
            Runtime rt;
            rt.register_default_engines();
            auto v = rt.evaluate(argv[2]);
            if (auto p = std::get_if<bool>(&v.data))           std::cout << (*p ? "true" : "false") << "\n";
            else if (auto p = std::get_if<std::int64_t>(&v.data)) std::cout << *p << "\n";
            else if (auto p = std::get_if<double>(&v.data))    std::cout << *p << "\n";
            else if (auto p = std::get_if<std::string>(&v.data)) std::cout << *p << "\n";
            else std::cout << "null\n";
            return 0;
        }
    } catch (const ThorError& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 5;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 5;
    }

    usage();
    return 1;
}
