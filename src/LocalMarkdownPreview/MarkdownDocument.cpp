#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#ifndef MD4C_USE_UTF16
#define MD4C_USE_UTF16
#endif

#include "md4c.h"
#include "MarkdownDocument.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static_assert(sizeof(MD_CHAR) == sizeof(wchar_t),
              "MD4C_USE_UTF16 must use the Windows wchar_t representation");

namespace LocalMarkdownPreview {
namespace {

constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

std::wstring CopyMdText(const MD_CHAR* text, MD_SIZE size)
{
    if (text == nullptr || size == 0) {
        return {};
    }
    return std::wstring(reinterpret_cast<const wchar_t*>(text),
                        static_cast<std::size_t>(size));
}

std::wstring CopyAttribute(const MD_ATTRIBUTE& attribute)
{
    return CopyMdText(attribute.text, attribute.size);
}

std::wstring SanitizeAttributeForDisplay(std::wstring_view value)
{
    std::wstring safe;
    safe.reserve(value.size());
    for (const wchar_t character : value) {
        if (character == L'\r' || character == L'\n' || character == L'\t') {
            safe.push_back(L' ');
        } else if (character < 0x20 || character == 0x7f) {
            safe.push_back(L'\uFFFD');
        } else {
            safe.push_back(character);
        }
    }
    return safe;
}

bool IsFormatSpan(MD_SPANTYPE type, MarkdownFormatKind* kind)
{
    if (kind == nullptr) {
        return false;
    }

    switch (type) {
    case MD_SPAN_EM:
        *kind = MarkdownFormatKind::Italic;
        return true;
    case MD_SPAN_STRONG:
        *kind = MarkdownFormatKind::Bold;
        return true;
    case MD_SPAN_CODE:
        *kind = MarkdownFormatKind::Code;
        return true;
    case MD_SPAN_DEL:
        *kind = MarkdownFormatKind::Strikethrough;
        return true;
    default:
        return false;
    }
}

struct BlockFrame {
    MD_BLOCKTYPE type = MD_BLOCK_DOC;
    bool has_format = false;
    MarkdownFormatKind format_kind = MarkdownFormatKind::Code;
    std::size_t format_start = 0;
    unsigned format_level = 0;
};

struct SpanFrame {
    MD_SPANTYPE type = MD_SPAN_EM;
    bool has_format = false;
    MarkdownFormatKind format_kind = MarkdownFormatKind::Code;
    std::size_t format_start = 0;
    unsigned format_level = 0;
    bool is_link = false;
    bool link_suppressed = false;
    std::wstring link_target;
    std::wstring link_title;
    bool is_image = false;
};

struct ListFrame {
    bool ordered = false;
    unsigned next_item = 1;
};

class Collector {
public:
    explicit Collector(const MarkdownDocumentLimits& limits)
        : _limits(limits), _start_tick(GetTickCount64())
    {
    }

    void Fail(MarkdownParseStatus status) noexcept
    {
        _result.status = status;
    }

    MarkdownDocumentResult Finish(int parser_status)
    {
        if (_result.status == MarkdownParseStatus::ParserError) {
            _result.status = parser_status == 0
                                 ? MarkdownParseStatus::Ok
                                 : MarkdownParseStatus::ParserError;
        }
        _result.output_char_count = _result.visible_text.size();
        std::sort(_result.formats.begin(),
                  _result.formats.end(),
                  [](const MarkdownFormatRange& left,
                     const MarkdownFormatRange& right) {
                      if (left.start != right.start) {
                          return left.start < right.start;
                      }
                      if (left.length != right.length) {
                          return left.length > right.length;
                      }
                      return static_cast<int>(left.kind) <
                             static_cast<int>(right.kind);
                  });
        return std::move(_result);
    }

