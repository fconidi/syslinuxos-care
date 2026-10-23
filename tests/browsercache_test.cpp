#include "browsercache.h"

#include <QDir>
#include <QTemporaryDir>

#include <cassert>

int main() {
    QTemporaryDir temporary(QDir::tempPath() + "/browsercache-test-XXXXXX");
    assert(temporary.isValid());
    QDir root(temporary.path());
    assert(root.mkpath("mozilla/firefox/example.default/cache2/entries"));
    assert(root.mkpath("chromium/Default/Cache/Cache_Data"));
    assert(root.mkpath("google-chrome/Profile 1/Cache"));
    const auto caches = discoverBrowserCaches(root.path());
    assert(caches.size() == 3);
    assert(caches.first().kind == BrowserKind::Firefox);
    assert(caches.first().profile == "example.default");
    assert(browserName(BrowserKind::Chrome) == "Google Chrome");
    return 0;
}
