#pragma once

#include <QString>

namespace infrastructure::data {
class ConclusionIndexRepository;
}

namespace tests::shared {

QString projectSourceDir();
QString testsSourceDir();
QString fixtureIndexPath();
QString fixtureIndexRound2Path();
QString malformedFixtureIndexPath();
QString realIndexPath();
QString summarizeDiagnostics(const infrastructure::data::ConclusionIndexRepository& repository);

bool loadRepositoryFromFile(const QString& filePath,
                            infrastructure::data::ConclusionIndexRepository* repository,
                            QString* errorSummary = nullptr);

}  // namespace tests::shared
