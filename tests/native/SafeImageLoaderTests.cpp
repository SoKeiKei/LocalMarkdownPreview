#include "SafeImageLoader.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace {

using LocalMarkdownPreview::LoadSafeMarkdownImage;
using LocalMarkdownPreview::SafeImageLoadLimits;
using LocalMarkdownPreview::SafeImageLoadResult;
using LocalMarkdownPreview::SafeImageLoadStatus;

int g_assertions = 0;
int g_failures = 0;
int g_skipped = 0;

void Check(bool condition, const char* message)
{
    ++g_assertions;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

void CheckStatus(const SafeImageLoadResult& result, SafeImageLoadStatus expected, const char* message)
{
    Check(result.status == expected, message);
}

class TestHandle final {
public:
    explicit TestHandle(HANDLE handle = nullptr) noexcept : _handle(handle) {}
    TestHandle(const TestHandle&) = delete;
    TestHandle& operator=(const TestHandle&) = delete;
    ~TestHandle()
    {
        if (_handle != nullptr && _handle != INVALID_HANDLE_VALUE) {
            CloseHandle(_handle);
        }
    }
    bool IsValid() const noexcept { return _handle != nullptr && _handle != INVALID_HANDLE_VALUE; }
    HANDLE Get() const noexcept { return _handle; }

private:
    HANDLE _handle;
};

class TemporaryDirectory final {
public:
    bool Create()
    {
        wchar_t tempPath[MAX_PATH]{};
        const DWORD tempLength = GetTempPathW(static_cast<DWORD>(_countof(tempPath)), tempPath);
        if (tempLength == 0 || tempLength >= _countof(tempPath)) {
            return false;
        }

        wchar_t candidate[MAX_PATH]{};
        if (GetTempFileNameW(tempPath, L"LMP", 0, candidate) == 0) {
            return false;
        }
        DeleteFileW(candidate);
        if (!CreateDirectoryW(candidate, nullptr)) {
            return false;
        }
        _path = candidate;
        return true;
    }

    ~TemporaryDirectory()
    {
        if (!_path.empty()) {
            std::error_code error;
            std::filesystem::remove_all(std::filesystem::path(_path), error);
        }
    }

    const std::wstring& Path() const noexcept { return _path; }

private:
    std::wstring _path;
};

std::wstring Join(const std::wstring& directory, const std::wstring& name)
{
    std::wstring result = directory;
    if (!result.empty() && result.back() != L'\\') {
        result.push_back(L'\\');
    }
    result.append(name);
    return result;
}

bool MakeDirectory(const std::wstring& path)
{
    return CreateDirectoryW(path.c_str(), nullptr) != FALSE || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool WriteBytes(const std::wstring& path, const std::uint8_t* bytes, std::size_t length)
{
    TestHandle file(CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));
    if (!file.IsValid()) {
        return false;
    }

    std::size_t offset = 0;
    while (offset < length) {
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(length - offset, 1u << 20));
        DWORD written = 0;
        if (!WriteFile(file.Get(), bytes + offset, requested, &written, nullptr) || written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

bool WriteBytes(const std::wstring& path, const std::vector<std::uint8_t>& bytes)
{
    return WriteBytes(path, bytes.data(), bytes.size());
}

bool WriteText(const std::wstring& path, const char* text)
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text);
    return WriteBytes(path, bytes, std::strlen(text));
}

bool CreateSizedFile(const std::wstring& path, std::uint64_t size)
{
    TestHandle file(CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));
    if (!file.IsValid()) {
        return false;
    }
    LARGE_INTEGER offset{};
    offset.QuadPart = static_cast<LONGLONG>(size);
    if (!SetFilePointerEx(file.Get(), offset, nullptr, FILE_BEGIN) || !SetEndOfFile(file.Get())) {
        return false;
    }
    return true;
}

