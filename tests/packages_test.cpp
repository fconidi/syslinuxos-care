#include "packages.h"

#include <cassert>

int main() {
    const auto packages = parseInstalledPackages("ii \talpha\t1.0\nrc \told\t0.1\nii \tbeta:amd64\t2.0-1\n");
    assert(packages.size() == 2);
    assert(packages.first().name == "alpha");
    assert(packages.last().version == "2.0-1");
    const auto plan = parseRemovalPlan("Reading package lists...\nRemv alpha [1.0]\nRemv beta:amd64 [2.0-1]\nInst gamma (1.0 Debian:13/stable)\nConf gamma (1.0 Debian:13/stable)\n");
    assert(plan.removals.size() == 2);
    assert(plan.removals.last() == "beta:amd64");
    assert(plan.removalDetails.last() == "Remv beta:amd64 [2.0-1]");
    assert(plan.otherActions.size() == 2);
    assert(installedPackageVersion(packages, "beta:amd64") == "2.0-1");
    assert(installedPackageVersion(packages, "missing").isEmpty());
    assert(removalPlanFingerprint("alpha", "1.0", plan) !=
           removalPlanFingerprint("alpha", "2.0", plan));
    auto changedPlan = plan;
    changedPlan.removals.removeLast();
    assert(removalPlanFingerprint("alpha", "1.0", plan) !=
           removalPlanFingerprint("alpha", "1.0", changedPlan));
    changedPlan = plan;
    changedPlan.removalDetails[1] = "Remv beta:amd64 [3.0-1]";
    assert(removalPlanFingerprint("alpha", "1.0", plan) !=
           removalPlanFingerprint("alpha", "1.0", changedPlan));
    QString error;
    assert(simulatePackageRemoval("alpha;danger", error).removals.isEmpty());
    assert(!error.isEmpty());
    error.clear();
    assert(simulatePackageRemoval("alpha+", error).removals.isEmpty());
    assert(!error.isEmpty());
    return 0;
}
