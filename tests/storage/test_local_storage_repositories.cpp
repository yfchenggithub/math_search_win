#include "core/logging/logger.h"
#include "domain/models/app_settings.h"
#include "domain/repositories/favorites_repository.h"
#include "domain/repositories/history_repository.h"
#include "domain/repositories/settings_repository.h"
#include "infrastructure/storage/local_storage_service.h"

#include <QtTest/QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>

namespace {

bool writeUtf8File(const QString& path, const QByteArray& payload)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    return file.write(payload) == payload.size();
}

class SandboxDir final {
public:
    explicit SandboxDir(const QString& name)
    {
        const QString base = QDir::current().filePath(QStringLiteral(".tmp_storage"));
        QDir().mkpath(base);

        const QString token = QStringLiteral("%1_%2")
                                  .arg(QDateTime::currentMSecsSinceEpoch())
                                  .arg(QRandomGenerator::global()->bounded(1000000));
        path_ = QDir(base).filePath(QStringLiteral("%1_%2").arg(name, token));
        QDir().mkpath(path_);
    }

    ~SandboxDir()
    {
        if (!path_.isEmpty()) {
            QDir(path_).removeRecursively();
        }
    }

    bool isValid() const
    {
        return !path_.isEmpty() && QDir(path_).exists();
    }

    QString path() const
    {
        return path_;
    }

private:
    QString path_;
};

}  // namespace

class LocalStorageRepositoriesTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanupTestCase();

    void localStorage_createsMissingCacheDirectory();
    void localStorage_roundTripAndCorruptReadFallback();
    void localStorage_writeFailureReturnsFalse();

    void favorites_missingFile_loadCreatesDefaultJson();
    void favorites_addToggleRemove_persistsAcrossReload();
    void favorites_emptyOrCorruptFile_fallbackToEmpty();

    void history_missingFile_loadCreatesDefaultJson();
    void history_addDedupLimit_persistsAcrossReload();
    void history_corruptFile_fallbackToEmpty();

    void settings_missingFile_loadCreatesDefaultJson();
    void settings_versionMissingOrFieldsMissing_mergeDefaults();
    void settings_corruptFile_fallbackToDefaults();
};

void LocalStorageRepositoriesTest::cleanupTestCase()
{
    logging::Logger::instance().shutdown();
}

void LocalStorageRepositoriesTest::localStorage_createsMissingCacheDirectory()
{
    SandboxDir sandbox(QStringLiteral("local_storage_create_cache"));
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be valid");

    const QString cachePath = QDir(sandbox.path()).filePath(QStringLiteral("cache"));
    QVERIFY(!QDir(cachePath).exists());

    infrastructure::storage::LocalStorageService storage(cachePath);
    QVERIFY(storage.ensureCacheDirExists());
    QVERIFY(QDir(cachePath).exists());
}

void LocalStorageRepositoriesTest::localStorage_roundTripAndCorruptReadFallback()
{
    SandboxDir sandbox(QStringLiteral("local_storage_roundtrip"));
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be valid");

    infrastructure::storage::LocalStorageService storage(sandbox.path());
    QVERIFY(storage.ensureCacheDirExists());

    const QJsonObject object = {
        {QStringLiteral("version"), 1},
        {QStringLiteral("ids"), QJsonArray({QStringLiteral("I001"), QStringLiteral("I002")})},
    };
    QVERIFY(storage.writeJsonFileAtomically(storage.favoritesFilePath(), QJsonDocument(object)));

    bool ok = false;
    QJsonDocument doc = storage.readJsonFile(storage.favoritesFilePath(), &ok);
    QVERIFY(ok);
    QCOMPARE(doc.object().value(QStringLiteral("version")).toInt(-1), 1);

    QVERIFY(writeUtf8File(storage.favoritesFilePath(), QByteArray("{broken-json")));
    doc = storage.readJsonFile(storage.favoritesFilePath(), &ok);
    QVERIFY(!ok);
    QVERIFY(doc.isNull());
}

void LocalStorageRepositoriesTest::localStorage_writeFailureReturnsFalse()
{
    SandboxDir sandbox(QStringLiteral("local_storage_write_fail"));
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be valid");

    const QString blockedPath = QDir(sandbox.path()).filePath(QStringLiteral("cache_is_file"));
    QVERIFY(writeUtf8File(blockedPath, QByteArray("x")));
    QVERIFY(QFileInfo(blockedPath).isFile());

    infrastructure::storage::LocalStorageService storage(blockedPath);
    QVERIFY(!storage.ensureCacheDirExists());

    const QJsonObject object = {{QStringLiteral("version"), 1}};
    QVERIFY(!storage.writeJsonFileAtomically(storage.favoritesFilePath(), QJsonDocument(object)));
}

