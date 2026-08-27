#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace LocalMarkdownPreview {

enum class SafeImageLoadStatus {
    Loaded,
    NoTrustedDocumentPath,
    UnsafePath,
    NotFound,
    UnsupportedFormat,
    EncodedTooLarge,
    DimensionsTooLarge,
    DecodeFailed,
    OutOfMemory,
};

struct SafeImageLoadLimits {
    std::uint64_t max_encoded_bytes = 10u * 1024u * 1024u;
    std::uint32_t max_dimension = 4096;
    std::uint64_t max_pixels = 16u * 1024u * 1024u;
    // This budget is reported as DimensionsTooLarge because it is a decoded
    // geometry/buffer limit, not a separate format or I/O failure.
    std::uint64_t max_decoded_bytes = 64u * 1024u * 1024u;
};

struct SafeImageLoadResult {
    SafeImageLoadStatus status = SafeImageLoadStatus::DecodeFailed;
    HRESULT hresult = E_FAIL;
    std::wstring normalized_path;
    std::uint64_t encoded_bytes = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    // The validated bytes from the already-opened file handle. Consumers may
    // pass this to a decoder or RichEdit without reopening a raceable path.
    std::vector<std::uint8_t> encoded_data;
    std::vector<std::uint8_t> pixels;

    bool Succeeded() const noexcept
    {
        return status == SafeImageLoadStatus::Loaded;
    }
};

// Resolves a Markdown image source below the trusted document's directory and
// decodes it with Windows WIC into 32bpp premultiplied BGRA pixels. The
// document path is deliberately required to be an existing, rooted file path;
// this prevents an image source from establishing its own trust root.
SafeImageLoadResult LoadSafeMarkdownImage(
    std::wstring_view trusted_document_path,
    std::wstring_view image_source,
    const SafeImageLoadLimits& limits = {});

} // namespace LocalMarkdownPreview
