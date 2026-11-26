#include "source_find.h"
#include "../util/reading_anchor.h"
#include "../util/file_content.h"
#include "../util/file_page.h"
#include "../util/fuzzy_match.h"
#include "../util/text_decode.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>

namespace git {

namespace {

inline int advance_columns_fast(std::string_view slice) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(slice.data());
    const size_t n = slice.size();
    bool ascii = true;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] > 0x7f) { ascii = false; break; }
    }
    if (ascii) return static_cast<int>(n);
    return reading::column_at_byte(slice, n) - 1;
}

// Fast streaming UTF-8 search: reads the file once in 64 KiB chunks without
// page-decoding, std::istringstream splitting, or syntax-lexing every line.
// Returns std::nullopt when UTF-16 is detected/requested so the caller falls
// back to the existing page-decoded path.
std::optional<ecs::SourceFindResult> find_source_stream_utf8(
    FileRequest request, const std::string& query, std::stop_token stop) {
    if (request.encoding == "utf16le" || request.encoding == "utf16be") return std::nullopt;
    const bool allowUtf16Detect = request.encoding.empty() || request.encoding == "auto";

    std::string blobOid;
    if (!request.revision.empty()) {
        if (request.revision != "INDEX" && !reading::is_object_id(request.revision)) {
            auto resolved = git_run(request.repo, {"rev-parse", "--verify", "--end-of-options", request.revision + "^{commit}"}, stop);
            if (!resolved.success()) {
                ecs::SourceFindResult err;
                err.error = resolved.stderr_str();
                return err;
            }
            request.revision = resolved.stdout_str();
            while (!request.revision.empty() && (request.revision.back() == '\n' || request.revision.back() == '\r'))
                request.revision.pop_back();
        }
        if (request.revision == "INDEX") {
            auto resolved = git_run(request.repo, {"rev-parse", "--verify", "--end-of-options", ":" + request.path}, stop);
            if (!resolved.success()) {
                ecs::SourceFindResult err;
                err.error = resolved.stderr_str();
                return err;
            }
            blobOid = resolved.stdout_str();
            while (!blobOid.empty() && (blobOid.back() == '\n' || blobOid.back() == '\r')) blobOid.pop_back();
        } else {
            auto entry = git_run(request.repo, {"ls-tree", "-l", "-z", request.revision, "--", ":(literal)" + request.path}, stop);
            const auto& out = entry.stdout_str();
            const auto tab = out.find('\t');
            const auto end = out.find('\0');
            if (!entry.success()) {
                ecs::SourceFindResult err;
                err.error = entry.stderr_str();
                return err;
            }
            if (tab == std::string::npos || end == std::string::npos || out.substr(tab + 1, end - tab - 1) != request.path) {
                ecs::SourceFindResult err;
                err.error = "File is unavailable at this revision";
                return err;
            }
            std::string mode, type, oid;
            std::istringstream fields(out.substr(0, tab));
            fields >> mode >> type >> oid;
            if (type != "blob" || !reading::is_object_id(oid)) {
                ecs::SourceFindResult err;
                err.error = type == "commit" ? "Path is a submodule; open its repository to read files"
                                             : "Unable to read file metadata at this revision";
                return err;
            }
            blobOid = std::move(oid);
        }
    }

    ecs::SourceFindResult result;
    const int queryColumns = reading::column_at_byte(query, query.size()) - 1;
    const bool queryHasReplacement = query.find("\xef\xbf\xbd") != std::string::npos;

    bool encodingChecked = false;
    bool needFallback = false;
    bool isBinary = false;
    bool stoppedByLimit = false;
    std::string probe;
    probe.reserve(512);

    int line = 1;
    int lastMatchLine = 0;
    int nextMatchColumn = 1;
    std::string carry;
    int carryStartCol = 1;
    size_t carryPrefix = 0;

    auto search_segment = [&](std::string_view text, int startCol, size_t minEnd) -> bool {
        std::string cleaned;
        if (queryHasReplacement) {
            bool hasHigh = false;
            for (unsigned char c : text) if (c > 0x7f) { hasHigh = true; break; }
            if (hasHigh) {
                cleaned = text_decode::utf8_clean(text, 0).first;
                text = cleaned;
            }
        }
        size_t at = 0, measured = 0;
        int matchCol = startCol;
        while ((at = text.find(query, at)) != std::string_view::npos) {
            if (stop.stop_requested()) return false;
            matchCol += advance_columns_fast(text.substr(measured, at - measured));
            measured = at;
            if (line == lastMatchLine && matchCol < nextMatchColumn) { ++at; continue; }
            if (at + query.size() > minEnd) {
                if (result.matches.size() == 5000) {
                    result.limited = true;
                    stoppedByLimit = true;
                    return false;
                }
                result.matches.push_back({line, matchCol});
                lastMatchLine = line;
                nextMatchColumn = matchCol + queryColumns;
            }
            at += query.size();
        }
        return true;
    };

    auto flush_long_carry = [&]() -> bool {
        if (!search_segment(carry, carryStartCol, carryPrefix)) return false;
        const size_t keep = query.size() > 1 ? query.size() - 1 : 0;
        size_t keepStart = carry.size() > keep ? carry.size() - keep : 0;
        while (keepStart < carry.size() && (static_cast<unsigned char>(carry[keepStart]) & 0xc0) == 0x80) ++keepStart;
        carryStartCol += advance_columns_fast(std::string_view(carry).substr(0, keepStart));
        carryPrefix = carry.size() - keepStart;
        carry.erase(0, keepStart);
        return true;
    };

    auto scan_utf8_bytes = [&](std::string_view bytes) -> bool {
        if (bytes.empty()) return true;
        if (std::memchr(bytes.data(), '\0', bytes.size()) != nullptr) {
            isBinary = true;
            return false;
        }
        result.maxPageBytes = std::max(result.maxPageBytes, std::min<size_t>(bytes.size(), 65536));
        const char* data = bytes.data();
        const size_t n = bytes.size();
        size_t pos = 0;
        while (pos < n) {
            if (stop.stop_requested()) return false;
            const void* found = std::memchr(data + pos, '\n', n - pos);
            if (!found) {
                carry.append(data + pos, n - pos);
                while (carry.size() >= 65536) {
                    if (!flush_long_carry()) return false;
                }
                break;
            }
            const size_t end = static_cast<size_t>(static_cast<const char*>(found) - data);
            if (carry.empty()) {
                if (!search_segment(std::string_view(data + pos, end - pos), 1, 0)) return false;
            } else {
                carry.append(data + pos, end - pos);
                const bool ok = search_segment(carry, carryStartCol, carryPrefix);
                carry.clear();
                carryStartCol = 1;
                carryPrefix = 0;
                if (!ok) return false;
            }
            ++line;
            pos = end + 1;
        }
        return true;
    };

    auto check_probe_and_scan = [&](bool eof) -> bool {
        if (encodingChecked) return true;
        if (probe.size() < 512 && !eof) return true;
        encodingChecked = true;
        if (allowUtf16Detect && (probe.starts_with("\xff\xfe") || probe.starts_with("\xfe\xff") ||
                                 text_decode::looks_utf16(probe, true) || text_decode::looks_utf16(probe, false))) {
            needFallback = true;
            return false;
        }
        size_t start = 0;
        if (probe.starts_with("\xef\xbb\xbf")) start = 3;
        std::string_view view(probe.data() + start, probe.size() - start);
        const bool ok = scan_utf8_bytes(view);
        probe.clear();
        return ok;
    };

    auto consume = [&](std::string_view chunk) -> bool {
        if (stop.stop_requested()) return false;
        result.scannedBytes += chunk.size();
        if (!encodingChecked) {
            const size_t need = 512 - probe.size();
            const size_t take = std::min(need, chunk.size());
            probe.append(chunk.data(), take);
            chunk.remove_prefix(take);
            if (!check_probe_and_scan(false)) return false;
            if (!encodingChecked) return true;
        }
        return scan_utf8_bytes(chunk);
    };

    std::string ioError;
    if (request.revision.empty()) {
        auto rd = file_content::read_working_file(std::filesystem::path(request.repo) / request.path, stop,
            std::numeric_limits<size_t>::max(), consume, 0);
        result.sourceIdentity = std::move(rd.identity);
        if (!rd.error.empty() && !stoppedByLimit && !isBinary && !needFallback) ioError = std::move(rd.error);
    } else {
        result.sourceIdentity = blobOid;
        auto rd = git_run(request.repo, {"cat-file", "blob", blobOid}, stop, consume);
        if (!rd.success() && !rd.raw.outputStopped && !stoppedByLimit && !isBinary && !needFallback)
            ioError = rd.stderr_str();
    }

    if (needFallback) return std::nullopt;
    if (stop.stop_requested()) {
        result.matches.clear();
        result.error = "Find cancelled";
        return result;
    }
    if (isBinary) {
        result.matches.clear();
        result.error = "Find is unavailable for binary files";
        return result;
    }
    if (!ioError.empty()) {
        result.matches.clear();
        result.error = std::move(ioError);
        return result;
    }
    if (!stoppedByLimit) {
        if (!encodingChecked && !check_probe_and_scan(true)) {
            if (needFallback) return std::nullopt;
            if (isBinary) { result.matches.clear(); result.error = "Find is unavailable for binary files"; return result; }
        }
        if (!stoppedByLimit && !carry.empty()) {
            (void)search_segment(carry, carryStartCol, carryPrefix);
        }
    }
    return result;
}

} // namespace

