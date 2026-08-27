#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "md4c.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static_assert(sizeof(MD_CHAR) == sizeof(wchar_t),
              "MD4C_USE_UTF16 must use the Windows wchar_t representation");

namespace {

constexpr unsigned kParserFlags =
    MD_FLAG_TABLES | MD_FLAG_TASKLISTS | MD_FLAG_STRIKETHROUGH;

enum class EventKind {
    EnterBlock,
    LeaveBlock,
    EnterSpan,
    LeaveSpan,
    Text,
    DroppedHtml,
};

struct Event {
    EventKind kind;
    int type = 0;
    std::wstring value;
    std::wstring auxiliary;
    std::uint64_t number = 0;
    bool flag = false;
};

struct ParseResult {
    int status = -1;
    bool callback_failed = false;
    std::vector<Event> events;
    std::wstring visible_text;
    std::uint64_t dropped_html_units = 0;
    unsigned html_block_count = 0;
    unsigned html_text_count = 0;
};

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

const char* BlockName(MD_BLOCKTYPE type)
{
    switch (type) {
    case MD_BLOCK_DOC: return "doc";
    case MD_BLOCK_QUOTE: return "quote";
    case MD_BLOCK_UL: return "unordered-list";
    case MD_BLOCK_OL: return "ordered-list";
    case MD_BLOCK_LI: return "list-item";
    case MD_BLOCK_HR: return "horizontal-rule";
    case MD_BLOCK_H: return "heading";
    case MD_BLOCK_CODE: return "code-block";
    case MD_BLOCK_HTML: return "raw-html";
    case MD_BLOCK_P: return "paragraph";
    case MD_BLOCK_TABLE: return "table";
    case MD_BLOCK_THEAD: return "table-head";
    case MD_BLOCK_TBODY: return "table-body";
    case MD_BLOCK_TR: return "table-row";
    case MD_BLOCK_TH: return "table-header-cell";
    case MD_BLOCK_TD: return "table-cell";
    default: return "unknown-block";
    }
}

const char* SpanName(MD_SPANTYPE type)
{
    switch (type) {
    case MD_SPAN_EM: return "emphasis";
    case MD_SPAN_STRONG: return "strong";
    case MD_SPAN_A: return "link";
    case MD_SPAN_IMG: return "image";
    case MD_SPAN_CODE: return "code";
    case MD_SPAN_DEL: return "strikethrough";
    case MD_SPAN_LATEXMATH: return "latex-math";
    case MD_SPAN_LATEXMATH_DISPLAY: return "latex-math-display";
    case MD_SPAN_WIKILINK: return "wiki-link";
    case MD_SPAN_U: return "underline";
    default: return "unknown-span";
    }
}

const char* TextName(MD_TEXTTYPE type)
{
    switch (type) {
    case MD_TEXT_NORMAL: return "normal";
    case MD_TEXT_NULLCHAR: return "null-character";
    case MD_TEXT_BR: return "hard-break";
    case MD_TEXT_SOFTBR: return "soft-break";
    case MD_TEXT_ENTITY: return "entity";
    case MD_TEXT_CODE: return "code";
    case MD_TEXT_HTML: return "raw-html";
    case MD_TEXT_LATEXMATH: return "latex-math";
    default: return "unknown-text";
    }
}

class Collector {
public:
    ParseResult result;

    void EnterBlock(MD_BLOCKTYPE type, void* detail)
    {
        if (type == MD_BLOCK_HTML) {
            ++result.html_block_count;
            result.events.push_back(
                Event{EventKind::DroppedHtml, static_cast<int>(type), {}, L"block"});
            return;
        }

        Event event{EventKind::EnterBlock, static_cast<int>(type)};
        switch (type) {
        case MD_BLOCK_H:
            if (detail != nullptr) {
                event.number = static_cast<MD_BLOCK_H_DETAIL*>(detail)->level;
            }
            break;
        case MD_BLOCK_LI:
            if (detail != nullptr) {
                const auto* list_item = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
                event.flag = list_item->is_task != 0;
                if (event.flag) {
                    event.number = static_cast<unsigned char>(list_item->task_mark);
                }
            }
            break;
        case MD_BLOCK_CODE:
            if (detail != nullptr) {
                event.value = CopyAttribute(
                    static_cast<MD_BLOCK_CODE_DETAIL*>(detail)->lang);
            }
            break;
        case MD_BLOCK_TABLE:
            if (detail != nullptr) {
                const auto* table = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
                event.number = table->col_count;
                event.auxiliary = std::to_wstring(table->body_row_count);
            }
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            if (detail != nullptr) {
                event.number = static_cast<unsigned>(
                    static_cast<MD_BLOCK_TD_DETAIL*>(detail)->align);
            }
            break;
        default:
            break;
        }

        result.events.push_back(std::move(event));
    }

