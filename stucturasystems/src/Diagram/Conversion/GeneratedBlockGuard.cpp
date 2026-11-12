//
// Protects hand-written SysML text from being overwritten by the diagram editor.
//

#include "GeneratedBlockGuard.h"

#include <algorithm>
#include <cstdio>

namespace StructuraSystems::Diagram {

    namespace {
        bool isWhitespace(char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        }

        std::string normalize(const std::string &text) {
            std::string result;
            result.reserve(text.size());
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                    continue;
                result += text[i];
            }
            while (!result.empty() && isWhitespace(result.back()))
                result.pop_back();
            return result;
        }

        std::string toHex(std::uint64_t value) {
            char buffer[17];
            std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
            return buffer;
        }

        struct ParsedBlock {
            std::uint64_t StoredFingerprint;
            std::string Code;
            std::string Layout;
        };

        std::optional<ParsedBlock> parse(const std::string &blockText) {
            std::size_t start = 0;
            while (start < blockText.size() && isWhitespace(blockText[start]))
                ++start;

            const std::string_view prefix = GeneratedBlockGuard::MarkerPrefix;
            const std::string_view suffix = GeneratedBlockGuard::MarkerSuffix;
            if (blockText.compare(start, prefix.size(), prefix) != 0)
                return std::nullopt;

            const std::size_t hexStart = start + prefix.size();
            constexpr std::size_t hexLength = 16;
            if (hexStart + hexLength > blockText.size())
                return std::nullopt;

            std::uint64_t value = 0;
            for (std::size_t i = hexStart; i < hexStart + hexLength; ++i) {
                const char c = blockText[i];
                value <<= 4;
                if (c >= '0' && c <= '9')
                    value |= static_cast<std::uint64_t>(c - '0');
                else if (c >= 'a' && c <= 'f')
                    value |= static_cast<std::uint64_t>(c - 'a' + 10);
                else
                    return std::nullopt;
            }

            if (blockText.compare(hexStart + hexLength, suffix.size(), suffix) != 0)
                return std::nullopt;

            std::size_t codeStart = hexStart + hexLength + suffix.size();
            if (codeStart < blockText.size() && blockText[codeStart] == '\r')
                ++codeStart;
            if (codeStart < blockText.size() && blockText[codeStart] == '\n')
                ++codeStart;
            else if (codeStart != blockText.size())
                return std::nullopt; // something was appended to the marker line

            // "// Layout: ..." right below the marker: positions of the diagram, not part of the code
            std::string layout;
            const std::string_view layoutPrefix = GeneratedBlockGuard::LayoutPrefix;
            if (blockText.compare(codeStart, layoutPrefix.size(), layoutPrefix) == 0) {
                std::size_t lineEnd = blockText.find('\n', codeStart);
                if (lineEnd == std::string::npos)
                    lineEnd = blockText.size();
                layout = blockText.substr(codeStart + layoutPrefix.size(), lineEnd - codeStart - layoutPrefix.size());
                if (!layout.empty() && layout.back() == '\r')
                    layout.pop_back();
                codeStart = std::min(blockText.size(), lineEnd + 1);
            }
            return ParsedBlock{value, blockText.substr(codeStart), layout};
        }
    }

    std::string GeneratedBlockGuard::wrap(const std::string &generatedCode, const std::string &layout) {
        std::string line = layout;
        line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        return std::string(MarkerPrefix) + toHex(fingerprint(generatedCode)) + std::string(MarkerSuffix) + "\n" +
               (line.empty() ? std::string() : std::string(LayoutPrefix) + line + "\n") + generatedCode;
    }

    std::string GeneratedBlockGuard::extractLayout(const std::string &blockText) {
        const auto parsed = parse(blockText);
        return parsed ? parsed->Layout : std::string();
    }

    bool GeneratedBlockGuard::isGeneratedBlock(const std::string &blockText) {
        return parse(blockText).has_value();
    }

    std::optional<std::string> GeneratedBlockGuard::extractCode(const std::string &blockText) {
        const auto parsed = parse(blockText);
        if (!parsed.has_value())
            return std::nullopt;
        return parsed->Code;
    }

    std::uint64_t GeneratedBlockGuard::fingerprint(const std::string &text) {
        // FNV-1a, 64 bit: stable across platforms and runs (unlike std::hash).
        std::uint64_t hash = 14695981039346656037ULL;
        for (const char c: normalize(text)) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    void GeneratedBlockGuard::recordWrite() {
        HasWritten = true;
    }

    GeneratedBlockGuard::SyncState GeneratedBlockGuard::check(const std::optional<std::string> &currentBlockText) const {
        if (!currentBlockText.has_value())
            return HasWritten ? SyncState::BlockMissing : SyncState::NoBlockYet;

        const auto parsed = parse(*currentBlockText);
        if (!parsed.has_value())
            return SyncState::ModifiedByUser; // marker removed or damaged: treat as hand-written text

        return fingerprint(parsed->Code) == parsed->StoredFingerprint ? SyncState::InSync
                                                                       : SyncState::ModifiedByUser;
    }

    bool GeneratedBlockGuard::mayOverwrite(const std::optional<std::string> &currentBlockText) const {
        return check(currentBlockText) != SyncState::ModifiedByUser;
    }

    void GeneratedBlockGuard::reset() {
        HasWritten = false;
    }
}
