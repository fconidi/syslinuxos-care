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
    assert(parseServices("invalid", false, error).isEmpty());
    assert(!error.isEmpty());
    assert(!setUserServiceRunning("example.service;rm", false, error));
    return 0;
}
