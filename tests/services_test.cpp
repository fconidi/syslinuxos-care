#include "services.h"

#include <cassert>

int main() {
    QString error;
    const auto entries = parseServices(
        R"([{"unit":"example.service","active":"active","description":"Example"},{"unit":"other.timer"}])",
        true, error);
    assert(error.isEmpty());
    assert(entries.size() == 1);
    assert(entries.first().unit == "example.service");
    assert(entries.first().active == "active");
    assert(entries.first().user);
    auto withStates = entries;
    applyServiceFileStates(withStates, R"([{"unit_file":"example.service","state":"enabled"},{"unit_file":"extra.service","state":"disabled"}])", true, error);
    assert(error.isEmpty());
    assert(withStates.first().enabled == "enabled");
    assert(withStates.size() == 2);
    assert(withStates.last().unit == "extra.service");
    assert(withStates.last().user);
    assert(parseServices("invalid", false, error).isEmpty());
    assert(!error.isEmpty());
    assert(!setUserServiceRunning("example.service;rm", false, error));
    assert(!setUserServiceEnabled("example.service;rm", true, error));
    assert(validServiceUnit("example@instance.service"));
    assert(!validServiceUnit(".service"));
    assert(!validServiceUnit("-example.service"));
    assert(!validServiceUnit("example.service;rm"));
    assert(!validServiceUnit(QString(260, 'a') + ".service"));
    return 0;
}