    bool OnEnterBlock(MD_BLOCKTYPE type, void* detail)
    {
        if (!ConsumeNode()) {
            return false;
        }

        BlockFrame frame;
        frame.type = type;

        if (type == MD_BLOCK_HTML) {
            ++_result.dropped_html_node_count;
            _blocks.push_back(std::move(frame));
            return true;
        }

        switch (type) {
        case MD_BLOCK_H:
            if (!EnsureNewlineIfNeeded() ||
                !BeginFormat(&frame, MarkdownFormatKind::Heading,
                             detail == nullptr
                                 ? 0
                                 : static_cast<MD_BLOCK_H_DETAIL*>(detail)->level)) {
                return false;
            }
            break;

        case MD_BLOCK_QUOTE:
            if (!EnsureNewlineIfNeeded() || !Append(L"> ")) {
                return false;
            }
            frame.has_format = true;
            frame.format_kind = MarkdownFormatKind::Quote;
            frame.format_start = _result.visible_text.size();
            break;

        case MD_BLOCK_UL:
            if (!EnsureNewlineIfNeeded()) {
                return false;
            }
            _lists.push_back(ListFrame{false, 1});
            break;

        case MD_BLOCK_OL: {
            if (!EnsureNewlineIfNeeded()) {
                return false;
            }
            unsigned start = 1;
            if (detail != nullptr) {
                start = static_cast<MD_BLOCK_OL_DETAIL*>(detail)->start;
            }
            _lists.push_back(ListFrame{true, start});
            break;
        }

        case MD_BLOCK_LI:
            if (!AppendListPrefix(detail == nullptr
                                      ? nullptr
                                      : static_cast<MD_BLOCK_LI_DETAIL*>(detail))) {
                return false;
            }
            break;

        case MD_BLOCK_CODE:
            if (!EnsureNewlineIfNeeded() ||
                !BeginFormat(&frame, MarkdownFormatKind::Code, 0)) {
                return false;
            }
            break;

        case MD_BLOCK_HR:
            if (!EnsureNewlineIfNeeded() || !Append(L"---")) {
                return false;
            }
            break;

        case MD_BLOCK_TABLE:
            if (!EnsureNewlineIfNeeded()) {
                return false;
            }
            break;

        case MD_BLOCK_TR:
            _table_row_cells.push_back(0);
            break;

        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            if (!_table_row_cells.empty() && _table_row_cells.back() > 0 &&
                !Append(L"\t")) {
                return false;
            }
            if (!_table_row_cells.empty()) {
                ++_table_row_cells.back();
            }
            if (type == MD_BLOCK_TH) {
                frame.has_format = true;
                frame.format_kind = MarkdownFormatKind::TableHeader;
                frame.format_start = _result.visible_text.size();
            }
            break;
        }

        default:
            break;
        }

        _blocks.push_back(std::move(frame));
        return true;
    }

    bool OnLeaveBlock(MD_BLOCKTYPE type)
    {
        if (!ConsumeNode()) {
            return false;
        }
        if (_blocks.empty() || _blocks.back().type != type) {
            Fail(MarkdownParseStatus::ParserError);
            return false;
        }

        BlockFrame frame = std::move(_blocks.back());
        _blocks.pop_back();

        if (frame.has_format) {
            CloseFormat(frame.format_kind,
                        frame.format_start,
                        frame.format_level);
        }

        switch (type) {
        case MD_BLOCK_P:
        case MD_BLOCK_H:
        case MD_BLOCK_LI:
        case MD_BLOCK_QUOTE:
        case MD_BLOCK_HR:
        case MD_BLOCK_CODE:
        case MD_BLOCK_TR:
        case MD_BLOCK_TABLE:
            if (!EnsureNewlineIfNeeded()) {
                return false;
            }
            break;
        default:
            break;
        }

        if (type == MD_BLOCK_UL || type == MD_BLOCK_OL) {
            if (_lists.empty()) {
                Fail(MarkdownParseStatus::ParserError);
                return false;
            }
            _lists.pop_back();
            if (!EnsureNewlineIfNeeded()) {
                return false;
            }
        } else if (type == MD_BLOCK_TR) {
            if (_table_row_cells.empty()) {
                Fail(MarkdownParseStatus::ParserError);
                return false;
            }
            _table_row_cells.pop_back();
        }

        return true;
    }

