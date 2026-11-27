// тесты хранилища в котором вебка держит свои настройки (макросы профили и т.д.)

#include <cstdlib>
#include <filesystem>
#include "check.hpp"
#include "db/store.hpp"

static std::vector<std::string> g_dirs;

static std::string tempDir()
{
  char tmpl[] = "/tmp/monsgeek-store-test-XXXXXX";
  char *dir = mkdtemp(tmpl);

  if (dir == nullptr)
  {
    perror("mkdtemp");
    exit(2);
  }

  g_dirs.push_back(dir);
  return dir;
}

static void testInsertGet()
{
  Store s(tempDir());
  CHECK(s.insert("macro", "k1", std::string("\x00\x01", 2)).empty());
  CHECK(s.get("macro", "k1") == std::optional<std::string>(std::string("\x00\x01", 2)));
  CHECK(!s.get("macro", "нет").has_value());
  CHECK(!s.get("другая", "k1").has_value());
}

static void testOverwrite()
{
  Store s(tempDir());
  s.insert("cfg", "a", "1");
  s.insert("cfg", "a", "2");
  CHECK(s.get("cfg", "a") == std::optional<std::string>("2"));
  CHECK(s.keys("cfg").size() == 1);
}

static void testSurvivesRestart()
{
  auto dir = tempDir();
  Store(dir).insert("cfg", "a", "1");
  CHECK(Store(dir).get("cfg", "a") == std::optional<std::string>("1"));
}

static void testKeysValuesRemove()
{
  Store s(tempDir());
  s.insert("p/../x", "a", "1");
  s.insert("p/../x", "b", "2");
  s.remove("p/../x", "a");
  CHECK(s.keys("p/../x") == std::vector<std::string>{"b"});
  CHECK(s.values("p/../x") == std::vector<std::string>{"2"});
}

// директории для хранилища еще нет
// должна создаться сама
static void testCreatesBaseDir()
{
  auto dir = tempDir() + "/еще/глубже";
  Store s(dir);
  CHECK(s.insert("cfg", "a", "1").empty());
  CHECK(s.get("cfg", "a") == std::optional<std::string>("1"));
}

int main(int argc, char **argv)
{
  const TestCase tests[] = {
      {"insert_get", testInsertGet},
      {"overwrite", testOverwrite},
      {"survives_restart", testSurvivesRestart},
      {"keys_values_remove", testKeysValuesRemove},
      {"creates_base_dir", testCreatesBaseDir},
  };
  int rc = runTests(tests, sizeof(tests) / sizeof(tests[0]), argc, argv);

  for (const auto &d : g_dirs)
  {
    std::filesystem::remove_all(d);
  }

  return rc;
}
