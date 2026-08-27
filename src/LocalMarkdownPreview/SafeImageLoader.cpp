#include "SafeImageLoader.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <initializer_list>
#include <limits>
#include <utility>

#pragma comment(lib, "windowscodecs.lib")

namespace LocalMarkdownPreview {
namespace {

using Microsoft::WRL::ComPtr;

constexpr HRESULT kNoTrustedDocumentPath = E_INVALIDARG;
constexpr HRESULT kUnsafePath = HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
constexpr HRESULT kNotFound = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
constexpr HRESULT kUnsupportedFormat = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
constexpr HRESULT kEncodedTooLarge = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
constexpr HRESULT kDimensionsTooLarge = HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

// These are the public WIC error values used when a decoder is unavailable or
// the input does not identify a known image. The SDK headers do not expose all
// of these aliases on every supported Windows SDK revision.
constexpr HRESULT kWicUnknownImageFormat = static_cast<HRESULT>(0x88982F07u);
constexpr HRESULT kWicComponentNotFound = static_cast<HRESULT>(0x88982F50u);

class UniqueHandle final {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE handle) noexcept : _handle(handle) {}
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : _handle(other.Release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    ~UniqueHandle() { Reset(); }

    HANDLE Get() const noexcept { return _handle; }
    bool IsValid() const noexcept { return _handle != nullptr && _handle != INVALID_HANDLE_VALUE; }
    HANDLE Release() noexcept
    {
        HANDLE result = _handle;
        _handle = nullptr;
        return result;
    }
    void Reset(HANDLE handle = nullptr) noexcept
    {
        if (IsValid()) {
            CloseHandle(_handle);
        }
        _handle = handle;
    }

private:
    HANDLE _handle = nullptr;
};

class ScopedComInitialization final {
public:
    ScopedComInitialization() noexcept
        : _hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)),
          _ownsInitialization(SUCCEEDED(_hr))
    {
        // An already initialized STA is still a usable COM apartment. In that
        // case CoInitializeEx reports RPC_E_CHANGED_MODE and must not be
        // balanced with CoUninitialize here.
        if (_hr == RPC_E_CHANGED_MODE) {
            _hr = S_OK;
        }
    }

    ScopedComInitialization(const ScopedComInitialization&) = delete;
    ScopedComInitialization& operator=(const ScopedComInitialization&) = delete;

    ~ScopedComInitialization()
    {
        if (_ownsInitialization) {
            CoUninitialize();
        }
    }

    HRESULT Result() const noexcept { return _hr; }

private:
    HRESULT _hr;
    bool _ownsInitialization;
};

SafeImageLoadResult MakeResult(SafeImageLoadStatus status, HRESULT hr)
{
    SafeImageLoadResult result;
    result.status = status;
    result.hresult = hr;
    return result;
}

bool IsSlash(wchar_t character) noexcept
{
    return character == L'\\' || character == L'/';
}

bool IsAsciiAlpha(wchar_t character) noexcept
{
    return (character >= L'A' && character <= L'Z') ||
           (character >= L'a' && character <= L'z');
}

wchar_t AsciiUpper(wchar_t character) noexcept
{
    if (character >= L'a' && character <= L'z') {
        return static_cast<wchar_t>(character - (L'a' - L'A'));
    }
    return character;
}

bool EqualAsciiInsensitive(std::wstring_view left, std::wstring_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (AsciiUpper(left[index]) != AsciiUpper(right[index])) {
            return false;
        }
    }
    return true;
}

bool HasPrefixInsensitive(std::wstring_view value, std::wstring_view prefix) noexcept
{
    if (value.size() < prefix.size()) {
        return false;
    }
    return EqualAsciiInsensitive(value.substr(0, prefix.size()), prefix);
}

bool IsRootedWindowsPath(std::wstring_view path) noexcept
{
    if (path.size() >= 3 && IsAsciiAlpha(path[0]) && path[1] == L':' && IsSlash(path[2])) {
        return true;
    }
    return path.size() >= 2 && IsSlash(path[0]) && IsSlash(path[1]);
}

bool IsDevicePath(std::wstring_view path) noexcept
{
    return HasPrefixInsensitive(path, L"\\\\?\\") ||
           HasPrefixInsensitive(path, L"\\\\.\\") ||
           HasPrefixInsensitive(path, L"\\??\\");
}

