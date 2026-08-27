#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <psapi.h>

#include "MarkdownDocument.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {

bool AlwaysCancel(void*) noexcept
{
    return true;
}

using LocalMarkdownPreview::MarkdownDocumentLimits;
using LocalMarkdownPreview::MarkdownDocumentResult;
using LocalMarkdownPreview::MarkdownFormatKind;
using LocalMarkdownPreview::MarkdownFormatRange;
using LocalMarkdownPreview::MarkdownParseStatus;
using LocalMarkdownPreview::ParseMarkdownDocument;

const std::wstring kFixture = LR"MD4C(# 中文标题

这是 **粗体**、*斜体* 和 ~~删除线~~，实体是 &amp;。

- 普通无序列表
- [x] 已完成任务
- [ ] 待办任务

1. 有序列表第一项
2. 有序列表第二项

> 引用内容

行内 `代码`，以及下面的代码块：

```cpp
const int answer = 42;
```

| 名称 | 值 |
| :--- | ---: |
| 中文 | 42 |

[链接文本](https://example.invalid/docs "链接标题") 和 ![图片替代文字](assets/pic.png "图片标题")

<div>
这段 HTML 块必须丢弃
</div>

这一段含有行内 <span>HTML</span> 标签。
)MD4C";

struct TestContext {
    std::size_t assertions = 0;
    std::size_t failures = 0;

    void Expect(bool condition, const char* message)
    {
        ++assertions;
        if (!condition) {
            ++failures;
            std::cout << "FAIL: " << message << '\n';
        }
    }
};

std::wstring MakeDocument(std::size_t minimum_characters)
{
    const std::wstring paragraph =
        L"## 性能测试标题\n\n"
        L"这是一段普通中文 Markdown 内容，包含 **粗体**、*斜体*、`代码` 和 "
        L"[安全链接](https://example.invalid/)。\n\n";
    std::wstring document;
    document.reserve(minimum_characters + paragraph.size());
    while (document.size() < minimum_characters) {
        document += paragraph;
    }
    return document;
}

double ParseElapsedMilliseconds(std::wstring_view markdown,
                                MarkdownDocumentResult& result)
{
    MarkdownDocumentLimits limits;
    limits.max_nodes = 500000;
    limits.max_output_chars = markdown.size() * 2 + 1024;
    limits.max_parse_milliseconds = 5000;
    const auto started = std::chrono::steady_clock::now();
    result = ParseMarkdownDocument(markdown, limits);
    const auto finished = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(finished - started).count();
}

std::size_t CurrentPrivateBytes()
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters))) {
        return 0;
    }
    return static_cast<std::size_t>(counters.PrivateUsage);
}

void RunDeterministicFuzzTests(TestContext& tests)
{
    constexpr wchar_t alphabet[] =
        L"#*_~`[]()<>!|\\\n\r\t 中文abc123:/.-\0";
    std::uint32_t state = 0x4D445052u;
    auto next = [&state]() noexcept {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    };

    MarkdownDocumentLimits limits;
    limits.max_nodes = 4096;
    limits.max_output_chars = 8192;
    limits.max_parse_milliseconds = 100;
    for (std::size_t case_index = 0; case_index < 2000; ++case_index) {
        std::wstring input;
        input.reserve(256);
        const std::size_t length = next() % 256;
        for (std::size_t index = 0; index < length; ++index) {
            input.push_back(alphabet[next() % (std::size(alphabet) - 1)]);
        }

        const MarkdownDocumentResult fuzzed = ParseMarkdownDocument(input, limits);
        tests.Expect(fuzzed.status == MarkdownParseStatus::Ok ||
                         fuzzed.status == MarkdownParseStatus::NodeLimitExceeded ||
                         fuzzed.status == MarkdownParseStatus::OutputLimitExceeded ||
                         fuzzed.status == MarkdownParseStatus::TimeLimitExceeded ||
                         fuzzed.status == MarkdownParseStatus::ParserError,
                     "fuzz input ends in a bounded parser status");
        tests.Expect(fuzzed.output_char_count == fuzzed.visible_text.size(),
                     "fuzz output count remains consistent");
        tests.Expect(fuzzed.visible_text.size() <= limits.max_output_chars,
                     "fuzz output remains within its configured limit");
        for (const MarkdownFormatRange& range : fuzzed.formats) {
            tests.Expect(range.start <= fuzzed.visible_text.size() &&
                             range.length <= fuzzed.visible_text.size() - range.start,
                         "fuzz format metadata remains in bounds");
        }
    }
}

