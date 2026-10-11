#pragma once

// Umbrella header: everything a program using asyncrt needs. asynctp
// injects this include into every generated file that contains an `async`
// function, so .async.cpp sources don't have to include it themselves.

#include "executor.hpp"
#include "task.hpp"
#include "yield.hpp"