bool IsReservedDeviceName(std::wstring_view segment)
{
    // Windows strips trailing spaces/dots and treats the part before the first
    // dot as the DOS device name, so reject those aliases as well.
    while (!segment.empty() && (segment.back() == L' ' || segment.back() == L'.')) {
        segment.remove_suffix(1);
    }
    const std::size_t dot = segment.find(L'.');
    if (dot != std::wstring_view::npos) {
        segment = segment.substr(0, dot);
    }
    if (segment.empty()) {
        return false;
    }
    std::wstring upper;
    upper.reserve(segment.size());
    for (wchar_t character : segment) {
        upper.push_back(AsciiUpper(character));
    }
    if (upper == L"CON" || upper == L"PRN" || upper == L"AUX" || upper == L"NUL") {
        return true;
    }
    if (upper.size() == 4 && (upper.starts_with(L"COM") || upper.starts_with(L"LPT")) &&
        upper[3] >= L'1' && upper[3] <= L'9') {
        return true;
    }
    return false;
}

bool IsSafeRelativeImageSource(std::wstring_view source)
{
    if (source.empty() || source.find(L'\0') != std::wstring_view::npos ||
        IsSlash(source.front()) || IsDevicePath(source) || source.find(L':') != std::wstring_view::npos) {
        return false;
    }

    std::size_t segmentStart = 0;
    while (segmentStart < source.size()) {
        const std::size_t separator = source.find_first_of(L"\\/", segmentStart);
        const std::size_t segmentEnd = separator == std::wstring_view::npos ? source.size() : separator;
        const std::wstring_view segment = source.substr(segmentStart, segmentEnd - segmentStart);
        if (segment == L"." || segment == L".." || IsReservedDeviceName(segment)) {
            return false;
        }
        if (separator == std::wstring_view::npos) {
            break;
        }
        segmentStart = separator + 1;
        if (segmentStart == source.size()) {
            return false;
        }
        while (segmentStart < source.size() && IsSlash(source[segmentStart])) {
            ++segmentStart;
        }
        if (segmentStart == source.size()) {
            return false;
        }
    }
    return true;
}

bool IsSupportedExtension(std::wstring_view source) noexcept
{
    const std::size_t slash = source.find_last_of(L"\\/");
    const std::size_t dot = source.find_last_of(L'.');
    if (dot == std::wstring_view::npos ||
        (slash != std::wstring_view::npos && dot <= slash) ||
        dot + 1 >= source.size()) {
        return false;
    }
    const std::wstring_view extension = source.substr(dot);
    return EqualAsciiInsensitive(extension, L".png") ||
           EqualAsciiInsensitive(extension, L".jpg") ||
           EqualAsciiInsensitive(extension, L".jpeg") ||
           EqualAsciiInsensitive(extension, L".gif") ||
           EqualAsciiInsensitive(extension, L".bmp") ||
           EqualAsciiInsensitive(extension, L".webp");
}

bool IsWebpExtension(std::wstring_view source) noexcept
{
    const std::size_t dot = source.find_last_of(L'.');
    return dot != std::wstring_view::npos &&
           EqualAsciiInsensitive(source.substr(dot), L".webp");
}

