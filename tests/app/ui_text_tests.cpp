// Untrusted strings (share-code names and hints, Workshop descriptors, paths)
// reach the QML through model roles, Backend properties and the import
// preview. Text.AutoText, the default of Text and Label, renders a string that
// starts with a known tag as rich text and loads its <img src> through the
// network, so every text element of Pmm.Ui whose text is not a fixed literal
// must say textFormat: Text.PlainText. This test reads the QML sources.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Elements that render a text property (Text, Label) or edit one.
const std::set<std::string> kTextTypes = {"Text", "Label", "TextEdit", "TextArea"};

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

// Index just past the string literal that starts at `i` (", ' or `).
std::size_t skipString(const std::string& code, std::size_t i) {
    const char quote = code[i];
    for (++i; i < code.size(); ++i) {
        if (code[i] == '\\')
            ++i;
        else if (code[i] == quote)
            return i + 1;
    }
    return code.size();
}

bool isQuote(char c) {
    return c == '"' || c == '\'' || c == '`';
}

bool isIdentifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
}

// Comments become spaces; newlines are kept so line numbers stay valid.
std::string stripComments(const std::string& code) {
    std::string out = code;
    for (std::size_t i = 0; i < out.size();) {
        if (isQuote(out[i])) {
            i = skipString(out, i);
        } else if (out.compare(i, 2, "//") == 0) {
            for (; i < out.size() && out[i] != '\n'; ++i)
                out[i] = ' ';
        } else if (out.compare(i, 2, "/*") == 0) {
            const std::size_t end = std::min(out.find("*/", i + 2), out.size() - 2) + 2;
            for (; i < end; ++i) {
                if (out[i] != '\n')
                    out[i] = ' ';
            }
        } else {
            ++i;
        }
    }
    return out;
}

// Index of the brace that closes the one at `open`.
std::size_t matchingBrace(const std::string& code, std::size_t open) {
    int depth = 0;
    for (std::size_t i = open; i < code.size();) {
        if (isQuote(code[i])) {
            i = skipString(code, i);
            continue;
        }
        if (code[i] == '{')
            ++depth;
        else if (code[i] == '}' && --depth == 0)
            return i;
        ++i;
    }
    return code.size();
}

// The type name in front of `open` when the brace opens an object
// declaration ("Text {", "contentItem: Text {"), else "".
std::string objectTypeBefore(const std::string& code, std::size_t open) {
    std::size_t end = open;
    while (end > 0 && std::isspace(static_cast<unsigned char>(code[end - 1])))
        --end;
    std::size_t begin = end;
    while (begin > 0 && isIdentifierChar(code[begin - 1]))
        --begin;
    std::string name = code.substr(begin, end - begin);
    const std::size_t dot = name.rfind('.');
    const std::string last = dot == std::string::npos ? name : name.substr(dot + 1);
    if (last.empty() || !std::isupper(static_cast<unsigned char>(last[0])))
        return {};
    return name;
}

struct Element {
    std::string type;
    std::size_t line = 0;
    std::map<std::string, std::string> bindings; // direct property bindings
};

// Property bindings of an object body, without those of nested objects.
std::map<std::string, std::string> directBindings(const std::string& body) {
    std::string direct;
    int depth = 0;
    for (std::size_t i = 0; i < body.size();) {
        if (isQuote(body[i])) {
            const std::size_t end = skipString(body, i);
            direct.append(body, i, end - i);
            i = end;
            continue;
        }
        const char c = body[i];
        if (c == '{' && depth == 0 && !objectTypeBefore(body, i).empty()) {
            // A child object: drop it with its type name.
            while (!direct.empty() && std::isspace(static_cast<unsigned char>(direct.back())))
                direct.pop_back();
            while (!direct.empty() && isIdentifierChar(direct.back()))
                direct.pop_back();
            i = matchingBrace(body, i) + 1;
            continue;
        }
        if (c == '{' || c == '(' || c == '[')
            ++depth;
        else if (c == '}' || c == ')' || c == ']')
            --depth;
        direct.push_back(c);
        ++i;
    }

    // Statements at depth 0: "name: value", where the value continues over
    // open brackets and lines that start or end with an operator.
    std::map<std::string, std::string> out;
    static const std::regex start(
        R"(^\s*(?:(?:required|readonly|default)\s+)*(?:property\s+\S+\s+)?([A-Za-z_][\w.]*)\s*:(?!:)\s*)");
    static const std::regex declaration(
        R"(^\s*(?:function|signal|enum|component|required|readonly|default|property)\b)");
    std::istringstream lines(direct);
    std::string line;
    std::string name;
    std::string value;
    int open = 0;
    bool continued = false;
    const auto flush = [&] {
        if (!name.empty())
            out[name] = value;
        name.clear();
        value.clear();
    };
    while (std::getline(lines, line)) {
        std::size_t first = line.find_first_not_of(" \t\r");
        const bool continuation = first != std::string::npos &&
                                  std::string(".?:+-*/|&").find(line[first]) != std::string::npos;
        std::smatch match;
        const bool statement = open == 0 && !continued && !continuation;
        if (statement && std::regex_search(line, match, start)) {
            flush();
            name = match[1];
            value = match.suffix();
        } else if (statement && std::regex_search(line, declaration)) {
            flush();
        } else if (!name.empty()) {
            value += '\n' + line;
        }
        for (std::size_t i = 0; i < line.size();) {
            if (isQuote(line[i])) {
                i = skipString(line, i);
                continue;
            }
            if (line[i] == '{' || line[i] == '(' || line[i] == '[')
                ++open;
            else if (line[i] == '}' || line[i] == ')' || line[i] == ']')
                --open;
            ++i;
        }
        const std::size_t last = line.find_last_not_of(" \t\r");
        continued = last != std::string::npos &&
                    std::string("+-*/|&?:,(=").find(line[last]) != std::string::npos;
    }
    flush();
    return out;
}

