#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace thor {

struct Value;
using ValueList = std::vector<Value>;
using ValueMap  = std::map<std::string, Value>;

struct Value {
    using ListPtr = std::shared_ptr<const ValueList>;
    using MapPtr  = std::shared_ptr<const ValueMap>;
    using Storage = std::variant<std::monostate, bool, std::int64_t, double,
                                 std::string, ListPtr, MapPtr>;
    Storage data;

    Value() = default;
    Value(bool v)         : data(v) {}
    Value(std::int64_t v) : data(v) {}
    Value(int v)          : data(static_cast<std::int64_t>(v)) {}
    Value(double v)       : data(v) {}
    Value(std::string v)  : data(std::move(v)) {}
    Value(const char* v)  : data(std::string(v)) {}
    Value(const ValueList& v) : data(std::make_shared<const ValueList>(v)) {}
    Value(ValueList&& v)      : data(std::make_shared<const ValueList>(std::move(v))) {}
    Value(const ValueMap& v)  : data(std::make_shared<const ValueMap>(v)) {}
    Value(ValueMap&& v)       : data(std::make_shared<const ValueMap>(std::move(v))) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(data); }
    bool is_list() const { return std::holds_alternative<ListPtr>(data); }
    bool is_map()  const { return std::holds_alternative<MapPtr>(data); }
};

inline Value make_map(ValueMap v)   { return Value{std::move(v)}; }
inline Value make_list(ValueList v) { return Value{std::move(v)}; }

bool value_equal(const Value& a, const Value& b);

inline bool operator==(const Value& a, const Value& b) { return value_equal(a, b); }
inline bool operator!=(const Value& a, const Value& b) { return !value_equal(a, b); }

struct SourceLocation {
    std::size_t line = 0;
    std::size_t column = 1;
};

enum class OpKind {
    Call, LoopStart, LoopEnd, IfStart, Else, IfEnd, Break, Continue,
};

struct Instruction {
    OpKind kind = OpKind::Call;
    SourceLocation loc{};

    std::string pillar;
    std::string action;
    std::vector<std::pair<std::string, std::string>> args;

    std::string loop_var;
    std::string loop_count_expr;
    std::string condition_expr;

    std::optional<std::size_t> jump;
    std::optional<std::size_t> alt;
};

struct Program {
    std::vector<Instruction> instructions;
    std::size_t step_limit = 1'000'000;
};

} // namespace thor