bool GetFinalPath(HANDLE handle, std::wstring& path)
{
    path.clear();
    std::vector<wchar_t> buffer(512);
    for (unsigned attempt = 0; attempt != 8; ++attempt) {
        const DWORD length = GetFinalPathNameByHandleW(
            handle,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (length == 0) {
            return false;
        }
        if (length + 1 < buffer.size()) {
            path.assign(buffer.data(), length);
            return true;
        }
        if (length >= 32768) {
            return false;
        }
        buffer.resize(static_cast<std::size_t>(length) + 1);
    }
    return false;
}

std::wstring RemoveExtendedPrefix(std::wstring path)
{
    if (HasPrefixInsensitive(path, L"\\\\?\\UNC\\")) {
        std::wstring result = L"\\\\";
        result.append(path.substr(8));
        return result;
    }
    if (HasPrefixInsensitive(path, L"\\\\?\\")) {
        path.erase(0, 4);
    }
    return path;
}

std::wstring DirectoryName(std::wstring path)
{
    path = RemoveExtendedPrefix(std::move(path));
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    if (slash == 2 && path.size() >= 3 && path[1] == L':') {
        return path.substr(0, 3);
    }
    if (slash == 0) {
        return path.substr(0, 1);
    }
    path.resize(slash);
    while (path.size() > 3 && !path.empty() && IsSlash(path.back())) {
        path.pop_back();
    }
    return path;
}

std::wstring JoinDirectoryAndSource(const std::wstring& directory, std::wstring_view source)
{
    std::wstring result = directory;
    if (result.empty() || !IsSlash(result.back())) {
        result.push_back(L'\\');
    }
    result.reserve(result.size() + source.size());
    for (wchar_t character : source) {
        result.push_back(character == L'/' ? L'\\' : character);
    }
    return result;
}

bool IsWithinDirectory(std::wstring_view directory, std::wstring_view path) noexcept
{
    if (directory.empty() || !HasPrefixInsensitive(path, directory)) {
        return false;
    }
    if (IsSlash(directory.back())) {
        return path.size() > directory.size();
    }
    if (path.size() <= directory.size()) {
        return false;
    }
    return IsSlash(path[directory.size()]);
}

SafeImageLoadStatus StatusForOpenError(DWORD error) noexcept
{
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
        error == ERROR_INVALID_DRIVE) {
        return SafeImageLoadStatus::NotFound;
    }
    return SafeImageLoadStatus::DecodeFailed;
}

bool ReadPrefix(HANDLE handle, std::array<BYTE, 16>& prefix, DWORD& bytesRead) noexcept
{
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN)) {
        return false;
    }
    bytesRead = 0;
    return ReadFile(handle, prefix.data(), static_cast<DWORD>(prefix.size()), &bytesRead, nullptr) != FALSE;
}

bool HasExpectedSignature(std::wstring_view source, const std::array<BYTE, 16>& prefix, DWORD bytesRead) noexcept
{
    const std::size_t dot = source.find_last_of(L'.');
    const std::wstring_view extension = dot == std::wstring_view::npos ? std::wstring_view{} : source.substr(dot);
    auto StartsWithBytes = [&](std::initializer_list<BYTE> expected) noexcept {
        if (bytesRead < expected.size()) {
            return false;
        }
        std::size_t index = 0;
        for (BYTE byte : expected) {
            if (prefix[index++] != byte) {
                return false;
            }
        }
        return true;
    };

    if (EqualAsciiInsensitive(extension, L".png")) {
        return StartsWithBytes({0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A});
    }
    if (EqualAsciiInsensitive(extension, L".jpg") || EqualAsciiInsensitive(extension, L".jpeg")) {
        return StartsWithBytes({0xFF, 0xD8, 0xFF});
    }
    if (EqualAsciiInsensitive(extension, L".gif")) {
        return bytesRead >= 6 &&
               ((std::equal(prefix.begin(), prefix.begin() + 6, std::array<BYTE, 6>{'G', 'I', 'F', '8', '7', 'a'}.begin())) ||
                (std::equal(prefix.begin(), prefix.begin() + 6, std::array<BYTE, 6>{'G', 'I', 'F', '8', '9', 'a'}.begin())));
    }
    if (EqualAsciiInsensitive(extension, L".bmp")) {
        return StartsWithBytes({'B', 'M'});
    }
    if (EqualAsciiInsensitive(extension, L".webp")) {
        return bytesRead >= 12 && StartsWithBytes({'R', 'I', 'F', 'F'}) &&
               prefix[8] == 'W' && prefix[9] == 'E' && prefix[10] == 'B' && prefix[11] == 'P';
    }
    return false;
}

bool IsAllowedContainer(const GUID& container) noexcept
{
    return IsEqualGUID(container, GUID_ContainerFormatPng) ||
           IsEqualGUID(container, GUID_ContainerFormatJpeg) ||
           IsEqualGUID(container, GUID_ContainerFormatGif) ||
           IsEqualGUID(container, GUID_ContainerFormatBmp) ||
           IsEqualGUID(container, GUID_ContainerFormatWebp);
}

