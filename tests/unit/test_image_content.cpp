#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include "test_framework.h"
#include "../../src/git/image_content.h"
#include "../../src/git/git_runner.h"
#include "../../src/util/async_task.h"
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

static std::string bmp(int width = 1, int height = 1) {
    std::string bytes(58, '\0');
    bytes[0] = 'B'; bytes[1] = 'M';
    auto integer = [&](size_t offset, int value) {
        for (int i = 0; i < 4; ++i) bytes[offset + i] = static_cast<char>((value >> (i * 8)) & 255);
    };
    integer(2, 58); integer(10, 54); integer(14, 40);
    integer(18, width); integer(22, height);
    bytes[26] = 1; bytes[28] = 24; bytes[56] = static_cast<char>(255);
    return bytes;
}

TEST(image_decode_validates_dimensions_bytes_and_releases_owned_memory) {
    ASSERT_EQ(image_content::decodedBytes.load(), 0u);
    {
        auto decoded = image_content::decode(bmp());
        ASSERT_TRUE(decoded.error.empty());
        ASSERT_EQ(decoded.width, 1);
        ASSERT_EQ(decoded.height, 1);
        ASSERT_EQ(decoded.pixels.get()[0], 255);
        ASSERT_EQ(image_content::decodedBytes.load(), 4u);
    }
    ASSERT_EQ(image_content::decodedBytes.load(), 0u);
    ASSERT_FALSE(image_content::decode("not an image").error.empty());
    ASSERT_FALSE(image_content::decode(bmp(9000, 1)).error.empty());
    ASSERT_TRUE(image_content::decode(std::string(image_content::encodedLimit + 1, 'x')).error.find("16 MB") != std::string::npos);
    std::stop_source cancelled;
    cancelled.request_stop();
    ASSERT_TRUE(image_content::decode(bmp(), cancelled.get_token()).error.find("cancelled") != std::string::npos);
    image_content::decodedBytes.store(image_content::totalDecodedLimit);
    ASSERT_TRUE(image_content::decode(bmp()).error.find("memory limit") != std::string::npos);
    image_content::decodedBytes.store(0);
}

TEST(image_reads_work_on_workers_preserve_revisions_and_report_queue_rejection) {
    char pattern[] = "/tmp/fh-image-unit.XXXXXX";
    auto* directory = mkdtemp(pattern);
    ASSERT_TRUE(directory != nullptr);
    std::filesystem::path path(directory);
    { std::ofstream file(path / "image.bmp", std::ios::binary); file << bmp(); }
    ASSERT_TRUE(git::git_run(directory, {"init", "-q"}).success());
    ASSERT_TRUE(git::git_run(directory, {"add", "."}).success());
    ASSERT_TRUE(git::git_run(directory, {"-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "-qm", "baseline"}).success());
    auto caller = std::this_thread::get_id();
    auto task = async_work::launch([directory, caller](std::stop_token stop) {
        ASSERT_TRUE(std::this_thread::get_id() != caller);
        return image_content::read(directory, "image.bmp", "HEAD", stop);
    });
    ASSERT_TRUE(task.get().error.empty());
    ASSERT_TRUE(image_content::read(directory, "image.bmp", "INDEX").error.empty());
    ASSERT_TRUE(image_content::read(directory, "image.bmp", "").error.empty());
    std::filesystem::create_symlink("image.bmp", path / "link.bmp");
    ASSERT_EQ(image_content::read(directory, "link.bmp", "").error, "Symbolic links are not image previews");
    ASSERT_EQ(mkfifo((path / "pipe.bmp").c_str(), 0600), 0);
    ASSERT_FALSE(image_content::read(directory, "pipe.bmp", "").error.empty());
    ASSERT_FALSE(image_content::read(directory, "missing.bmp", "").error.empty());
    std::stop_source cancelled;
    cancelled.request_stop();
    for (const auto& revision : {"", "HEAD", "INDEX"})
        ASSERT_TRUE(image_content::read(directory, "image.bmp", revision, cancelled.get_token()).error.find("cancelled") != std::string::npos);
    { std::ofstream file(path / "large.bmp", std::ios::binary); file.seekp(image_content::encodedLimit); file.put('x'); }
    ASSERT_TRUE(image_content::read(directory, "large.bmp", "").error.find("16 MB") != std::string::npos);
    async_work::Executor full(1, 0);
    auto rejected = async_work::launch_on(full, [](std::stop_token) { return image_content::Decoded{}; },
        async_work::Priority::Foreground, image_content::Decoded{.error = "Background queue is full"});
    ASSERT_EQ(rejected.get().error, "Background queue is full");
    std::filesystem::remove_all(path);
}

int main() { RUN_ALL_TESTS(); }