void RunPerformanceTests(TestContext& tests)
{
    const std::wstring document_100kb = MakeDocument(100u * 1024u);
    const std::wstring document_1mb = MakeDocument(1u * 1024u * 1024u);

    MarkdownDocumentResult warmup;
    ParseElapsedMilliseconds(document_100kb, warmup);

    MarkdownDocumentResult result_100kb;
    const double elapsed_100kb =
        ParseElapsedMilliseconds(document_100kb, result_100kb);
    tests.Expect(result_100kb.status == MarkdownParseStatus::Ok,
                 "100 KB performance fixture parses successfully");
    tests.Expect(elapsed_100kb <= 250.0,
                 "warm 100 KB component parse stays within 250 ms");

    MarkdownDocumentResult result_1mb;
    const double elapsed_1mb = ParseElapsedMilliseconds(document_1mb, result_1mb);
    tests.Expect(result_1mb.status == MarkdownParseStatus::Ok,
                 "1 MB performance fixture parses successfully");
    tests.Expect(elapsed_1mb <= 1500.0,
                 "1 MB component parse stays within 1.5 seconds");

    const std::size_t private_before = CurrentPrivateBytes();
    for (std::size_t index = 0; index < 500; ++index) {
        MarkdownDocumentResult switched;
        ParseElapsedMilliseconds(document_100kb, switched);
        tests.Expect(switched.status == MarkdownParseStatus::Ok,
                     "sequential preview parse succeeds");
    }
    SetProcessWorkingSetSize(GetCurrentProcess(),
                             static_cast<SIZE_T>(-1),
                             static_cast<SIZE_T>(-1));
    const std::size_t private_after = CurrentPrivateBytes();
    const std::size_t private_growth = private_after > private_before
        ? private_after - private_before
        : 0;
    tests.Expect(private_before == 0 || private_after == 0 ||
                     private_growth <= 20u * 1024u * 1024u,
                 "500 sequential parses do not retain more than 20 MB private bytes");

    std::cout << "PERF: 100KB=" << elapsed_100kb
              << "ms, 1MB=" << elapsed_1mb
              << "ms, private-growth-after-500=" << private_growth
              << " bytes\n";
}

bool HasFormat(const MarkdownDocumentResult& result,
               MarkdownFormatKind kind,
               unsigned level = 0)
{
    for (const MarkdownFormatRange& range : result.formats) {
        if (range.kind == kind && (level == 0 || range.level == level)) {
            return true;
        }
    }
    return false;
}

std::size_t CountFormat(const MarkdownDocumentResult& result,
                        MarkdownFormatKind kind)
{
    std::size_t count = 0;
    for (const MarkdownFormatRange& range : result.formats) {
        if (range.kind == kind) {
            ++count;
        }
    }
    return count;
}