bool IsOutOfMemory(HRESULT hr) noexcept
{
    return hr == E_OUTOFMEMORY || hr == WINCODEC_ERR_OUTOFMEMORY;
}

SafeImageLoadResult LoadSafeMarkdownImageImpl(
    std::wstring_view trustedDocumentPath,
    std::wstring_view imageSource,
    const SafeImageLoadLimits& limits)
{
    if (trustedDocumentPath.empty() || !IsRootedWindowsPath(trustedDocumentPath) ||
        IsDevicePath(trustedDocumentPath)) {
        return MakeResult(SafeImageLoadStatus::NoTrustedDocumentPath, kNoTrustedDocumentPath);
    }
    if (!IsSafeRelativeImageSource(imageSource)) {
        return MakeResult(SafeImageLoadStatus::UnsafePath, kUnsafePath);
    }
    if (!IsSupportedExtension(imageSource)) {
        return MakeResult(SafeImageLoadStatus::UnsupportedFormat, kUnsupportedFormat);
    }

    const std::wstring documentPath(trustedDocumentPath);
    UniqueHandle document(CreateFileW(
        documentPath.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS,
        nullptr));
    if (!document.IsValid()) {
        return MakeResult(SafeImageLoadStatus::NoTrustedDocumentPath, kNoTrustedDocumentPath);
    }

    BY_HANDLE_FILE_INFORMATION documentInfo{};
    if (!GetFileInformationByHandle(document.Get(), &documentInfo) ||
        (documentInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return MakeResult(SafeImageLoadStatus::NoTrustedDocumentPath, kNoTrustedDocumentPath);
    }

    std::wstring finalDocumentPath;
    if (!GetFinalPath(document.Get(), finalDocumentPath)) {
        return MakeResult(SafeImageLoadStatus::NoTrustedDocumentPath, HRESULT_FROM_WIN32(GetLastError()));
    }
    const std::wstring documentDirectory = DirectoryName(finalDocumentPath);
    if (documentDirectory.empty()) {
        return MakeResult(SafeImageLoadStatus::NoTrustedDocumentPath, kNoTrustedDocumentPath);
    }

    const std::wstring candidatePath = JoinDirectoryAndSource(documentDirectory, imageSource);
    UniqueHandle image(CreateFileW(
        candidatePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr));
    if (!image.IsValid()) {
        const DWORD error = GetLastError();
        return MakeResult(StatusForOpenError(error), HRESULT_FROM_WIN32(error));
    }

    BY_HANDLE_FILE_INFORMATION imageInfo{};
    if (!GetFileInformationByHandle(image.Get(), &imageInfo) ||
        (imageInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return MakeResult(SafeImageLoadStatus::DecodeFailed, HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    }

    LARGE_INTEGER encodedSize{};
    if (!GetFileSizeEx(image.Get(), &encodedSize) || encodedSize.QuadPart < 0) {
        return MakeResult(SafeImageLoadStatus::DecodeFailed, HRESULT_FROM_WIN32(GetLastError()));
    }
    const std::uint64_t encodedBytes = static_cast<std::uint64_t>(encodedSize.QuadPart);
    if (encodedBytes > limits.max_encoded_bytes) {
        SafeImageLoadResult result = MakeResult(SafeImageLoadStatus::EncodedTooLarge, kEncodedTooLarge);
        result.encoded_bytes = encodedBytes;
        return result;
    }
    if (encodedBytes > std::numeric_limits<DWORD>::max()) {
        SafeImageLoadResult result = MakeResult(SafeImageLoadStatus::EncodedTooLarge, kEncodedTooLarge);
        result.encoded_bytes = encodedBytes;
        return result;
    }

    std::wstring finalImagePath;
    if (!GetFinalPath(image.Get(), finalImagePath)) {
        return MakeResult(SafeImageLoadStatus::DecodeFailed, HRESULT_FROM_WIN32(GetLastError()));
    }
    finalImagePath = RemoveExtendedPrefix(std::move(finalImagePath));
    if (!IsWithinDirectory(documentDirectory, finalImagePath)) {
        SafeImageLoadResult result = MakeResult(SafeImageLoadStatus::UnsafePath, kUnsafePath);
        result.normalized_path = finalImagePath;
        result.encoded_bytes = encodedBytes;
        return result;
    }

    std::array<BYTE, 16> prefix{};
    DWORD prefixBytes = 0;
    if (!ReadPrefix(image.Get(), prefix, prefixBytes) ||
        !HasExpectedSignature(imageSource, prefix, prefixBytes)) {
        SafeImageLoadResult result = MakeResult(SafeImageLoadStatus::DecodeFailed, HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        result.normalized_path = finalImagePath;
        result.encoded_bytes = encodedBytes;
        return result;
    }

    std::vector<BYTE> encodedData;
    try {
        encodedData.resize(static_cast<std::size_t>(encodedBytes));
    } catch (...) {
        SafeImageLoadResult result = MakeResult(SafeImageLoadStatus::OutOfMemory, E_OUTOFMEMORY);
        result.normalized_path = finalImagePath;
        result.encoded_bytes = encodedBytes;
        return result;
    }
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(image.Get(), zero, nullptr, FILE_BEGIN)) {
        SafeImageLoadResult result = MakeResult(SafeImageLoadStatus::DecodeFailed, HRESULT_FROM_WIN32(GetLastError()));
        result.normalized_path = finalImagePath;
        result.encoded_bytes = encodedBytes;
        return result;
    }
    std::size_t readOffset = 0;
    while (readOffset < encodedData.size()) {
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            encodedData.size() - readOffset,
            1u << 20));
        DWORD bytesRead = 0;
        if (!ReadFile(image.Get(), encodedData.data() + readOffset, requested, &bytesRead, nullptr) ||
            bytesRead == 0) {
            const DWORD error = GetLastError() == ERROR_SUCCESS ? ERROR_HANDLE_EOF : GetLastError();
            SafeImageLoadResult result = MakeResult(SafeImageLoadStatus::DecodeFailed, HRESULT_FROM_WIN32(error));
            result.normalized_path = finalImagePath;
            result.encoded_bytes = encodedBytes;
            return result;
        }
        readOffset += bytesRead;
    }

    ScopedComInitialization com;
    if (FAILED(com.Result())) {
        SafeImageLoadResult result = MakeResult(SafeImageLoadStatus::DecodeFailed, com.Result());
        result.normalized_path = finalImagePath;
        result.encoded_bytes = encodedBytes;
        return result;
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        SafeImageLoadResult result = MakeResult(IsOutOfMemory(hr) ? SafeImageLoadStatus::OutOfMemory : SafeImageLoadStatus::DecodeFailed, hr);
        result.normalized_path = finalImagePath;
        result.encoded_bytes = encodedBytes;
        return result;
    }

    ComPtr<IWICStream> encodedStream;
    hr = factory->CreateStream(&encodedStream);
    if (SUCCEEDED(hr)) {
        hr = encodedStream->InitializeFromMemory(
            encodedData.data(),
            static_cast<DWORD>(encodedData.size()));
    }
    ComPtr<IWICBitmapDecoder> decoder;
    if (SUCCEEDED(hr)) {
        hr = factory->CreateDecoderFromStream(
        encodedStream.Get(),
        nullptr,
        WICDecodeMetadataCacheOnLoad,
        &decoder);
    }
    if (FAILED(hr)) {
        SafeImageLoadStatus status = SafeImageLoadStatus::DecodeFailed;
        if (IsWebpExtension(imageSource) &&
            (hr == kWicComponentNotFound || hr == kWicUnknownImageFormat)) {
            status = SafeImageLoadStatus::UnsupportedFormat;
        }
        if (IsOutOfMemory(hr)) {
            status = SafeImageLoadStatus::OutOfMemory;
        }
        SafeImageLoadResult result = MakeResult(status, hr);
        result.normalized_path = finalImagePath;
        result.encoded_bytes = encodedBytes;
        return result;
    }

    GUID container{};
    hr = decoder->GetContainerFormat(&container);
    if (FAILED(hr)) {
        SafeImageLoadResult result = MakeResult(IsOutOfMemory(hr) ? SafeImageLoadStatus::OutOfMemory : SafeImageLoadStatus::DecodeFailed, hr);
        result.normalized_path = finalImagePath;
        result.encoded_bytes = encodedBytes;
        return result;
    }
    if (!IsAllowedContainer(container)) {
        SafeImageLoadResult result = MakeResult(SafeImageLoadStatus::UnsupportedFormat, kUnsupportedFormat);
        result.normalized_path = finalImagePath;
        result.encoded_bytes = encodedBytes;
        return result;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        SafeImageLoadResult result = MakeResult(IsOutOfMemory(hr) ? SafeImageLoadStatus::OutOfMemory : SafeImageLoadStatus::DecodeFailed, hr);
        result.normalized_path = finalImagePath;
        result.encoded_bytes = encodedBytes;
        return result;
    }

    UINT width = 0;
    UINT height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr)) {
        SafeImageLoadResult result = MakeResult(IsOutOfMemory(hr) ? SafeImageLoadStatus::OutOfMemory : SafeImageLoadStatus::DecodeFailed, hr);
        result.normalized_path = finalImagePath;
        result.encoded_bytes = encodedBytes;
        return result;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (width == 0 || height == 0 || width > limits.max_dimension || height > limits.max_dimension ||
        pixels > limits.max_pixels) {
        SafeImageLoadResult result = MakeResult(SafeImageLoadStatus::DimensionsTooLarge, kDimensionsTooLarge);
        result.normalized_path = finalImagePath;
        result.encoded_bytes = encodedBytes;
        result.width = width;
        result.height = height;
        return result;
    }

    const std::uint64_t stride = static_cast<std::uint64_t>(width) * 4u;
    const std::uint64_t decodedBytes = stride * static_cast<std::uint64_t>(height);
    if (stride > std::numeric_limits<std::uint32_t>::max() || decodedBytes > limits.max_decoded_bytes ||
        decodedBytes > std::vector<std::uint8_t>().max_size()) {
        SafeImageLoadResult result = MakeResult(SafeImageLoadStatus::DimensionsTooLarge, kDimensionsTooLarge);
        result.normalized_path = finalImagePath;
        result.encoded_bytes = encodedBytes;
        result.width = width;
        result.height = height;
        return result;
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        SafeImageLoadResult result = MakeResult(IsOutOfMemory(hr) ? SafeImageLoadStatus::OutOfMemory : SafeImageLoadStatus::DecodeFailed, hr);
        result.normalized_path = finalImagePath;
        result.encoded_bytes = encodedBytes;
        result.width = width;
        result.height = height;
        return result;
    }
    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        SafeImageLoadResult result = MakeResult(IsOutOfMemory(hr) ? SafeImageLoadStatus::OutOfMemory : SafeImageLoadStatus::DecodeFailed, hr);
        result.normalized_path = finalImagePath;
        result.encoded_bytes = encodedBytes;
        result.width = width;
        result.height = height;
        return result;
    }

    SafeImageLoadResult result = MakeResult(SafeImageLoadStatus::Loaded, S_OK);
    result.normalized_path = finalImagePath;
    result.encoded_bytes = encodedBytes;
    result.width = width;
    result.height = height;
    result.stride = static_cast<std::uint32_t>(stride);
    try {
        result.pixels.resize(static_cast<std::size_t>(decodedBytes));
    } catch (...) {
        result.status = SafeImageLoadStatus::OutOfMemory;
        result.hresult = E_OUTOFMEMORY;
        result.pixels.clear();
        return result;
    }

    hr = converter->CopyPixels(
        nullptr,
        result.stride,
        static_cast<UINT>(result.pixels.size()),
        result.pixels.data());
    if (FAILED(hr)) {
        result.status = IsOutOfMemory(hr) ? SafeImageLoadStatus::OutOfMemory : SafeImageLoadStatus::DecodeFailed;
        result.hresult = hr;
        result.pixels.clear();
    } else {
        result.encoded_data = std::move(encodedData);
    }
    return result;
}

} // namespace

SafeImageLoadResult LoadSafeMarkdownImage(
    std::wstring_view trustedDocumentPath,
    std::wstring_view imageSource,
    const SafeImageLoadLimits& limits)
{
    try {
        return LoadSafeMarkdownImageImpl(trustedDocumentPath, imageSource, limits);
    } catch (...) {
        return MakeResult(SafeImageLoadStatus::OutOfMemory, E_OUTOFMEMORY);
    }
}

} // namespace LocalMarkdownPreview
