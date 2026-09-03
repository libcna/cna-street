// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file TestSupport.hpp
 * @brief The whole test framework.
 *
 * Deliberately this small. What is under test here is arithmetic and a state
 * machine: a failure needs to say which check failed, on which line, with the
 * two numbers that disagreed, and then stop. A framework dependency would add a
 * build dependency, a discovery mechanism and a vocabulary, and would say
 * exactly the same thing.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace CnaStreet::Test {

inline int gChecks = 0;
inline int gFailures = 0;
inline const char* gCase = "";

inline void beginCase(const char* name)
{
    gCase = name;
    std::printf("  %s\n", name);
}

inline void fail(const char* file, int line, const std::string& what)
{
    ++gFailures;
    std::printf("FAIL %s:%d [%s]\n     %s\n", file, line, gCase, what.c_str());
}

inline void check(bool condition, const char* file, int line, const std::string& what)
{
    ++gChecks;
    if (!condition) fail(file, line, what);
}

inline void checkNear(double actual, double expected, double tolerance, const char* file, int line,
                      const std::string& what)
{
    ++gChecks;
    if (!(std::fabs(actual - expected) <= tolerance))
        fail(file, line,
             what + " -- got " + std::to_string(actual) + ", expected "
                 + std::to_string(expected) + " +/- " + std::to_string(tolerance));
}

inline int summary(const char* suite)
{
    std::printf("%s: %d checks, %d failures\n", suite, gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

}  // namespace CnaStreet::Test

#define CASE(name) ::CnaStreet::Test::beginCase(name)
#define CHECK(cond) ::CnaStreet::Test::check((cond), __FILE__, __LINE__, #cond)
#define CHECK_MSG(cond, msg) ::CnaStreet::Test::check((cond), __FILE__, __LINE__, (msg))
#define CHECK_NEAR(actual, expected, tol) \
    ::CnaStreet::Test::checkNear((actual), (expected), (tol), __FILE__, __LINE__, \
                                 #actual " ~= " #expected)
#define TEST_MAIN(suite) return ::CnaStreet::Test::summary(suite)