std::vector<Element> textElements(const std::string& source) {
    const std::string code = stripComments(source);
    std::vector<Element> out;
    for (std::size_t i = 0; i < code.size();) {
        if (isQuote(code[i])) {
            i = skipString(code, i);
            continue;
        }
        if (code[i] == '{') {
            const std::string type = objectTypeBefore(code, i);
            if (kTextTypes.count(type)) {
                const std::size_t close = matchingBrace(code, i);
                Element element;
                element.type = type;
                element.line =
                    1 + static_cast<std::size_t>(std::count(
                            code.begin(), code.begin() + static_cast<std::ptrdiff_t>(i), '\n'));
                element.bindings = directBindings(code.substr(i + 1, close - i - 1));
                out.push_back(std::move(element));
            }
        }
        ++i;
    }
    return out;
}

// "text", qsTr("text") and concatenations of literals.
bool isFixedText(const std::string& value) {
    static const std::regex literal(
        R"(^\s*(qsTr\(\s*)?("([^"\\]|\\.)*"|'([^'\\]|\\.)*')(\s*\+\s*("([^"\\]|\\.)*"|'([^'\\]|\\.)*'))*\s*\)?\s*$)");
    return std::regex_match(value, literal);
}

std::vector<std::string> violations(const std::string& file, const std::string& source) {
    std::vector<std::string> out;
    for (const Element& element : textElements(source)) {
        const auto text = element.bindings.find("text");
        if (text != element.bindings.end() && isFixedText(text->second))
            continue;
        const auto format = element.bindings.find("textFormat");
        if (format != element.bindings.end() &&
            format->second.find("PlainText") != std::string::npos)
            continue;
        out.push_back(file + ":" + std::to_string(element.line) + ": " + element.type +
                      " shows non-literal text without textFormat: Text.PlainText");
    }
    return out;
}

} // namespace

TEST_CASE("The text checker recognises data shown as rich text", "[app][qml][lint]") {
    CHECK(violations("a.qml", "Text { text: model.name }").size() == 1);
    CHECK(violations("a.qml", "Text {\n text: model.name\n textFormat: Text.PlainText\n}").empty());
    CHECK(violations("a.qml", "Label { text: qsTr(\"Fixed\") }").empty());
    CHECK(violations("a.qml", "Text { text: \"a\" + \"b\" }").empty());
    CHECK(violations("a.qml", "Item {\n Text {\n  text: qsTr(\"%1\")\n   .arg(x)\n }\n}").size() ==
          1);
    // A component's root Text gets its text from the outside.
    CHECK(violations("a.qml", "Text {\n font.bold: true\n}").size() == 1);
    // The text of a child object does not count for its parent, and vice versa.
    CHECK(violations("a.qml", "Text {\n text: \"x\"\n Rectangle { property string text: y }\n}")
              .empty());
    CHECK(violations("a.qml", "Text {\n text: {\n  return x\n }\n textFormat: Text.PlainText\n}")
              .empty());
    CHECK(violations("a.qml", "contentItem: Text {\n text: control.text // x\n}").size() == 1);
    CHECK(violations("a.qml", "Text { /* text: \"x\" */ text: root.y }").size() == 1);
}

TEST_CASE("Pmm.Ui shows every non-literal text as plain text", "[app][qml][lint]") {
    const fs::path root = PMM_UI_SOURCE_DIR;
    std::vector<std::string> found;
    std::size_t files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        const fs::path relative = fs::relative(entry.path(), root);
        if (!entry.is_regular_file() || entry.path().extension() != ".qml" ||
            *relative.begin() == "dev") // previews and fake data, not shipped
            continue;
        ++files;
        for (std::string& v : violations(relative.generic_string(), readFile(entry.path())))
            found.push_back(std::move(v));
    }
    CHECK(files > 40);
    std::string report;
    for (const std::string& v : found)
        report += v + "\n";
    INFO(report);
    CHECK(found.empty());
}