    void LeaveBlock(MD_BLOCKTYPE type)
    {
        if (type == MD_BLOCK_HTML) {
            return;
        }
        result.events.push_back(
            Event{EventKind::LeaveBlock, static_cast<int>(type)});
    }

    void EnterSpan(MD_SPANTYPE type, void* detail)
    {
        Event event{EventKind::EnterSpan, static_cast<int>(type)};
        if (detail != nullptr && type == MD_SPAN_A) {
            const auto* link = static_cast<MD_SPAN_A_DETAIL*>(detail);
            event.value = CopyAttribute(link->href);
            event.auxiliary = CopyAttribute(link->title);
            event.flag = link->is_autolink != 0;
        } else if (detail != nullptr && type == MD_SPAN_IMG) {
            const auto* image = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
            event.value = CopyAttribute(image->src);
            event.auxiliary = CopyAttribute(image->title);
        }
        result.events.push_back(std::move(event));
    }

    void LeaveSpan(MD_SPANTYPE type)
    {
        result.events.push_back(Event{EventKind::LeaveSpan, static_cast<int>(type)});
    }

    void Text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size)
    {
        if (type == MD_TEXT_HTML) {
            ++result.html_text_count;
            result.dropped_html_units += size;
            result.events.push_back(Event{EventKind::DroppedHtml,
                                          static_cast<int>(type),
                                          {},
                                          L"text",
                                          size});
            return;
        }

        std::wstring value;
        if (type == MD_TEXT_NULLCHAR) {
            value.assign(1, L'\uFFFD');
        } else {
            value = CopyMdText(text, size);
        }

        result.visible_text.append(value);
        result.events.push_back(Event{EventKind::Text,
                                      static_cast<int>(type),
                                      std::move(value)});
    }
};

int EnterBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    auto* collector = static_cast<Collector*>(userdata);
    try {
        collector->EnterBlock(type, detail);
        return 0;
    } catch (...) {
        collector->result.callback_failed = true;
        return 1;
    }
}

int LeaveBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    (void)detail;
    auto* collector = static_cast<Collector*>(userdata);
    try {
        collector->LeaveBlock(type);
        return 0;
    } catch (...) {
        collector->result.callback_failed = true;
        return 1;
    }
}

int EnterSpanCallback(MD_SPANTYPE type, void* detail, void* userdata)
{
    auto* collector = static_cast<Collector*>(userdata);
    try {
        collector->EnterSpan(type, detail);
        return 0;
    } catch (...) {
        collector->result.callback_failed = true;
        return 1;
    }
}

int LeaveSpanCallback(MD_SPANTYPE type, void* detail, void* userdata)
{
    (void)detail;
    auto* collector = static_cast<Collector*>(userdata);
    try {
        collector->LeaveSpan(type);
        return 0;
    } catch (...) {
        collector->result.callback_failed = true;
        return 1;
    }
}

int TextCallback(MD_TEXTTYPE type,
                 const MD_CHAR* text,
                 MD_SIZE size,
                 void* userdata)
{
    auto* collector = static_cast<Collector*>(userdata);
    try {
        collector->Text(type, text, size);
        return 0;
    } catch (...) {
        collector->result.callback_failed = true;
        return 1;
    }
}

ParseResult ParseMarkdown(const std::wstring& markdown)
{
    Collector collector;
    if (markdown.size() > std::numeric_limits<MD_SIZE>::max()) {
        collector.result.status = -2;
        return std::move(collector.result);
    }

    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags = kParserFlags;
    parser.enter_block = &EnterBlockCallback;
    parser.leave_block = &LeaveBlockCallback;
    parser.enter_span = &EnterSpanCallback;
    parser.leave_span = &LeaveSpanCallback;
    parser.text = &TextCallback;

    collector.result.status = md_parse(
        reinterpret_cast<const MD_CHAR*>(markdown.data()),
        static_cast<MD_SIZE>(markdown.size()),
        &parser,
        &collector);
    if (collector.result.callback_failed && collector.result.status == 0) {
        collector.result.status = -3;
    }
    return std::move(collector.result);
}

std::string ToUtf8(std::wstring_view value)
{
    if (value.empty()) {
        return {};
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return "<text-too-long>";
    }

    const int source_size = static_cast<int>(value.size());
    const int output_size = WideCharToMultiByte(CP_UTF8,
                                                WC_ERR_INVALID_CHARS,
                                                value.data(),
                                                source_size,
                                                nullptr,
                                                0,
                                                nullptr,
                                                nullptr);
    if (output_size <= 0) {
        return "<invalid-utf16>";
    }

    std::string output(static_cast<std::size_t>(output_size), '\0');
    const int converted = WideCharToMultiByte(CP_UTF8,
                                              WC_ERR_INVALID_CHARS,
                                              value.data(),
                                              source_size,
                                              output.data(),
                                              output_size,
                                              nullptr,
                                              nullptr);
    if (converted != output_size) {
        return "<invalid-utf16>";
    }
    return output;
}

