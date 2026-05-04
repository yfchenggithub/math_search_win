#include "infrastructure/data/conclusion_pdf_map_loader.h"

#include <QtTest/QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

namespace {

class ScopedSandboxRoot final {
public:
    ScopedSandboxRoot()
    {
        const QString baseDir = QDir::current().filePath(QStringLiteral(".tmp_conclusion_pdf_map_loader_tests"));
        if (!QDir().mkpath(baseDir)) {
            return;
        }

        rootPath_ = QDir(baseDir).filePath(
            QStringLiteral("sandbox_%1_%2")
                .arg(QDateTime::currentMSecsSinceEpoch())
                .arg(QRandomGenerator::global()->bounded(1000000)));
        if (!QDir().mkpath(rootPath_)) {
            rootPath_.clear();
        }
    }

    ~ScopedSandboxRoot()
    {
        if (!rootPath_.isEmpty()) {
            QDir(rootPath_).removeRecursively();
        }
    }

    bool isValid() const
    {
        return !rootPath_.isEmpty() && QDir(rootPath_).exists();
    }

    QString path(const QString& relativePath) const
    {
        return QDir(rootPath_).filePath(relativePath);
    }

private:
    QString rootPath_;
};

bool writeJsonFile(const QString& filePath, const QJsonObject& object)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Indented);
    const qint64 written = file.write(payload);
    file.close();
    return written == payload.size();
}

}  // namespace

class ConclusionPdfMapLoaderTest final : public QObject {
    Q_OBJECT

private slots:
    void loadFromFile_missingFile_returnsFalse();
    void loadFromFile_flatObject_loadsEntries();
};

void ConclusionPdfMapLoaderTest::loadFromFile_missingFile_returnsFalse()
{
    infrastructure::data::ConclusionPdfMapLoader loader;
    QVERIFY(!loader.loadFromFile(QStringLiteral("Z:/not_exists/conclusion_pdf_map.json")));
    QVERIFY(loader.diagnostics().fatalError.contains(QStringLiteral("missing")));
}

void ConclusionPdfMapLoaderTest::loadFromFile_flatObject_loadsEntries()
{
    ScopedSandboxRoot sandbox;
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be available");

    const QString mapPath = sandbox.path(QStringLiteral("conclusion_pdf_map.json"));
    QJsonObject mapRoot;
    mapRoot.insert(QStringLiteral("I028"), QStringLiteral("I028.pdf"));
    mapRoot.insert(QStringLiteral(" C001 "), QStringLiteral("C001_Circle.pdf"));
    mapRoot.insert(QStringLiteral("bad"), QJsonObject());
    QVERIFY(writeJsonFile(mapPath, mapRoot));

    infrastructure::data::ConclusionPdfMapLoader loader;
    QVERIFY(loader.loadFromFile(mapPath));
    QCOMPARE(loader.mappedPdfFileName(QStringLiteral("I028")), QStringLiteral("I028.pdf"));
    QCOMPARE(loader.mappedPdfFileName(QStringLiteral("C001")), QStringLiteral("C001_Circle.pdf"));
    QVERIFY(loader.contains(QStringLiteral("I028")));
    QVERIFY(!loader.contains(QStringLiteral("M404")));
}

QTEST_APPLESS_MAIN(ConclusionPdfMapLoaderTest)

#include "test_conclusion_pdf_map_loader.moc"
