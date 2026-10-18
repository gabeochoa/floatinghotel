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

#ifdef __APPLE__
// px0 previews WebP, AVIF and ICO; stb_image cannot, so ImageIO does. 2x2
// fixtures: red, green, blue, and white at alpha 128 (made with Pillow; the
// ICO holds 16x16 and 32x32 copies, and the larger must win; ImageIO rejects
// icons much smaller than that).
static const std::string webpBytes(
    "\x52\x49\x46\x46\x30\x00\x00\x00\x57\x45\x42\x50\x56\x50\x38\x4c\x23\x00\x00\x00\x2f\x01\x40\x00"
    "\x10\x1f\x20\x10\x48\xde\x1f\x3a\x8d\xf9\x17\x10\x14\xfc\x1f\x9d\x80\xa0\xe8\xba\xe5\x22\x66\x0f"
    "\xc2\x0d\x18\x22\xfa\x1f\x02\x00"
    , 56);
static const std::string avifBytes(
    "\x00\x00\x00\x20\x66\x74\x79\x70\x61\x76\x69\x66\x00\x00\x00\x00\x61\x76\x69\x66\x6d\x69\x66\x31"
    "\x6d\x69\x61\x66\x4d\x41\x31\x42\x00\x00\x01\x86\x6d\x65\x74\x61\x00\x00\x00\x00\x00\x00\x00\x21"
    "\x68\x64\x6c\x72\x00\x00\x00\x00\x00\x00\x00\x00\x70\x69\x63\x74\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x0e\x70\x69\x74\x6d\x00\x00\x00\x00\x00\x01\x00\x00\x00\x2c\x69"
    "\x6c\x6f\x63\x00\x00\x00\x00\x44\x00\x00\x02\x00\x01\x00\x00\x00\x01\x00\x00\x01\xd9\x00\x00\x00"
    "\x30\x00\x02\x00\x00\x00\x01\x00\x00\x01\xae\x00\x00\x00\x2b\x00\x00\x00\x42\x69\x69\x6e\x66\x00"
    "\x00\x00\x00\x00\x02\x00\x00\x00\x1a\x69\x6e\x66\x65\x02\x00\x00\x00\x00\x01\x00\x00\x61\x76\x30"
    "\x31\x43\x6f\x6c\x6f\x72\x00\x00\x00\x00\x1a\x69\x6e\x66\x65\x02\x00\x00\x00\x00\x02\x00\x00\x61"
    "\x76\x30\x31\x41\x6c\x70\x68\x61\x00\x00\x00\x00\x1a\x69\x72\x65\x66\x00\x00\x00\x00\x00\x00\x00"
    "\x0e\x61\x75\x78\x6c\x00\x02\x00\x01\x00\x01\x00\x00\x00\xc3\x69\x70\x72\x70\x00\x00\x00\x9d\x69"
    "\x70\x63\x6f\x00\x00\x00\x14\x69\x73\x70\x65\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00\x02\x00"
    "\x00\x00\x10\x70\x69\x78\x69\x00\x00\x00\x00\x03\x08\x08\x08\x00\x00\x00\x0c\x61\x76\x31\x43\x81"
    "\x00\x0c\x00\x00\x00\x00\x13\x63\x6f\x6c\x72\x6e\x63\x6c\x78\x00\x01\x00\x0d\x00\x06\x80\x00\x00"
    "\x00\x0e\x70\x69\x78\x69\x00\x00\x00\x00\x01\x08\x00\x00\x00\x0c\x61\x76\x31\x43\x81\x00\x1c\x00"
    "\x00\x00\x00\x38\x61\x75\x78\x43\x00\x00\x00\x00\x75\x72\x6e\x3a\x6d\x70\x65\x67\x3a\x6d\x70\x65"
    "\x67\x42\x3a\x63\x69\x63\x70\x3a\x73\x79\x73\x74\x65\x6d\x73\x3a\x61\x75\x78\x69\x6c\x69\x61\x72"
    "\x79\x3a\x61\x6c\x70\x68\x61\x00\x00\x00\x00\x1e\x69\x70\x6d\x61\x00\x00\x00\x00\x00\x00\x00\x02"
    "\x00\x01\x04\x01\x02\x83\x04\x00\x02\x04\x01\x05\x86\x07\x00\x00\x00\x63\x6d\x64\x61\x74\x12\x00"
    "\x0a\x04\x18\x00\x36\x15\x32\x21\x10\x00\x88\x6a\x0f\xff\xff\xfb\xe9\x7e\x59\x78\xe5\xe3\x98\x15"
    "\xbc\x9b\x8e\x60\x57\x79\xcc\x9c\x15\xde\x7b\x9e\xe7\xb9\xee\x82\x80\x12\x00\x0a\x08\x18\x00\x36"
    "\x08\x08\x68\x34\x20\x32\x22\x10\x00\x00\x8a\xce\x47\xff\xff\xfd\xb3\x42\xec\xc0\xeb\x7f\x04\xd0"
    "\xb2\xa8\x39\x57\xf2\xe3\x11\x42\xb4\x1b\x0c\xc3\x30\xcc\x33\x15\x40"
    , 521);