std::string EscapeForConsole(std::wstring_view value)
{
    const std::string utf8 = ToUtf8(value);
    std::string escaped;
    escaped.reserve(utf8.size());
    for (const char character : utf8) {
        switch (character) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\r': escaped += "\\r"; break;
        case '\n': escaped += "\\n"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += character; break;
        }
    }
    return escaped;
}

std::size_t CountBlock(const ParseResult& result, MD_BLOCKTYPE type)
{
    std::size_t count = 0;
    for (const Event& event : result.events) {
        if (event.kind == EventKind::EnterBlock && event.type == static_cast<int>(type)) {
            ++count;
        }
    }
    return count;
}

std::size_t CountSpan(const ParseResult& result, MD_SPANTYPE type)
{
    std::size_t count = 0;
    for (const Event& event : result.events) {
        if (event.kind == EventKind::EnterSpan && event.type == static_cast<int>(type)) {
            ++count;
        }
    }
    return count;
}

std::size_t CountText(const ParseResult& result, MD_TEXTTYPE type)
{
    std::size_t count = 0;
    for (const Event& event : result.events) {
        if (event.kind == EventKind::Text && event.type == static_cast<int>(type)) {
            ++count;
        }
    }
    return count;
}

bool HasBlockLevel(const ParseResult& result, unsigned level)
{
    for (const Event& event : result.events) {
        if (event.kind == EventKind::EnterBlock &&
            event.type == static_cast<int>(MD_BLOCK_H) &&
            event.number == level) {
            return true;
        }
    }
    return false;
}

bool HasTask(const ParseResult& result, wchar_t mark)
{
    for (const Event& event : result.events) {
        if (event.kind == EventKind::EnterBlock &&
            event.type == static_cast<int>(MD_BLOCK_LI) &&
            event.flag &&
            event.number == static_cast<unsigned>(mark)) {
            return true;
        }
    }
    return false;
}

bool HasSpanAttribute(const ParseResult& result,
                      MD_SPANTYPE type,
                      std::wstring_view value)
{
    for (const Event& event : result.events) {
        if (event.kind == EventKind::EnterSpan &&
            event.type == static_cast<int>(type) &&
            event.value == value) {
            return true;
        }
    }
    return false;
}

const std::wstring kFixture = LR"MD4C(# 中文标题

这是 **粗体**、*斜体* 和 ~~删除线~~。

- 普通列表
- [x] 已完成任务
- [ ] 待办任务

> 引用内容

行内 `代码`，以及下面的代码块：

```cpp
const int answer = 42;
```

| 名称 | 值 |
| :--- | ---: |
| 中文 | 42 |

[链接](https://example.invalid/docs "链接标题") 和 ![图片](images/pic.png "图片标题")

<div>
这段 HTML 必须丢弃
</div>

这一段含有行内 <span>HTML</span>，标签不作为可见节点。
)MD4C";

int RunTests()
{
    const ParseResult result = ParseMarkdown(kFixture);
    std::vector<std::string> failures;
    const auto expect = [&failures](bool condition, const char* description) {
        if (!condition) {
            failures.emplace_back(description);
        }
    };

    expect(result.status == 0, "md_parse returned success");
    expect(!result.callback_failed, "callbacks did not throw");
    expect(HasBlockLevel(result, 1), "heading level 1 was collected");
    expect(CountSpan(result, MD_SPAN_STRONG) > 0, "strong span was collected");
    expect(CountSpan(result, MD_SPAN_EM) > 0, "emphasis span was collected");
    expect(CountSpan(result, MD_SPAN_DEL) > 0, "strikethrough span was collected");
    expect(CountBlock(result, MD_BLOCK_UL) > 0, "unordered list was collected");
    expect(CountBlock(result, MD_BLOCK_LI) >= 3, "list items were collected");
    expect(HasTask(result, L'x'), "completed task item was collected");
    expect(HasTask(result, L' '), "pending task item was collected");
    expect(CountBlock(result, MD_BLOCK_QUOTE) > 0, "blockquote was collected");
    expect(CountBlock(result, MD_BLOCK_CODE) > 0, "fenced code block was collected");
    expect(CountText(result, MD_TEXT_CODE) > 0, "code text was collected");
    expect(CountBlock(result, MD_BLOCK_TABLE) > 0, "table was collected");
    expect(CountBlock(result, MD_BLOCK_TH) > 0, "table header cell was collected");
    expect(CountBlock(result, MD_BLOCK_TD) > 0, "table body cell was collected");
    expect(HasSpanAttribute(result,
                            MD_SPAN_A,
                            L"https://example.invalid/docs"),
           "link destination was collected without navigation");
    expect(HasSpanAttribute(result, MD_SPAN_IMG, L"images/pic.png"),
           "image source was collected without loading");
    expect(result.visible_text.find(L"中文") != std::wstring::npos,
           "Chinese text survived UTF-16 parsing");
    expect(result.html_block_count > 0, "raw HTML block was identified");
    expect(result.html_text_count > 0, "raw HTML text was identified");
    expect(result.dropped_html_units > 0, "raw HTML was marked droppable");
    expect(result.visible_text.find(L"<div>") == std::wstring::npos,
           "raw HTML block was not exposed as visible text");
    expect(result.visible_text.find(L"<span>") == std::wstring::npos,
           "inline raw HTML was not exposed as visible text");
    for (const Event& event : result.events) {
        if (event.kind == EventKind::DroppedHtml) {
            expect(event.value.empty(), "dropped HTML event does not retain source");
        }
    }

    if (failures.empty()) {
        std::cout << "PASS: MD4C UTF-16 parser tests ("
                  << result.events.size() << " events, "
                  << result.dropped_html_units << " dropped HTML code units)\n";
        return 0;
    }

    std::cout << "FAIL: " << failures.size() << " assertion(s)\n";
    for (const std::string& failure : failures) {
        std::cout << "  - " << failure << '\n';
    }
    return 1;
}

