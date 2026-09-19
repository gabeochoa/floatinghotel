#include "test_framework.h"
#include "../../src/util/navigation.h"
#include <atomic>
#include <cstdlib>
#include <new>

static std::atomic<size_t> allocations{0};
static std::atomic<bool> counting{false};
void* operator new(std::size_t size) {
    if (counting.load()) ++allocations;
    if (auto* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

TEST(request_validation_borrows_long_repository_document_and_key_strings) {
    ecs::RepoComponent repo;
    repo.repoPath = "/repository/" + std::string(500, 'r');
    navigation::open(repo, reading::source(std::string(500, 'p') + ".cpp"));
    const std::string key(500, 'k');
    const auto request = navigation::stamp(repo, key);
    allocations = 0;
    bool accepted = true;
    counting = true;
    for (int i = 0; i < 10000; ++i) accepted &= navigation::accepts(repo, request, key);
    counting = false;
    ASSERT_TRUE(accepted);
    ASSERT_EQ(allocations.load(), 0u);
    ++repo.dataGeneration;
    ASSERT_FALSE(navigation::accepts(repo, request, key));
}

int main() { RUN_ALL_TESTS(); }