void LocalStorageRepositoriesTest::favorites_missingFile_loadCreatesDefaultJson()
{
    SandboxDir sandbox(QStringLiteral("favorites_missing"));
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be valid");

    infrastructure::storage::LocalStorageService storage(sandbox.path());
    domain::repositories::FavoritesRepository favorites(&storage, false);

    const QString path = storage.favoritesFilePath();
    QVERIFY(!QFileInfo::exists(path));

    QVERIFY(favorites.load());
    QCOMPARE(favorites.count(), 0);
    QVERIFY(QFileInfo::exists(path));

    bool ok = false;
    const QJsonDocument doc = storage.readJsonFile(path, &ok);
    QVERIFY(ok);
    QCOMPARE(doc.object().value(QStringLiteral("version")).toInt(-1), 1);
    QVERIFY(doc.object().value(QStringLiteral("ids")).toArray().isEmpty());
}

void LocalStorageRepositoriesTest::favorites_addToggleRemove_persistsAcrossReload()
{
    SandboxDir sandbox(QStringLiteral("favorites_persist"));
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be valid");

    infrastructure::storage::LocalStorageService storage(sandbox.path());
    domain::repositories::FavoritesRepository writer(&storage, true);

    QVERIFY(writer.load());
    writer.add(QStringLiteral(" I001 "));
    writer.add(QStringLiteral("I002"));
    writer.add(QStringLiteral("I001"));
    QCOMPARE(writer.count(), 2);
    QVERIFY(writer.contains(QStringLiteral("I001")));

    writer.toggle(QStringLiteral("I001"));
    QVERIFY(!writer.contains(QStringLiteral("I001")));
    writer.toggle(QStringLiteral("I003"));
    writer.remove(QStringLiteral("I002"));
    QCOMPARE(writer.allIds(), QStringList({QStringLiteral("I003")}));

    domain::repositories::FavoritesRepository reader(&storage, false);
    QVERIFY(reader.load());
    QCOMPARE(reader.allIds(), QStringList({QStringLiteral("I003")}));
}

void LocalStorageRepositoriesTest::favorites_emptyOrCorruptFile_fallbackToEmpty()
{
    SandboxDir sandbox(QStringLiteral("favorites_corrupt"));
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be valid");

    infrastructure::storage::LocalStorageService storage(sandbox.path());
    QVERIFY(storage.ensureCacheDirExists());
    const QString path = storage.favoritesFilePath();

    QVERIFY(writeUtf8File(path, QByteArray()));
    domain::repositories::FavoritesRepository fromEmpty(&storage, false);
    QVERIFY(!fromEmpty.load());
    QCOMPARE(fromEmpty.count(), 0);

    bool ok = false;
    QJsonDocument doc = storage.readJsonFile(path, &ok);
    QVERIFY(ok);
    QVERIFY(doc.isObject());

    QVERIFY(writeUtf8File(path, QByteArray("{bad_json")));
    domain::repositories::FavoritesRepository fromCorrupt(&storage, false);
    QVERIFY(!fromCorrupt.load());
    QCOMPARE(fromCorrupt.count(), 0);
    doc = storage.readJsonFile(path, &ok);
    QVERIFY(ok);
    QVERIFY(doc.isObject());
}

void LocalStorageRepositoriesTest::history_missingFile_loadCreatesDefaultJson()
{
    SandboxDir sandbox(QStringLiteral("history_missing"));
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be valid");

    infrastructure::storage::LocalStorageService storage(sandbox.path());
    domain::repositories::HistoryRepository history(&storage, 50, false);

    const QString path = storage.historyFilePath();
    QVERIFY(!QFileInfo::exists(path));

    QVERIFY(history.load());
    QCOMPARE(history.count(), 0);
    QVERIFY(QFileInfo::exists(path));
}

void LocalStorageRepositoriesTest::history_addDedupLimit_persistsAcrossReload()
{
    SandboxDir sandbox(QStringLiteral("history_persist"));
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be valid");

    infrastructure::storage::LocalStorageService storage(sandbox.path());
    domain::repositories::HistoryRepository writer(&storage, 3, true);
    QVERIFY(writer.load());

    writer.addQuery(QStringLiteral("恒成立"));
    writer.addQuery(QStringLiteral("极值"), QStringLiteral("manual"));
    writer.addQuery(QStringLiteral("恒成立"), QStringLiteral("manual"));
    writer.addQuery(QStringLiteral("导数"));
    writer.addQuery(QStringLiteral("数列"));

    QCOMPARE(writer.count(), 3);
    const auto currentItems = writer.recentItems(10);
    QCOMPARE(currentItems.size(), 3);
    QCOMPARE(currentItems.at(0).query, QStringLiteral("数列"));
    QCOMPARE(currentItems.at(1).query, QStringLiteral("导数"));
    QCOMPARE(currentItems.at(2).query, QStringLiteral("恒成立"));

    domain::repositories::HistoryRepository reader(&storage, 3, false);
    QVERIFY(reader.load());
    const auto reloaded = reader.recentItems(10);
    QCOMPARE(reloaded.size(), 3);
    QCOMPARE(reloaded.at(0).query, QStringLiteral("数列"));
    QCOMPARE(reloaded.at(1).query, QStringLiteral("导数"));
    QCOMPARE(reloaded.at(2).query, QStringLiteral("恒成立"));
    QCOMPARE(reader.recentItems(2).size(), 2);
}

