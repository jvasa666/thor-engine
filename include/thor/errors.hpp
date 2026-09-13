#pragma once
#include <stdexcept>
#include <string>

namespace thor {

struct ThorError          : std::runtime_error { using std::runtime_error::runtime_error; };
struct ParseError         : ThorError { using ThorError::ThorError; };
struct ValidationError    : ThorError { using ThorError::ThorError; };
struct ExecutionError     : ThorError { using ThorError::ThorError; };
struct UnknownMetricError : ThorError { using ThorError::ThorError; };
struct UnknownPathError   : ThorError { using ThorError::ThorError; };
struct TypeMismatchError  : ThorError { using ThorError::ThorError; };
struct ExpressionError    : ThorError { using ThorError::ThorError; };
struct ExpressionSyntaxError : ExpressionError { using ExpressionError::ExpressionError; };
struct UnsafeExpressionError : ExpressionError { using ExpressionError::ExpressionError; };
struct ExpressionTypeError   : ExpressionError { using ExpressionError::ExpressionError; };
struct UnknownEngineError : ThorError { using ThorError::ThorError; };

} // namespace thor
