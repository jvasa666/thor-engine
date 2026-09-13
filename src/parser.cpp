#include "thor/parser.hpp"
#include "thor/errors.hpp"

#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace thor {

namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string strip(const std::string& raw) {
    // Remove # comment unless inside a quote
    std::string s; bool dq = false, sq = false;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c == '\\' && i + 1 < raw.size() && (dq || sq)) {
            s.push_back(c); s.push_back(raw[++i]); continue;
        }
        if      (c == '"'  && !sq) dq = !dq;
        else if (c == '\'' && !dq) sq = !sq;
        else if (c == '#'  && !dq && !sq) break;
        s.push_back(c);
    }
    return trim(s);
}

} // namespace

Program Parser::parse(std::istream& in) const {
    Program prog;
    std::vector<std::size_t> loops;
    std::vector<std::size_t> ifs;
    std::vector<bool> if_has_else;

    std::string raw; std::size_t line = 0;
    while (std::getline(in, raw)) {
        ++line;
        std::string s = strip(raw);
        if (s.empty()) continue;

        if (s.rfind("Block ", 0) == 0) continue; // skip block markers

        if (s == "Break") {
            if (loops.empty())
                throw ValidationError("Break outside loop at line " + std::to_string(line));
            Instruction i; i.kind = OpKind::Break; i.loc = {line, 1};
            prog.instructions.push_back(i); continue;
        }
        if (s == "Continue") {
            if (loops.empty())
                throw ValidationError("Continue outside loop at line " + std::to_string(line));
            Instruction i; i.kind = OpKind::Continue; i.loc = {line, 1};
            prog.instructions.push_back(i); continue;
        }

        if (s.rfind("Loop Iterations:", 0) == 0) {
            std::string body = s.substr(16);
            auto as_pos = body.find(" AS:");
            if (as_pos == std::string::npos)
                throw ParseError("Loop missing ' AS:' at line " + std::to_string(line));
            std::string count = trim(body.substr(0, as_pos));
            std::string varname = trim(body.substr(as_pos + 4));
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
            if (loops.empty())
                throw ValidationError("Loop End without Loop at line " + std::to_string(line));
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
            if (trim(cond).empty())
                throw ParseError("Empty If condition at line " + std::to_string(line));
            Instruction i; i.kind = OpKind::IfStart; i.loc = {line, 1};
            i.condition_expr = cond;
            ifs.push_back(prog.instructions.size());
            if_has_else.push_back(false);
            prog.instructions.push_back(i); continue;
        }
        if (s == "Else:") {
            if (ifs.empty())
                throw ValidationError("Else without If at line " + std::to_string(line));
            if (if_has_else.back())
                throw ValidationError("Duplicate Else at line " + std::to_string(line));
            if_has_else.back() = true;
            Instruction i; i.kind = OpKind::Else; i.loc = {line, 1};
            i.jump = ifs.back();
            std::size_t else_idx = prog.instructions.size();
            prog.instructions.push_back(i);
            prog.instructions[ifs.back()].alt = else_idx;
            continue;
        }
        if (s == "If End") {
            if (ifs.empty())
                throw ValidationError("If End without If at line " + std::to_string(line));
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
        std::size_t p = 0;
        while (p < rest.size()) {
            while (p < rest.size() && std::isspace((unsigned char)rest[p])) ++p;
            std::size_t kstart = p;
            while (p < rest.size() &&
                   (std::isalnum((unsigned char)rest[p]) || rest[p] == '_')) ++p;
            if (p == kstart) break;
            std::string key = rest.substr(kstart, p - kstart);
            while (p < rest.size() && std::isspace((unsigned char)rest[p])) ++p;
            if (p >= rest.size() || rest[p] != ':') break;
            ++p;
            while (p < rest.size() && std::isspace((unsigned char)rest[p])) ++p;
            std::string val;
            if (p < rest.size() && (rest[p] == '"' || rest[p] == '\'')) {
                char q = rest[p];
                val.push_back(q);
                ++p;
                while (p < rest.size() && rest[p] != q) val.push_back(rest[p++]);
                if (p < rest.size()) { val.push_back(q); ++p; }
            } else {
                while (p < rest.size() && !std::isspace((unsigned char)rest[p]))
                    val.push_back(rest[p++]);
            }
            i.args.emplace_back(key, val);
        }
        prog.instructions.push_back(i);
    }
    if (!loops.empty()) throw ValidationError("Unterminated Loop");
    if (!ifs.empty())   throw ValidationError("Unterminated If");
    return prog;
}

Program Parser::parse_text(const std::string& text) const {
    std::istringstream in(text);
    return parse(in);
}

} // namespace thor