int RunTests()
{
    TestContext tests;
    const MarkdownDocumentResult result = ParseMarkdownDocument(kFixture);

    tests.Expect(result.status == MarkdownParseStatus::Ok,
                 "fixture parses successfully");
    tests.Expect(result.Succeeded(), "Succeeded reports true for valid input");
    tests.Expect(result.node_count > 0, "callback nodes were counted");
    tests.Expect(result.output_char_count == result.visible_text.size(),
                 "output character count uses UTF-16 units");
    tests.Expect(result.visible_text.find(L"中文标题") != std::wstring::npos,
                 "Chinese heading is visible");
    tests.Expect(result.visible_text.find(L"&amp;") != std::wstring::npos,
                 "entity text remains visible");

    tests.Expect(HasFormat(result, MarkdownFormatKind::Heading, 1),
                 "heading level is represented");
    tests.Expect(HasFormat(result, MarkdownFormatKind::Bold),
                 "bold range is represented");
    tests.Expect(HasFormat(result, MarkdownFormatKind::Italic),
                 "italic range is represented");
    tests.Expect(HasFormat(result, MarkdownFormatKind::Strikethrough),
                 "strikethrough range is represented");
    tests.Expect(HasFormat(result, MarkdownFormatKind::Code),
                 "code range is represented");
    tests.Expect(CountFormat(result, MarkdownFormatKind::Code) >= 2,
                 "inline and block code ranges are both represented");
    tests.Expect(HasFormat(result, MarkdownFormatKind::Quote),
                 "quote range is represented");
    tests.Expect(HasFormat(result, MarkdownFormatKind::TableHeader),
                 "table header range is represented");
    tests.Expect(HasFormat(result, MarkdownFormatKind::Link),
                 "link range is represented");

    tests.Expect(result.visible_text.find(L"- 普通无序列表") != std::wstring::npos,
                 "unordered list marker is visible");
    tests.Expect(result.visible_text.find(L"- [x] 已完成任务") != std::wstring::npos,
                 "completed task marker is visible");
    tests.Expect(result.visible_text.find(L"- [ ] 待办任务") != std::wstring::npos,
                 "pending task marker is visible");
    tests.Expect(result.visible_text.find(L"1. 有序列表第一项") != std::wstring::npos,
                 "ordered list marker is visible");
    tests.Expect(result.visible_text.find(L"> 引用内容") != std::wstring::npos,
                 "quote marker is visible");
    tests.Expect(result.visible_text.find(L"\t值") != std::wstring::npos,
                 "table cells are separated in visible text");

    tests.Expect(result.links.size() == 1, "one link was collected");
    if (!result.links.empty()) {
        tests.Expect(result.links[0].target == L"https://example.invalid/docs",
                     "link target is retained for audit only");
        tests.Expect(result.links[0].length > 0,
                     "link range covers visible link text");
        tests.Expect(result.visible_text.find(L"链接文本") != std::wstring::npos,
                     "link text is visible");
        tests.Expect(result.visible_text.find(L"(https://example.invalid/docs)") !=
                         std::wstring::npos,
                     "link target is displayed without navigation");
    }

    tests.Expect(result.images.size() == 1, "one image was collected");
    if (!result.images.empty()) {
        tests.Expect(result.images[0].source == L"assets/pic.png",
                     "image source is retained for audit only");
        tests.Expect(result.visible_text.find(L"[图片: assets/pic.png]") !=
                         std::wstring::npos,
                     "image is represented by an auditable placeholder");
        tests.Expect(result.visible_text.find(L"图片替代文字") == std::wstring::npos,
                     "image alt text is not duplicated beside the placeholder");
    }

    tests.Expect(result.dropped_html_node_count > 0,
                 "raw HTML callbacks are counted as dropped nodes");
    tests.Expect(result.dropped_html_char_count > 0,
                 "raw HTML text length is counted without retaining source");
    tests.Expect(result.visible_text.find(L"<div>") == std::wstring::npos,
                 "block HTML is absent from visible text");
    tests.Expect(result.visible_text.find(L"<span>") == std::wstring::npos,
                 "inline HTML is absent from visible text");
    tests.Expect(result.visible_text.find(L"这段 HTML 块必须丢弃") ==
                     std::wstring::npos,
                 "block HTML contents are dropped");

    for (const MarkdownFormatRange& range : result.formats) {
        tests.Expect(range.length > 0, "format range is non-empty");
        tests.Expect(range.start <= result.visible_text.size(),
                     "format range starts within visible text");
        tests.Expect(range.length <= result.visible_text.size() - range.start,
                     "format range ends within visible text");
    }
    for (const auto& link : result.links) {
        tests.Expect(link.start <= result.visible_text.size(),
                     "link metadata starts within visible text");
        tests.Expect(link.length <= result.visible_text.size() - link.start,
                     "link metadata ends within visible text");
    }
    for (const auto& image : result.images) {
        tests.Expect(image.start <= result.visible_text.size(),
                     "image metadata starts within visible text");
        tests.Expect(image.length <= result.visible_text.size() - image.start,
                     "image metadata ends within visible text");
    }

    std::wstring with_null = L"前";
    with_null.push_back(L'\0');
    with_null += L"后";
    const MarkdownDocumentResult null_result = ParseMarkdownDocument(with_null);
    tests.Expect(null_result.status == MarkdownParseStatus::Ok,
                 "embedded null input parses successfully");
    tests.Expect(null_result.visible_text.find(L'\uFFFD') != std::wstring::npos,
                 "embedded null is replaced with U+FFFD");

    MarkdownDocumentLimits node_limits;
    node_limits.max_nodes = 5;
    node_limits.max_output_chars = 1024;
    const MarkdownDocumentResult node_limited =
        ParseMarkdownDocument(kFixture, node_limits);
    tests.Expect(node_limited.status == MarkdownParseStatus::NodeLimitExceeded,
                 "node limit aborts parsing");
    tests.Expect(node_limited.node_count == node_limits.max_nodes,
                 "node callback stops exactly at node limit");

    MarkdownDocumentLimits output_limits;
    output_limits.max_nodes = 8192;
    output_limits.max_output_chars = 24;
    const MarkdownDocumentResult output_limited =
        ParseMarkdownDocument(kFixture, output_limits);
    tests.Expect(output_limited.status == MarkdownParseStatus::OutputLimitExceeded,
                 "output limit aborts parsing");
    tests.Expect(output_limited.output_char_count <=
                     output_limits.max_output_chars,
                 "output never exceeds output limit");

    MarkdownDocumentLimits zero_limits;
    zero_limits.max_nodes = 0;
    zero_limits.max_output_chars = 0;
    const MarkdownDocumentResult zero_limited =
        ParseMarkdownDocument(kFixture, zero_limits);
    tests.Expect(zero_limited.status == MarkdownParseStatus::NodeLimitExceeded,
                 "zero node limit aborts before first callback");
    tests.Expect(zero_limited.node_count == 0,
                 "zero node limit emits no callbacks");

    MarkdownDocumentLimits cancelled_limits;
    cancelled_limits.should_cancel = &AlwaysCancel;
    const MarkdownDocumentResult cancelled =
        ParseMarkdownDocument(kFixture, cancelled_limits);
    tests.Expect(cancelled.status == MarkdownParseStatus::Cancelled,
                 "cancellation aborts before the first parser event");
    tests.Expect(cancelled.node_count == 0,
                 "cancelled parsing emits no accepted nodes");

    RunDeterministicFuzzTests(tests);
    RunPerformanceTests(tests);

    if (tests.failures == 0) {
        std::cout << "PASS: MarkdownDocument tests ("
                  << tests.assertions << " assertions, "
                  << result.visible_text.size() << " UTF-16 output units, "
                  << result.node_count << " nodes)\n";
        return 0;
    }

    std::cout << "FAIL: " << tests.failures << " of "
              << tests.assertions << " assertions\n";
    return 1;
}

} // namespace

int wmain()
{
    SetConsoleOutputCP(CP_UTF8);
    return RunTests();
}