void PrintResult(const ParseResult& result)
{
    std::cout << "status=" << result.status
              << " visible_utf16_units=" << result.visible_text.size()
              << " html_blocks=" << result.html_block_count
              << " html_text_chunks=" << result.html_text_count
              << " dropped_html_units=" << result.dropped_html_units << '\n';

    for (const Event& event : result.events) {
        switch (event.kind) {
        case EventKind::EnterBlock:
            std::cout << "BLOCK+ " << BlockName(static_cast<MD_BLOCKTYPE>(event.type));
            if (event.type == static_cast<int>(MD_BLOCK_H)) {
                std::cout << " level=" << event.number;
            } else if (event.type == static_cast<int>(MD_BLOCK_LI) && event.flag) {
                std::cout << " task=" << static_cast<char>(event.number);
            } else if (event.type == static_cast<int>(MD_BLOCK_TABLE)) {
                std::cout << " columns=" << event.number
                          << " body_rows=" << EscapeForConsole(event.auxiliary);
            } else if (event.type == static_cast<int>(MD_BLOCK_CODE) && !event.value.empty()) {
                std::cout << " language=" << EscapeForConsole(event.value);
            }
            std::cout << '\n';
            break;
        case EventKind::LeaveBlock:
            std::cout << "BLOCK- " << BlockName(static_cast<MD_BLOCKTYPE>(event.type)) << '\n';
            break;
        case EventKind::EnterSpan:
            std::cout << "SPAN+ " << SpanName(static_cast<MD_SPANTYPE>(event.type));
            if (event.type == static_cast<int>(MD_SPAN_A)) {
                std::cout << " href=" << EscapeForConsole(event.value);
            } else if (event.type == static_cast<int>(MD_SPAN_IMG)) {
                std::cout << " src=" << EscapeForConsole(event.value);
            }
            std::cout << '\n';
            break;
        case EventKind::LeaveSpan:
            std::cout << "SPAN- " << SpanName(static_cast<MD_SPANTYPE>(event.type)) << '\n';
            break;
        case EventKind::Text:
            std::cout << "TEXT[" << TextName(static_cast<MD_TEXTTYPE>(event.type))
                      << "] \"" << EscapeForConsole(event.value) << "\"\n";
            break;
        case EventKind::DroppedHtml:
            std::cout << "DROP_HTML kind=" << EscapeForConsole(event.auxiliary)
                      << " units=" << event.number << '\n';
            break;
        }
    }
}

void PrintUsage()
{
    std::cout << "用法：md4c_parser_spike.exe --test | --sample\n";
    std::cout << "--test    运行离线 UTF-16 结构测试\n";
    std::cout << "--sample  输出事件收集结果；链接和图片只记录，不访问\n";
}

} // namespace

int wmain(int argc, wchar_t* argv[])
{
    SetConsoleOutputCP(CP_UTF8);

    if (argc == 2 && std::wstring_view(argv[1]) == L"--test") {
        return RunTests();
    }
    if (argc == 1 || (argc == 2 && std::wstring_view(argv[1]) == L"--sample")) {
        const ParseResult result = ParseMarkdown(kFixture);
        PrintResult(result);
        return result.status == 0 ? 0 : 1;
    }
    if (argc == 2 && (std::wstring_view(argv[1]) == L"--help" ||
                      std::wstring_view(argv[1]) == L"-h")) {
        PrintUsage();
        return 0;
    }

    PrintUsage();
    return 2;
}