static const std::string icoBytes(
    "\x00\x00\x01\x00\x02\x00\x10\x10\x00\x00\x00\x00\x20\x00\xd9\x00\x00\x00\x26\x00\x00\x00\x20\x20"
    "\x00\x00\x00\x00\x20\x00\x83\x00\x00\x00\xff\x00\x00\x00\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00"
    "\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x10\x00\x00\x00\x10\x08\x06\x00\x00\x00\x1f\xf3\xff\x61\x00"
    "\x00\x00\xa0\x49\x44\x41\x54\x78\x9c\xd5\x92\x41\x0e\xc2\x30\x0c\x04\x27\x21\x45\x9c\xc2\x85\x63"
    "\x79\x36\xaf\xe1\x39\x7d\x41\x41\x40\x53\xd4\x18\x45\x6d\x84\x68\x1d\x2e\x1c\x80\xb5\xa2\x95\x1c"
    "\x7b\xb5\xb1\x83\x80\xcc\x4f\x34\x23\xb7\x1e\xf1\x2d\x92\xc2\xc4\x91\xe7\x61\xf9\x10\xf6\xff\x05"
    "\x9c\x60\x16\x49\x79\x61\x79\x2f\x60\x94\x82\x2c\x59\x01\x71\xf2\x28\xa6\x20\x70\xc2\xab\x0e\x52"
    "\xfd\x19\x61\x33\xf4\xfd\x3d\xf5\x47\xd4\x9d\xb9\x3d\xcd\x32\x3b\x99\x5a\x07\xc2\xf1\x72\x3d\xd4"
    "\xdb\xa1\x0b\x56\x9f\x97\xd3\x1c\x64\xac\x80\x5a\x7c\xb7\x83\x5b\x69\xe0\x4e\x9b\x41\x46\x85\x10"
    "\xb0\xa9\xd1\x16\x05\x44\xd9\xc2\xf3\x25\xe5\xbb\xdf\xf9\x48\xf6\xeb\x02\x0f\xf9\x4b\x45\x7a\x81"
    "\xd4\x43\xca\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00"
    "\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x20\x00\x00\x00\x20\x08\x06\x00\x00\x00\x73\x7a\x7a\xf4"
    "\x00\x00\x00\x4a\x49\x44\x41\x54\x78\x9c\xed\x96\xb1\x09\x00\x31\x0c\xc4\xe4\xe3\x17\xcf\xe4\x09"
    "\x99\xc1\x85\x79\x22\xf5\x07\xc2\x6a\x5c\x1b\x36\x0d\xaa\xb5\x86\x30\x4c\x14\xc0\x04\xc3\x44\x01"
    "\x4c\x30\x4c\x14\xe0\xf5\x04\x5f\xf5\xde\x81\xbb\x5e\xbf\xbe\x40\x14\xc0\x04\xc3\x44\x01\x4c\x30"
    "\x4c\x14\xe0\xf5\x04\x07\xe3\x10\x05\xc0\x97\x8c\x3f\x64\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42"
    "\x60\x82"
    , 386);

TEST(imageio_decodes_webp_avif_and_the_largest_ico_entry_with_straight_alpha) {
    auto webp = image_content::decode(webpBytes);
    ASSERT_TRUE(webp.error.empty());
    ASSERT_EQ(webp.width, 2);
    ASSERT_EQ(webp.height, 2);
    const auto* p = webp.pixels.get();
    ASSERT_TRUE(p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255);
    ASSERT_TRUE(p[8] == 0 && p[9] == 0 && p[10] == 255);
    // Straight alpha, as stb returns: premultiplied white would read ~128.
    ASSERT_TRUE(p[12] >= 250 && p[15] >= 126 && p[15] <= 130);
    ASSERT_EQ(image_content::decodedBytes.load(), 16u);
    auto avif = image_content::decode(avifBytes);
    ASSERT_TRUE(avif.error.empty());
    // Lossy 4:2:0 averages a 2x2 image's chroma, so only the shape is exact.
    ASSERT_EQ(avif.width, 2);
    ASSERT_EQ(avif.height, 2);
    auto ico = image_content::decode(icoBytes);
    ASSERT_TRUE(ico.error.empty());
    ASSERT_EQ(ico.width, 32);
    ASSERT_EQ(ico.height, 32);
    ASSERT_FALSE(image_content::decode(webpBytes.substr(0, 20)).error.empty());
}
#endif

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