ecs::SourceFindResult find_source(FileRequest request, const std::string& query, std::stop_token stop) {
    ecs::SourceFindResult result;
    if (query.empty()) return result;
    if (query.size() > 65536) { result.error = "Find query exceeds 64 KiB"; return result; }
    if (query.find_first_of("\r\n") != std::string::npos) { result.error = "Find accepts a single line"; return result; }
    if (stop.stop_requested()) { result.error = "Find cancelled"; return result; }
    if (auto fast = find_source_stream_utf8(request, query, stop)) return *fast;
    request.page = {};
    std::string tail;
    int tailColumn = 1;
    int lastMatchLine = 0, nextMatchColumn = 1;
    const int queryColumns = reading::column_at_byte(query, query.size()) - 1;
    while (!stop.stop_requested()) {
        auto content = read_file(request, stop);
        if (!content.error.empty()) { result.error = std::move(content.error); result.matches.clear(); return result; }
        if (content.diff.isBinary) { result.error = "Find is unavailable for binary files"; result.matches.clear(); return result; }
        if (!content.resolvedRevision.empty()) request.revision = content.resolvedRevision;
        result.sourceIdentity = content.page.sourceIdentity;
        result.scannedBytes = content.page.next.offset;
        result.maxPageBytes = std::max(result.maxPageBytes, content.raw.size());
        for (const auto& hunk : content.diff.hunks) {
            int line = hunk.newStart;
            for (const auto& encoded : hunk.lines) {
                if (stop.stop_requested()) break;
                const std::string_view text = std::string_view(encoded).substr(1);
                const auto prefix = tail.size();
                const int column = tail.empty() ? (line == content.page.begin.line ? content.page.begin.column : 1) : tailColumn;
                std::string combined = tail;
                combined.append(text);
                size_t at = 0, measured = 0;
                int matchColumn = column;
                while ((at = combined.find(query, at)) != std::string::npos) {
                    if (stop.stop_requested()) break;
                    matchColumn += reading::column_at_byte(std::string_view(combined).substr(measured, at - measured), at - measured) - 1;
                    measured = at;
                    if (line == lastMatchLine && matchColumn < nextMatchColumn) { ++at; continue; }
                    if (at + query.size() > prefix) {
                        if (result.matches.size() == 5000) { result.limited = true; return result; }
                        result.matches.push_back({line, matchColumn});
                        lastMatchLine = line;
                        nextMatchColumn = matchColumn + queryColumns;
                    }
                    at += query.size();
                }
                tail.clear();
                if (line == content.page.next.line && content.page.next.continuation && content.page.next.offset < content.page.totalBytes) {
                    auto begin = combined.size() > query.size() - 1 ? combined.size() - (query.size() - 1) : 0;
                    while (begin < combined.size() && (static_cast<unsigned char>(combined[begin]) & 0xc0) == 0x80) ++begin;
                    tail = combined.substr(begin);
                    tailColumn = column + reading::column_at_byte(combined, begin) - 1;
                }
                ++line;
            }
        }
        if (content.page.next.offset >= content.page.totalBytes) return result;
        if (content.page.next.offset <= content.page.begin.offset) { result.error = "Find could not advance through the file"; break; }
        request.page = {ecs::FilePageRequest::Action::Next, content.page.next, 0, content.page.sourceIdentity};
        request.detectedEncoding = content.page.encoding;
    }
    result.matches.clear();
    if (stop.stop_requested()) result.error = "Find cancelled";
    return result;
}

