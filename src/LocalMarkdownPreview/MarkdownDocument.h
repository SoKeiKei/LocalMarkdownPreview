#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace LocalMarkdownPreview {

enum class MarkdownParseStatus {
    Ok,
    InputTooLarge,
    NodeLimitExceeded,
    OutputLimitExceeded,
    TimeLimitExceeded,
    Cancelled,
    OutOfMemory,
    ParserError,
};

enum class MarkdownFormatKind {
    Heading,
    Bold,
    Italic,
    Strikethrough,
    Code,
    Link,
    Quote,
    TableHeader,
};

struct MarkdownFormatRange {
    MarkdownFormatKind kind = MarkdownFormatKind::Code;
    std::size_t start = 0;
    std::size_t length = 0;
    unsigned level = 0;
};

struct MarkdownLink {
    std::size_t start = 0;
    std::size_t length = 0;
    std::wstring target;
    std::wstring title;
};

struct MarkdownImagePlaceholder {
    std::size_t start = 0;
    std::size_t length = 0;
    std::wstring source;
    std::wstring title;
};

struct MarkdownDocumentLimits {
    // Counts callback events, including dropped HTML events.
    std::size_t max_nodes = 8192;
    // Counts UTF-16 code units in visible_text, including safe placeholders.
    std::size_t max_output_chars = 1u * 1024u * 1024u;
    // Zero disables the clock budget. The callback is checked before every
    // parser event and must not throw.
    unsigned long max_parse_milliseconds = 1000;
    bool (*should_cancel)(void* context) noexcept = nullptr;
    void* cancellation_context = nullptr;
};

struct MarkdownDocumentResult {
    MarkdownParseStatus status = MarkdownParseStatus::ParserError;
    std::size_t node_count = 0;
    std::size_t output_char_count = 0;
    std::size_t dropped_html_node_count = 0;
    std::size_t dropped_html_char_count = 0;
    std::wstring visible_text;
    std::vector<MarkdownFormatRange> formats;
    std::vector<MarkdownLink> links;
    std::vector<MarkdownImagePlaceholder> images;

    bool Succeeded() const noexcept
    {
        return status == MarkdownParseStatus::Ok;
    }
};

// Parses Windows UTF-16 Markdown directly. No file, URL, image, or HTML
// operation is performed by this function.
MarkdownDocumentResult ParseMarkdownDocument(
    std::wstring_view markdown,
    const MarkdownDocumentLimits& limits = {});

} // namespace LocalMarkdownPreview