std::vector<std::uint8_t> BuildOversizedBmp(std::uint32_t width, std::uint32_t height)
{
    const std::uint64_t rowStride = ((static_cast<std::uint64_t>(width) * 3u + 3u) / 4u) * 4u;
    const std::uint64_t imageBytes = rowStride * height;
    const std::uint64_t fileBytes = 54u + imageBytes;
    if (fileBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return {};
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fileBytes), 0);
    auto Set16 = [&](std::size_t offset, std::uint16_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
        bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    };
    auto Set32 = [&](std::size_t offset, std::uint32_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
        bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
        bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
        bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
    };
    Set16(0, 0x4D42);
    Set32(2, static_cast<std::uint32_t>(fileBytes));
    Set32(10, 54);
    Set32(14, 40);
    Set32(18, width);
    Set32(22, height);
    Set16(26, 1);
    Set16(28, 24);
    Set32(34, static_cast<std::uint32_t>(imageBytes));
    return bytes;
}

const std::vector<std::uint8_t>& OneByOnePng()
{
    static const std::vector<std::uint8_t> bytes{
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x04, 0x00, 0x00, 0x00, 0xB5, 0x1C, 0x0C, 0x02,
        0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54,
        0x78, 0xDA, 0x63, 0x64, 0xF8, 0x0F, 0x00, 0x01, 0x05, 0x01, 0x01,
        0x27, 0x18, 0xE3, 0x66, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
        0xAE, 0x42, 0x60, 0x82};
    return bytes;
}

