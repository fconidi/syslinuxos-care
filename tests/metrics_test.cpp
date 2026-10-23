#include "metrics.h"

#include <cassert>

int main() {
    quint64 busy = 0, total = 0;
    assert(MetricsReader::parseCpu("cpu  10 2 3 85 0 0 0 0\n", busy, total));
    assert(busy == 15 && total == 100);
    assert(MetricsReader::parseCpu("cpu  10 2 3 85 0 0 0 0 8 2\n", busy, total));
    assert(busy == 15 && total == 100);
    assert(!MetricsReader::parseCpu("cpu nonsense\n", busy, total));
    quint64 available = 0;
    assert(MetricsReader::parseMemory("MemTotal: 1000 kB\nMemAvailable: 400 kB\n", total, available));
    assert(total == 1024000 && available == 409600);
    quint64 rx = 0, tx = 0;
    assert(MetricsReader::parseNetwork("Inter-| Receive | Transmit\nlo: 1 0 0 0 0 0 0 0 2 0 0 0 0 0 0 0\neth0: 42 0 0 0 0 0 0 0 84 0 0 0 0 0 0 0\n", rx, tx));
    assert(rx == 42 && tx == 84);
    return 0;
}
