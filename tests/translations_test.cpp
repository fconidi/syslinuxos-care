#include <QCoreApplication>
#include <QTranslator>

#include <cassert>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    assert(argc == 5);
    const char *expected[] = {"Panoramica", "Vue d’ensemble", "Übersicht", "Vista general"};
    for (int index = 1; index < argc; ++index) {
        QTranslator translator;
        assert(translator.load(QString::fromLocal8Bit(argv[index])));
        assert(translator.translate("QObject", "Overview") == QString::fromUtf8(expected[index - 1]));
    }
    return 0;
}
