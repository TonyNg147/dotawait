#pragma once

#include "ast.hpp"
#include "async_parser.hpp" // for ParseError

#include <string>
#include <string_view>
#include <variant>

namespace asynctp {

// Lowers one async-marked function's body text: every top-level
// `expr.await` becomes `co_await (expr)` and every top-level bare
// `return`/`return expr;` becomes `co_return`/`co_return expr;`, where
// "top-level" means not inside a nested *ordinary* (non-async) lambda
// defined within the body -- a `return`/`.await` inside such a lambda is
// left completely untouched (that lambda has its own, non-coroutine call
// frame; using `.await` there is a documented MVP limitation, mirroring how
// Rust itself disallows `.await` inside a non-async closure).
//
// Known MVP limitations, documented rather than handled:
//   - The `.await` backward expression scan does not skip over string
//     literals while walking backward (it does stop at a comment outside
//     any ()/[] group); a pathological expression right
//     before `.await` should be parenthesized first (e.g. `(a ? b : c).await`),
//     which the scanner handles trivially as one balanced unit.
//   - Lambda-start detection after a bracket like `return [x](){ ... }` only
//     recognizes a small fixed set of keywords (`return`, `throw`,
//     `co_return`) as still being "expression position" before `[`; any
//     other keyword immediately preceding a lambda is misdetected as an
//     ordinary subscript.
std::variant<std::string, ParseError> lower_body(std::string_view body);

// Produces the full replacement text for one AsyncFunction:
// "::asyncrt::Task<RetType> name(params) { <lowered body> }". The name is
// fully qualified so generated code never needs a `using asyncrt::Task;`.
std::variant<std::string, ParseError> lower_function(const AsyncFunction& fn);

} // namespace asynctp