void RunTests(const std::wstring& root)
{
    const std::wstring documents = Join(root, L"可信 文档目录");
    const std::wstring imageDirectory = Join(documents, L"图片 空格目录");
    const std::wstring document = Join(documents, L"预览 文档.md");
    const std::wstring image = Join(imageDirectory, L"一 张.png");
    Check(MakeDirectory(documents), "create trusted document directory");
    Check(MakeDirectory(imageDirectory), "create image directory");
    Check(WriteText(document, "# trusted markdown\n"), "create trusted markdown file");
    Check(WriteBytes(image, OneByOnePng()), "create 1x1 PNG");

    const SafeImageLoadResult loaded = LoadSafeMarkdownImage(document, L"图片 空格目录/一 张.png");
    CheckStatus(loaded, SafeImageLoadStatus::Loaded, "1x1 PNG should load");
    Check(loaded.Succeeded(), "loaded result should report success");
    Check(loaded.width == 1 && loaded.height == 1, "loaded dimensions should be 1x1");
    Check(loaded.stride == 4 && loaded.pixels.size() == 4, "loaded pixels should be one BGRA row");
    Check(loaded.encoded_data == OneByOnePng(),
          "loaded result should retain the validated encoded bytes");
    Check(loaded.normalized_path.find(L"一 张.png") != std::wstring::npos,
          "result should carry the normalized final path");
    Check(loaded.encoded_bytes == OneByOnePng().size(), "result should carry encoded byte count");

    CheckStatus(LoadSafeMarkdownImage(L"", L"图片 空格目录/一 张.png"),
                SafeImageLoadStatus::NoTrustedDocumentPath,
                "empty trusted document path should be rejected");
    CheckStatus(LoadSafeMarkdownImage(Join(root, L"missing.md"), L"图片 空格目录/一 张.png"),
                SafeImageLoadStatus::NoTrustedDocumentPath,
                "missing trusted document path should be rejected");

    const std::vector<std::wstring> unsafeSources{
        L"",
        L"..\\一 张.png",
        L"子目录\\..\\一 张.png",
        L"C:\\outside.png",
        L"\\server\\share\\outside.png",
        L"\\\\?\\C:\\outside.png",
        L"\\\\.\\pipe\\outside.png",
        L"http://example.invalid/outside.png",
        L"https://example.invalid/outside.png",
        L"ftp://example.invalid/outside.png",
        L"data:image/png;base64,AA==",
        L"javascript:alert(1).png",
        L"file:///outside.png",
        L"一 张.png:secret",
        L"NUL.png",
    };
    for (const std::wstring& source : unsafeSources) {
        CheckStatus(LoadSafeMarkdownImage(document, source), SafeImageLoadStatus::UnsafePath,
                     "unsafe or protocol image source should be rejected");
    }

    CheckStatus(LoadSafeMarkdownImage(document, L"missing.png"), SafeImageLoadStatus::NotFound,
                "missing image should report NotFound");

    const std::wstring svg = Join(documents, L"fake.svg");
    Check(WriteText(svg, "<svg><script>alert(1)</script></svg>"), "create fake SVG");
    CheckStatus(LoadSafeMarkdownImage(document, L"fake.svg"), SafeImageLoadStatus::UnsupportedFormat,
                "SVG should be rejected as unsupported");

    const std::wstring broken = Join(documents, L"broken.png");
    const std::vector<std::uint8_t> brokenBytes{0x89, 0x50, 0x4E, 0x47, 0x00, 0x01, 0x02};
    Check(WriteBytes(broken, brokenBytes), "create corrupt PNG");
    CheckStatus(LoadSafeMarkdownImage(document, L"broken.png"), SafeImageLoadStatus::DecodeFailed,
                "corrupt PNG should report DecodeFailed");

    const std::wstring large = Join(documents, L"large.png");
    Check(CreateSizedFile(large, 10u * 1024u * 1024u + 1u), "create encoded-too-large file");
    const SafeImageLoadResult tooLarge = LoadSafeMarkdownImage(document, L"large.png");
    CheckStatus(tooLarge, SafeImageLoadStatus::EncodedTooLarge, "encoded size limit should be enforced");
    Check(tooLarge.encoded_bytes == 10u * 1024u * 1024u + 1u,
          "encoded-too-large result should report actual size");

    const std::wstring oversizedDimensions = Join(documents, L"wide.bmp");
    Check(WriteBytes(oversizedDimensions, BuildOversizedBmp(4097, 1)), "create oversized BMP");
    const SafeImageLoadResult dimensions = LoadSafeMarkdownImage(document, L"wide.bmp");
    CheckStatus(dimensions, SafeImageLoadStatus::DimensionsTooLarge,
                "dimension limit should be enforced before pixel allocation");
    Check(dimensions.width == 4097 && dimensions.height == 1,
          "dimension-limit result should report decoder dimensions");

    SafeImageLoadLimits smallDecodedBudget;
    smallDecodedBudget.max_decoded_bytes = 3;
    CheckStatus(LoadSafeMarkdownImage(document, L"图片 空格目录/一 张.png", smallDecodedBudget),
                SafeImageLoadStatus::DimensionsTooLarge,
                "decoded byte limit should be enforced");

    const std::wstring outside = Join(root, L"outside");
    const std::wstring linkDirectory = Join(documents, L"目录联接");
    const std::wstring outsideImage = Join(outside, L"escape.png");
    Check(MakeDirectory(outside), "create controlled outside directory");
    Check(WriteBytes(outsideImage, OneByOnePng()), "create controlled outside image");
    if (CreateSymbolicLinkW(linkDirectory.c_str(), outside.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY)) {
        const SafeImageLoadResult escaped = LoadSafeMarkdownImage(document, L"目录联接\\escape.png");
        CheckStatus(escaped, SafeImageLoadStatus::UnsafePath,
                    "directory symlink escape should be rejected");
        RemoveDirectoryW(linkDirectory.c_str());
    } else {
        ++g_skipped;
        std::printf("SKIP: directory reparse escape test (CreateSymbolicLinkW error %lu)\n",
                    static_cast<unsigned long>(GetLastError()));
    }
}

} // namespace

int wmain()
{
    TemporaryDirectory temporary;
    if (!temporary.Create()) {
        std::fprintf(stderr, "FAIL: could not create controlled temporary directory\n");
        return 1;
    }

    try {
        RunTests(temporary.Path());
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: unexpected test exception: %s\n", error.what());
        ++g_failures;
    } catch (...) {
        std::fprintf(stderr, "FAIL: unexpected non-standard test exception\n");
        ++g_failures;
    }

    if (g_failures == 0) {
        std::printf("PASS: SafeImageLoader (%d assertions, %d skipped)\n", g_assertions, g_skipped);
        return 0;
    }
    std::fprintf(stderr, "FAIL: SafeImageLoader (%d assertions, %d failures, %d skipped)\n",
                 g_assertions, g_failures, g_skipped);
    return 1;
}