    bool OnEnterSpan(MD_SPANTYPE type, void* detail)
    {
        if (!ConsumeNode()) {
            return false;
        }

        SpanFrame frame;
        frame.type = type;

        if (type == MD_SPAN_IMG) {
            frame.is_image = true;
            std::wstring source;
            std::wstring title;
            if (detail != nullptr) {
                const auto* image = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
                source = CopyAttribute(image->src);
                title = CopyAttribute(image->title);
            }

            const std::size_t start = _result.visible_text.size();
            const std::wstring safe_source = SanitizeAttributeForDisplay(source);
            if (safe_source.empty()) {
                if (!Append(L"[图片]")) {
                    return false;
                }
            } else {
                if (!Append(L"[图片: ") || !Append(safe_source) ||
                    !Append(L"]")) {
                    return false;
                }
            }

            _result.images.push_back(
                MarkdownImagePlaceholder{start,
                                         _result.visible_text.size() - start,
                                         std::move(source),
                                         std::move(title)});
            ++_image_depth;
            _spans.push_back(std::move(frame));
            return true;
        }

        if (type == MD_SPAN_A) {
            frame.is_link = true;
            frame.link_suppressed = _image_depth != 0;
            if (detail != nullptr) {
                const auto* link = static_cast<MD_SPAN_A_DETAIL*>(detail);
                frame.link_target = CopyAttribute(link->href);
                frame.link_title = CopyAttribute(link->title);
            }
            if (!frame.link_suppressed) {
                frame.has_format = true;
                frame.format_kind = MarkdownFormatKind::Link;
                frame.format_start = _result.visible_text.size();
            }
            _spans.push_back(std::move(frame));
            return true;
        }

        MarkdownFormatKind format_kind = MarkdownFormatKind::Code;
        if (IsFormatSpan(type, &format_kind) && _image_depth == 0) {
            frame.has_format = true;
            frame.format_kind = format_kind;
            frame.format_start = _result.visible_text.size();
        }
        _spans.push_back(std::move(frame));
        return true;
    }

    bool OnLeaveSpan(MD_SPANTYPE type)
    {
        if (!ConsumeNode()) {
            return false;
        }
        if (_spans.empty() || _spans.back().type != type) {
            Fail(MarkdownParseStatus::ParserError);
            return false;
        }

        SpanFrame frame = std::move(_spans.back());
        _spans.pop_back();
        const std::size_t end = _result.visible_text.size();

        if (frame.has_format) {
            CloseFormat(frame.format_kind, frame.format_start, 0);
        }

        if (frame.is_link && !frame.link_suppressed) {
            _result.links.push_back(
                MarkdownLink{frame.format_start,
                             end - frame.format_start,
                             frame.link_target,
                             frame.link_title});
            if (!frame.link_target.empty()) {
                const std::wstring safe_target =
                    SanitizeAttributeForDisplay(frame.link_target);
                if (!Append(L" (") || !Append(safe_target) || !Append(L")")) {
                    return false;
                }
            }
        }

        if (frame.is_image) {
            if (_image_depth == 0) {
                Fail(MarkdownParseStatus::ParserError);
                return false;
            }
            --_image_depth;
        }
        return true;
    }