async_work::Task<ecs::SourceFindResult> find_source_async(FileRequest request, std::string query) {
    return async_work::launch([request = std::move(request), query = std::move(query)](std::stop_token stop) {
        return find_source(request, query, stop);
    }, async_work::Priority::Background, ecs::SourceFindResult{.error = "Find is busy. Try again."});
}


async_work::Task<ecs::OutlineResult> outline_source_async(FileRequest request) {
    return async_work::launch([request = std::move(request)](std::stop_token stop) mutable {
        ecs::OutlineResult result;
        constexpr size_t kMaxOutlineBytes = 64u << 20;
        // Fast path for working-tree files: read once directly without
        // per-page parse_complete_file + hunk_syntax::annotate overhead.
        if (request.revision.empty() && !stop.stop_requested()) {
            auto rd = file_content::read_working_file(std::filesystem::path(request.repo) / request.path, stop, kMaxOutlineBytes);
            if (stop.stop_requested()) { result.error = "Symbol scan cancelled"; return result; }
            if (rd.error.empty()) {
                auto decoded = text_decode::decode(rd.bytes, request.encoding);
                if (decoded.binary) { result.error = "Binary files have no symbols"; return result; }
                result.symbols = symbol_outline::cached_scan(request.path, decoded.text);
                return result;
            }
            if (rd.totalBytes > kMaxOutlineBytes) {
                result.error = "File is too large to outline (over 64 MiB)";
                return result;
            }
            result.error = std::move(rd.error);
            return result;
        }
        std::string text;
        request.page = {};
        while (!stop.stop_requested()) {
            auto content = read_file(request, stop);
            if (!content.error.empty()) { result.error = std::move(content.error); return result; }
            if (content.diff.isBinary) { result.error = "Binary files have no symbols"; return result; }
            if (!content.resolvedRevision.empty()) request.revision = content.resolvedRevision;
            for (const auto& hunk : content.diff.hunks)
                for (size_t i = 0; i < hunk.lines.size(); ++i) {
                    text.append(std::string_view(hunk.lines[i]).substr(1));
                    // A line split across pages continues on the next one.
                    const bool split = i + 1 == hunk.lines.size() && content.page.next.continuation &&
                        content.page.next.offset < content.page.totalBytes;
                    if (!split) text += '\n';
                }
            if (text.size() > (64u << 20)) { result.error = "File is too large to outline (over 64 MiB)"; return result; }
            if (content.page.next.offset >= content.page.totalBytes) {
                result.symbols = symbol_outline::cached_scan(request.path, text);
                return result;
            }
            if (content.page.next.offset <= content.page.begin.offset) { result.error = "Could not read through the file"; return result; }
            request.page = {ecs::FilePageRequest::Action::Next, content.page.next, 0, content.page.sourceIdentity};
            request.detectedEncoding = content.page.encoding;
        }
        result.error = "Symbol scan cancelled";
        return result;
    }, async_work::Priority::Background, ecs::OutlineResult{.error = "Reader queue is full; reopen Go to Symbol"});
}

