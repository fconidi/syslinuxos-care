#include "browsercache.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cassert>
#include <csignal>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

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
    const QString home = root.filePath("home");
    const QString config = root.filePath("config");
    assert(root.mkpath("home/.mozilla/firefox/example.default"));
    assert(root.mkpath("config/chromium/Default/Network"));
    assert(root.mkpath("config/google-chrome/Profile 1"));
    auto touch = [](const QString &path) {
        QFile file(path);
        assert(file.open(QIODevice::WriteOnly));
        assert(file.write("db") == 2);
    };
    touch(home + "/.mozilla/firefox/example.default/cookies.sqlite");
    touch(home + "/.mozilla/firefox/example.default/places.sqlite");
    touch(config + "/chromium/Default/History");
    touch(config + "/chromium/Default/History-wal");
    touch(config + "/chromium/Default/Network/Cookies");
    touch(config + "/google-chrome/Profile 1/Cookies");
    const auto data = discoverBrowserData(config, home);
    assert(data.size() == 4);
    assert(data.first().browser == BrowserKind::Firefox);
    assert(data.first().kind == BrowserDataKind::Cookies);
    assert(data.first().paths.first() == "cookies.sqlite");
    assert(scanBrowserData(data.first()).items.size() == 1);
    assert(data[1].kind == BrowserDataKind::History);
    const auto history = scanBrowserData(data[1]);
    assert(history.items.size() == 2);
    const QString fingerprint = browserDataFingerprint(history);
    auto differentMount = history;
    ++differentMount.rootMountId;
    assert(browserDataFingerprint(differentMount) != fingerprint);
    touch(config + "/chromium/Default/History-shm");
    assert(browserDataFingerprint(scanBrowserData(data[1])) != fingerprint);
    assert(data[2].paths.first() == "Network/Cookies");
    assert(data[3].paths.first() == "Cookies");
    assert(QFile::link(home + "/.mozilla/firefox/example.default/places.sqlite",
                       home + "/.mozilla/firefox/example.default/cookies.sqlite-wal"));
    assert(!scanBrowserData(data.first()).error.isEmpty());
    assert(QFile::remove(home + "/.mozilla/firefox/example.default/cookies.sqlite-wal"));
    QString error;
    const auto firefoxCookies = scanBrowserData(data.first());
    assert(firefoxCookies.error.isEmpty());
    for (const auto &item : firefoxCookies.items)
        assert(removeScannedFile(firefoxCookies, item, error));
    assert(!QFile::exists(home + "/.mozilla/firefox/example.default/cookies.sqlite"));
    assert(QFile::exists(home + "/.mozilla/firefox/example.default/places.sqlite"));
    touch(config + "/chromium/Default/Bookmarks");
    const auto chromiumHistory = scanBrowserData(data[1]);
    for (const auto &item : chromiumHistory.items)
        assert(removeScannedFile(chromiumHistory, item, error));
    assert(QFile::exists(config + "/chromium/Default/Bookmarks"));
    assert(QFile::exists(config + "/chromium/Default/Network/Cookies"));
    const struct { const char *name; BrowserKind kind; } names[] = {
        {"firefox", BrowserKind::Firefox}, {"chromium", BrowserKind::Chromium},
        {"chrome", BrowserKind::Chrome}, {"google-chrome", BrowserKind::Chrome},
    };
    for (const auto &browser : names) {
        int ready[2];
        assert(::pipe(ready) == 0);
        const pid_t child = ::fork();
        assert(child >= 0);
        if (child == 0) {
            ::close(ready[0]);
            if (::prctl(PR_SET_NAME, browser.name) != 0) ::_exit(1);
            const char signal = 'r';
            if (::write(ready[1], &signal, 1) != 1) ::_exit(1);
            ::pause();
            ::_exit(0);
        }
        ::close(ready[1]);
        char signal = 0;
        const bool started = ::read(ready[0], &signal, 1) == 1 && signal == 'r';
        ::close(ready[0]);
        const bool detected = started && browserRunning(browser.kind);
        ::kill(child, SIGTERM);
        ::waitpid(child, nullptr, 0);
        assert(detected);
    }
    return 0;
}
