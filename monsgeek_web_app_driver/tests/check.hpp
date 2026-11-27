#pragma once

#include <cstddef>
#include <cstring>
#include <iostream>

static int g_failed = 0;
static const char *g_current = "";

#define CHECK(cond)                                                                                                    \
  do                                                                                                                   \
  {                                                                                                                    \
    if (!(cond))                                                                                                       \
    {                                                                                                                  \
      std::cout << "  FAIL [" << g_current << "] line " << __LINE__ << ": " << #cond << std::endl;                     \
      ++g_failed;                                                                                                      \
    }                                                                                                                  \
  } while (0)

struct TestCase
{
  const char *name;
  void (*run)();
};

// argv[1] - имя одного теста, если нужен только он
static int runTests(const TestCase *tests, size_t count, int argc, char **argv)
{
  int total = 0;

  for (size_t i = 0; i < count; ++i)
  {
    if (argc > 1 && strcmp(argv[1], tests[i].name) != 0)
    {
      continue;
    }

    g_current = tests[i].name;
    int before = g_failed;
    ++total;

    try
    {
      tests[i].run();
    }

    catch (const std::exception &e)
    {
      std::cout << "  EXCEPTION [" << tests[i].name << "]: " << e.what() << std::endl;
      ++g_failed;
    }

    std::cout << (g_failed == before ? "OK      " : "FAILED  ") << tests[i].name << std::endl;
  }

  std::cout << total << " test(s), failures: " << g_failed << std::endl;
  return g_failed == 0 ? 0 : 1;
}
