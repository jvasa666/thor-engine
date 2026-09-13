#pragma once

#include <memory>
#include <string>

#include "thor/models.hpp"

namespace thor {

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

} // namespace thor