    bool OnText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size)
    {
        if (!ConsumeNode()) {
            return false;
        }

        if (type == MD_TEXT_HTML) {
            ++_result.dropped_html_node_count;
            SaturatingAdd(&_result.dropped_html_char_count,
                          static_cast<std::size_t>(size));
            return true;
        }

        // Alt text is deliberately suppressed because images are represented
        // by one auditable placeholder created in OnEnterSpan().
        if (_image_depth != 0) {
            return true;
        }

        if (type == MD_TEXT_NULLCHAR) {
            return Append(L"\uFFFD");
        }
        return Append(CopyMdText(text, size));
    }

private:
    static void SaturatingAdd(std::size_t* destination, std::size_t value) noexcept
    {
        if (destination == nullptr ||
            std::numeric_limits<std::size_t>::max() - *destination < value) {
            if (destination != nullptr) {
                *destination = std::numeric_limits<std::size_t>::max();
            }
            return;
        }
        *destination += value;
    }

    bool ConsumeNode()
    {
        if (_limits.should_cancel != nullptr &&
            _limits.should_cancel(_limits.cancellation_context)) {
            Fail(MarkdownParseStatus::Cancelled);
            return false;
        }
        if (_limits.max_parse_milliseconds != 0 &&
            GetTickCount64() - _start_tick > _limits.max_parse_milliseconds) {
            Fail(MarkdownParseStatus::TimeLimitExceeded);
            return false;
        }
        if (_result.node_count >= _limits.max_nodes) {
            Fail(MarkdownParseStatus::NodeLimitExceeded);
            return false;
        }
        ++_result.node_count;
        return true;
    }

    bool Append(std::wstring_view value)
    {
        if (value.size() > _limits.max_output_chars -
                               std::min(_limits.max_output_chars,
                                        _result.visible_text.size())) {
            Fail(MarkdownParseStatus::OutputLimitExceeded);
            return false;
        }
        if (_result.visible_text.size() > _limits.max_output_chars) {
            Fail(MarkdownParseStatus::OutputLimitExceeded);
            return false;
        }
        try {
            _result.visible_text.append(value.data(), value.size());
            return true;
        } catch (const std::bad_alloc&) {
            Fail(MarkdownParseStatus::OutOfMemory);
            return false;
        }
    }

    bool EnsureNewlineIfNeeded()
    {
        if (_result.visible_text.empty() || _result.visible_text.back() == L'\n') {
            return true;
        }
        return Append(L"\n");
    }

    bool BeginFormat(BlockFrame* frame,
                     MarkdownFormatKind kind,
                     unsigned level)
    {
        if (frame == nullptr) {
            Fail(MarkdownParseStatus::ParserError);
            return false;
        }
        frame->has_format = true;
        frame->format_kind = kind;
        frame->format_start = _result.visible_text.size();
        frame->format_level = level;
        return true;
    }

    void CloseFormat(MarkdownFormatKind kind,
                     std::size_t start,
                     unsigned level)
    {
        const std::size_t end = _result.visible_text.size();
        if (end <= start) {
            return;
        }
        _result.formats.push_back(
            MarkdownFormatRange{kind, start, end - start, level});
    }

    bool AppendListPrefix(const MD_BLOCK_LI_DETAIL* detail)
    {
        bool ordered = false;
        unsigned item = 1;
        if (!_lists.empty()) {
            ordered = _lists.back().ordered;
            item = _lists.back().next_item++;
        }

        if (ordered) {
            if (!Append(std::to_wstring(item)) || !Append(L". ")) {
                return false;
            }
        } else if (!Append(L"- ")) {
            return false;
        }

        if (detail != nullptr && detail->is_task != 0) {
            if (!Append(L"[") ||
                !Append(std::wstring(1, detail->task_mark)) ||
                !Append(L"] ")) {
                return false;
            }
        }
        return true;
    }

    MarkdownDocumentLimits _limits;
    MarkdownDocumentResult _result;
    std::vector<BlockFrame> _blocks;
    std::vector<SpanFrame> _spans;
    std::vector<ListFrame> _lists;
    std::vector<unsigned> _table_row_cells;
    std::size_t _image_depth = 0;
    ULONGLONG _start_tick = 0;
};

int EnterBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    auto* collector = static_cast<Collector*>(userdata);
    if (collector == nullptr) {
        return 1;
    }
    try {
        return collector->OnEnterBlock(type, detail) ? 0 : 1;
    } catch (const std::bad_alloc&) {
        collector->Fail(MarkdownParseStatus::OutOfMemory);
        return 1;
    } catch (...) {
        collector->Fail(MarkdownParseStatus::ParserError);
        return 1;
    }
}

int LeaveBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    (void)detail;
    auto* collector = static_cast<Collector*>(userdata);
    if (collector == nullptr) {
        return 1;
    }
    try {
        return collector->OnLeaveBlock(type) ? 0 : 1;
    } catch (const std::bad_alloc&) {
        collector->Fail(MarkdownParseStatus::OutOfMemory);
        return 1;
    } catch (...) {
        collector->Fail(MarkdownParseStatus::ParserError);
        return 1;
    }
}

int EnterSpanCallback(MD_SPANTYPE type, void* detail, void* userdata)
{
    auto* collector = static_cast<Collector*>(userdata);
    if (collector == nullptr) {
        return 1;
    }
    try {
        return collector->OnEnterSpan(type, detail) ? 0 : 1;
    } catch (const std::bad_alloc&) {
        collector->Fail(MarkdownParseStatus::OutOfMemory);
        return 1;
    } catch (...) {
        collector->Fail(MarkdownParseStatus::ParserError);
        return 1;
    }
}

int LeaveSpanCallback(MD_SPANTYPE type, void* detail, void* userdata)
{
    (void)detail;
    auto* collector = static_cast<Collector*>(userdata);
    if (collector == nullptr) {
        return 1;
    }
    try {
        return collector->OnLeaveSpan(type) ? 0 : 1;
    } catch (const std::bad_alloc&) {
        collector->Fail(MarkdownParseStatus::OutOfMemory);
        return 1;
    } catch (...) {
        collector->Fail(MarkdownParseStatus::ParserError);
        return 1;
    }
}

int TextCallback(MD_TEXTTYPE type,
                 const MD_CHAR* text,
                 MD_SIZE size,
                 void* userdata)
{
    auto* collector = static_cast<Collector*>(userdata);
    if (collector == nullptr) {
        return 1;
    }
    try {
        return collector->OnText(type, text, size) ? 0 : 1;
    } catch (const std::bad_alloc&) {
        collector->Fail(MarkdownParseStatus::OutOfMemory);
        return 1;
    } catch (...) {
        collector->Fail(MarkdownParseStatus::ParserError);
        return 1;
    }
}

} // namespace

MarkdownDocumentResult ParseMarkdownDocument(
    std::wstring_view markdown,
    const MarkdownDocumentLimits& limits)
{
    MarkdownDocumentResult too_large;
    if (markdown.size() > std::numeric_limits<MD_SIZE>::max()) {
        too_large.status = MarkdownParseStatus::InputTooLarge;
        return too_large;
    }

    try {
        Collector collector(limits);
        MD_PARSER parser{};
        parser.abi_version = 0;
        parser.flags = MD_FLAG_TABLES | MD_FLAG_TASKLISTS |
                       MD_FLAG_STRIKETHROUGH;
        parser.enter_block = &EnterBlockCallback;
        parser.leave_block = &LeaveBlockCallback;
        parser.enter_span = &EnterSpanCallback;
        parser.leave_span = &LeaveSpanCallback;
        parser.text = &TextCallback;

        const int parser_status = md_parse(
            reinterpret_cast<const MD_CHAR*>(markdown.data()),
            static_cast<MD_SIZE>(markdown.size()),
            &parser,
            &collector);
        return collector.Finish(parser_status);
    } catch (const std::bad_alloc&) {
        too_large.status = MarkdownParseStatus::OutOfMemory;
        return too_large;
    } catch (...) {
        too_large.status = MarkdownParseStatus::ParserError;
        return too_large;
    }
}

} // namespace LocalMarkdownPreview
