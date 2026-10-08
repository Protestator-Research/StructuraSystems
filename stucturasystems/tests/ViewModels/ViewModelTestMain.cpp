//
// Runs the view model tests of the diagram editor. They need a QCoreApplication (timers, queued calls).
// Optional argument: only run tests whose name contains it.
//

#include "../TestFramework.h"

#include <QCoreApplication>

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    const std::string filter = argc > 1 ? argv[1] : "";
    int failed = 0;
    int run = 0;

    for (const auto &test: StructuraSystems::Test::registry()) {
        if (!filter.empty() && test.Name.find(filter) == std::string::npos)
            continue;
        ++run;
        try {
            test.Body();
            std::cout << "[  OK  ] " << test.Name << "\n";
        } catch (const std::exception &exception) {
            ++failed;
            std::cout << "[ FAIL ] " << test.Name << "\n" << exception.what() << "\n";
        }
    }

    std::cout << "\n" << run - failed << " of " << run << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