void LocalStorageRepositoriesTest::history_corruptFile_fallbackToEmpty()
{
    SandboxDir sandbox(QStringLiteral("history_corrupt"));
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be valid");

    infrastructure::storage::LocalStorageService storage(sandbox.path());
    QVERIFY(storage.ensureCacheDirExists());
    const QString path = storage.historyFilePath();
    QVERIFY(writeUtf8File(path, QByteArray("{oops")));

    domain::repositories::HistoryRepository history(&storage, 50, false);
    QVERIFY(!history.load());
    QCOMPARE(history.count(), 0);

    bool ok = false;
    const QJsonDocument doc = storage.readJsonFile(path, &ok);
    QVERIFY(ok);
    QVERIFY(doc.isObject());
}

void LocalStorageRepositoriesTest::settings_missingFile_loadCreatesDefaultJson()
{
    SandboxDir sandbox(QStringLiteral("settings_missing"));
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be valid");

    infrastructure::storage::LocalStorageService storage(sandbox.path());
    domain::repositories::SettingsRepository settings(&storage, false);

    QVERIFY(settings.load());
    QCOMPARE(settings.value(QStringLiteral("window_width")).toInt(), 1280);
    QCOMPARE(settings.value(QStringLiteral("window_height")).toInt(), 800);
    QCOMPARE(settings.value(QStringLiteral("last_sort_mode")).toString(), QStringLiteral("relevance"));
    QVERIFY(QFileInfo::exists(storage.settingsFilePath()));
}

void LocalStorageRepositoriesTest::settings_versionMissingOrFieldsMissing_mergeDefaults()
{
    SandboxDir sandbox(QStringLiteral("settings_merge_defaults"));
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be valid");

    infrastructure::storage::LocalStorageService storage(sandbox.path());
    QVERIFY(storage.ensureCacheDirExists());

    QJsonObject legacyRoot;
    legacyRoot.insert(QStringLiteral("window_width"), 1440);
    legacyRoot.insert(QStringLiteral("last_query"), QStringLiteral("恒成立"));
    QVERIFY(storage.writeJsonFileAtomically(storage.settingsFilePath(), QJsonDocument(legacyRoot)));

    domain::repositories::SettingsRepository settings(&storage, false);
    QVERIFY(settings.load());
    QCOMPARE(settings.value(QStringLiteral("window_width")).toInt(), 1440);
    QCOMPARE(settings.value(QStringLiteral("window_height")).toInt(), 800);
    QCOMPARE(settings.value(QStringLiteral("last_query")).toString(), QStringLiteral("恒成立"));
    QCOMPARE(settings.value(QStringLiteral("last_sort_mode")).toString(), QStringLiteral("relevance"));

    settings.setValue(QStringLiteral("theme"), QStringLiteral("dark"));
    QVERIFY(settings.contains(QStringLiteral("theme")));
    settings.remove(QStringLiteral("theme"));
    QVERIFY(!settings.contains(QStringLiteral("theme")));
    settings.setValue(QStringLiteral("theme"), QStringLiteral("dark"));
    QVERIFY(settings.save());

    domain::repositories::SettingsRepository reloaded(&storage, false);
    QVERIFY(reloaded.load());
    QCOMPARE(reloaded.value(QStringLiteral("theme")).toString(), QStringLiteral("dark"));

    reloaded.resetToDefaults();
    QCOMPARE(reloaded.value(QStringLiteral("theme")).toString(),
             domain::models::AppSettings::defaultValues().value(QStringLiteral("theme")).toString());
}

void LocalStorageRepositoriesTest::settings_corruptFile_fallbackToDefaults()
{
    SandboxDir sandbox(QStringLiteral("settings_corrupt"));
    QVERIFY2(sandbox.isValid(), "temporary sandbox should be valid");

    infrastructure::storage::LocalStorageService storage(sandbox.path());
    QVERIFY(storage.ensureCacheDirExists());
    QVERIFY(writeUtf8File(storage.settingsFilePath(), QByteArray("{bad")));

    domain::repositories::SettingsRepository settings(&storage, false);
    QVERIFY(!settings.load());
    QCOMPARE(settings.value(QStringLiteral("window_width")).toInt(), 1280);
    QCOMPARE(settings.value(QStringLiteral("last_module_filter")).toString(), QStringLiteral("all"));

    bool ok = false;
    const QJsonDocument doc = storage.readJsonFile(storage.settingsFilePath(), &ok);
    QVERIFY(ok);
    QVERIFY(doc.isObject());
}

QTEST_APPLESS_MAIN(LocalStorageRepositoriesTest)

#include "test_local_storage_repositories.moc"
