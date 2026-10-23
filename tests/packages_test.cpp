#include "packages.h"

#include <cassert>

int main() {
    const auto packages = parseInstalledPackages("ii \talpha\t1.0\nrc \told\t0.1\nii \tbeta:amd64\t2.0-1\n");
    assert(packages.size() == 2);
    assert(packages.first().name == "alpha");
    assert(packages.last().version == "2.0-1");
    const auto plan = parseRemovalPlan("Reading package lists...\nRemv alpha [1.0]\nRemv beta:amd64 [2.0-1]\n");
    assert(plan.size() == 2);
    assert(plan.last() == "beta:amd64");
    QString error;
    assert(simulatePackageRemoval("alpha;danger", error).isEmpty());
    assert(!error.isEmpty());
    return 0;
}