async_work::Task<ecs::ProjectSymbolsResult> project_symbols_async(std::string repoPath,
    std::vector<std::string> paths, std::string query) {
    return async_work::launch([repoPath = std::move(repoPath), paths = std::move(paths), query = std::move(query)](std::stop_token stop) {
        ecs::ProjectSymbolsResult result;
        // `a::b::c` matches on the last segment.
        std::string needle = query;
        if (const auto cut = needle.rfind("::"); cut != std::string::npos) needle = needle.substr(cut + 2);
        for (const auto& path : paths) {
            if (result.symbols.size() >= 100 || stop.stop_requested()) break;
            if (!symbol_outline::supported(path)) continue;
            std::ifstream input(std::filesystem::path(repoPath) / path, std::ios::binary);
            if (!input) continue;
            std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            if (text.size() > (2u << 20)) continue;
            if (text.substr(0, 4096).find('\0') != std::string::npos) continue;
            for (const auto& symbol : symbol_outline::cached_scan(path, text)) {
                if (result.symbols.size() >= 100) break;
                if (needle.empty() || fuzzy::score(needle, symbol.name))
                    result.symbols.push_back({symbol.name, symbol.kind, path, symbol.line});
            }
        }
        return result;
    }, async_work::Priority::Background, ecs::ProjectSymbolsResult{.error = "Reader queue is full; reopen project symbols"});
}

}
