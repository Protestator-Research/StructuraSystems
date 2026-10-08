//
// GeneratedBlockGuard: the diagram editor must never overwrite hand-written text.
//

#include "../TestFramework.h"

#include <Diagram/Conversion/GeneratedBlockGuard.h>

using namespace StructuraSystems::Diagram;
using SyncState = GeneratedBlockGuard::SyncState;

namespace {
    const std::string Code = "part vehicle {\n    attribute mass;\n}\n";
}

TEST_CASE("Guard: wrapped block is recognized and in sync") {
    const std::string block = GeneratedBlockGuard::wrap(Code);
    CHECK(GeneratedBlockGuard::isGeneratedBlock(block));
    CHECK_EQ(GeneratedBlockGuard::extractCode(block).value(), Code);

    GeneratedBlockGuard guard;
    CHECK(guard.check(block) == SyncState::InSync);
    CHECK(guard.mayOverwrite(block));
}

TEST_CASE("Guard: manual edit is detected - even without any stored state (after restart)") {
    std::string block = GeneratedBlockGuard::wrap(Code);
    block.replace(block.find("mass"), 4, "weight");

    GeneratedBlockGuard freshGuard; // simulates a restart: nothing remembered
    CHECK(freshGuard.check(block) == SyncState::ModifiedByUser);
    CHECK(!freshGuard.mayOverwrite(block));
}

TEST_CASE("Guard: damaged or removed marker counts as hand-written") {
    GeneratedBlockGuard guard;
    CHECK(guard.check(Code) == SyncState::ModifiedByUser); // no marker at all

    std::string block = GeneratedBlockGuard::wrap(Code);
    block.insert(block.find('\n'), " x");                  // text appended to the marker line
    CHECK(guard.check(block) == SyncState::ModifiedByUser);

    std::string badHex = GeneratedBlockGuard::wrap(Code);
    badHex[GeneratedBlockGuard::MarkerPrefix.size()] = 'z';
    CHECK(guard.check(badHex) == SyncState::ModifiedByUser);
}

TEST_CASE("Guard: line endings and trailing whitespace do not count as edits") {
    std::string block = GeneratedBlockGuard::wrap(Code);
    std::string windows;
    for (const char c: block) {
        if (c == '\n')
            windows += '\r';
        windows += c;
    }
    GeneratedBlockGuard guard;
    CHECK(guard.check(windows) == SyncState::InSync);
    CHECK(guard.check(block + "\n\n  ") == SyncState::InSync);
    CHECK(guard.check("\n" + block) == SyncState::InSync);
}

TEST_CASE("Guard: missing block") {
    GeneratedBlockGuard guard;
    CHECK(guard.check(std::nullopt) == SyncState::NoBlockYet);
    guard.recordWrite();
    CHECK(guard.check(std::nullopt) == SyncState::BlockMissing);
    CHECK(guard.mayOverwrite(std::nullopt));
    guard.reset();
    CHECK(guard.check(std::nullopt) == SyncState::NoBlockYet);
}

TEST_CASE("Guard: fingerprint is stable") {
    // Fixed value: must not change between platforms/compilers, otherwise every stored
    // block would suddenly look "modified by user".
    CHECK_EQ(GeneratedBlockGuard::fingerprint(""), std::uint64_t{14695981039346656037ULL});
    CHECK(GeneratedBlockGuard::fingerprint("a") != GeneratedBlockGuard::fingerprint("b"));
}
