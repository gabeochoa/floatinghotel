#include "test_framework.h"
#include "../../src/review_snapshot.h"
#include <cstdlib>
#include <unistd.h>
#include <map>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

struct SnapshotFixture {
    std::string path;
    std::string head;
    SnapshotFixture() {
        std::string pattern = "/tmp/floatinghotel-snapshot-test-XXXXXX";
        auto* created = mkdtemp(pattern.data());
        if (!created) throw std::runtime_error("mkdtemp failed");
        path = created;
        git::git_run(path, {"init", "-q"});
        git::git_run(path, {"config", "user.name", "Snapshot test"});
        git::git_run(path, {"config", "user.email", "test@example.invalid"});
        write("tracked.txt", "committed\n");
        write("unchanged.txt", "baseline\n");
        write("deleted.txt", "deleted before review\n");
        git::git_run(path, {"add", "."});
        git::git_run(path, {"commit", "-qm", "baseline"});
        head = git::git_run(path, {"rev-parse", "HEAD"}).stdout_str();
        head.erase(head.find_last_not_of("\n\r") + 1);
    }
    ~SnapshotFixture() { std::error_code error; std::filesystem::remove_all(path, error); }
    void write(const std::string& file, const std::string& contents) {
        std::ofstream output(path + "/" + file, std::ios::binary);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
    git::GitResult run(bool capture) {
        return review_store::snapshot_async(path, path + "/.git/review.cbor", head, capture, 3, false).get();
    }
};

TEST(snapshot_compares_saved_bytes_and_falls_back_to_head) {
    SnapshotFixture fixture;
    fixture.write("tracked.txt", "reviewed\n");
    fixture.write("new.txt", "new before review\n");
    fixture.write("binary.dat", std::string("\0\xff", 2));
    std::filesystem::remove(fixture.path + "/deleted.txt");
    ASSERT_TRUE(fixture.run(true).success());
    auto unchanged = fixture.run(false);
    ASSERT_TRUE(unchanged.success());
    ASSERT_EQ(nlohmann::json::parse(unchanged.stdout_str()).size(), 0u);
    fixture.write("tracked.txt", "reviewed\nadded later\n");
    fixture.write("unchanged.txt", "new baseline\n");
    fixture.write("deleted.txt", "restored\n");
    fixture.write("binary.dat", std::string("\0\xfe", 2));
    std::filesystem::remove(fixture.path + "/new.txt");
    auto changed = fixture.run(false);
    ASSERT_TRUE(changed.success());
    auto patches = nlohmann::json::parse(changed.stdout_str());
    ASSERT_EQ(patches.size(), 5u);
    std::map<std::string, std::string> byFile;
    for (const auto& entry : patches) byFile[entry["file"]] = entry["patch"];
    ASSERT_TRUE(byFile["tracked.txt"].find("+added later") != std::string::npos);
    ASSERT_TRUE(byFile["tracked.txt"].find("-committed") == std::string::npos);
    ASSERT_TRUE(byFile["unchanged.txt"].find("-baseline") != std::string::npos);
    ASSERT_TRUE(byFile["new.txt"].find("-new before review") != std::string::npos);
    ASSERT_TRUE(byFile["deleted.txt"].find("+restored") != std::string::npos);
    ASSERT_TRUE(byFile["binary.dat"].find("Binary files") != std::string::npos);
    ASSERT_TRUE(git::git_run(fixture.path, {"diff", "--cached", "--name-only"}).stdout_str().empty());
}

TEST(snapshot_failure_preserves_the_previous_baseline) {
    SnapshotFixture fixture;
    fixture.write("tracked.txt", "reviewed\n");
    ASSERT_TRUE(fixture.run(true).success());
    std::filesystem::create_symlink("tracked.txt", fixture.path + "/link");
    ASSERT_FALSE(fixture.run(true).success());
    std::filesystem::remove(fixture.path + "/link");
    ASSERT_EQ(nlohmann::json::parse(fixture.run(false).stdout_str()).size(), 0u);
}

int main() { RUN_ALL_TESTS(); }
