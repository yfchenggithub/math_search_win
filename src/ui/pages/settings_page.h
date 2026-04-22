#pragma once

#include "infrastructure/data/conclusion_content_repository.h"
#include "infrastructure/data/conclusion_index_repository.h"

#include <QWidget>

namespace license {
class LicenseService;
}

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(const infrastructure::data::ConclusionIndexRepository* indexRepository,
                          const infrastructure::data::ConclusionContentRepository* contentRepository,
                          const license::LicenseService* licenseService,
                          bool indexLoaded,
                          bool contentLoaded,
                          QWidget* parent = nullptr);

    void reloadData();

private:
    void setupUi();
    void setupHeader();
    void setupSections();

    QWidget* buildSoftwareInfoSection();
    QWidget* buildLicenseInfoSection();
    QWidget* buildDataInfoSection();
    QWidget* buildHelpSection();
    QWidget* buildFeedbackSection();
    QWidget* buildExpansionHintSection();

    QWidget* createSection(const QString& title, const QString& summary, const QString& sectionRole);
    QWidget* createInfoRow(const QString& label, QLabel** valueLabel, bool wrapValue = false);

    QString buildDataStatusText() const;
    QString buildDataHintText() const;
    QString resolvedPath(const QString& candidate, const QString& fallback) const;

private:
    const infrastructure::data::ConclusionIndexRepository* indexRepository_ = nullptr;
    const infrastructure::data::ConclusionContentRepository* contentRepository_ = nullptr;
    const license::LicenseService* licenseService_ = nullptr;
    bool indexLoaded_ = false;
    bool contentLoaded_ = false;

    QVBoxLayout* rootLayout_ = nullptr;
    QWidget* headerWidget_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* subtitleLabel_ = nullptr;
    QLabel* headerMetaLabel_ = nullptr;

    QScrollArea* scrollArea_ = nullptr;
    QWidget* contentWidget_ = nullptr;
    QVBoxLayout* contentLayout_ = nullptr;

    QWidget* softwareInfoGroup_ = nullptr;
    QWidget* licenseInfoGroup_ = nullptr;
    QWidget* dataInfoGroup_ = nullptr;
    QWidget* helpGroup_ = nullptr;
    QWidget* feedbackGroup_ = nullptr;
    QWidget* expansionHintGroup_ = nullptr;

    QLabel* appNameValueLabel_ = nullptr;
    QLabel* versionValueLabel_ = nullptr;
    QLabel* buildValueLabel_ = nullptr;
    QLabel* modeValueLabel_ = nullptr;

    QLabel* licenseStatusValueLabel_ = nullptr;
    QLabel* licenseFileStatusValueLabel_ = nullptr;
    QLabel* licenseSerialValueLabel_ = nullptr;
    QLabel* licenseWatermarkValueLabel_ = nullptr;
    QLabel* deviceFingerprintValueLabel_ = nullptr;
    QLabel* licensePathValueLabel_ = nullptr;
    QLabel* licenseHintLabel_ = nullptr;

    QLabel* dataStatusValueLabel_ = nullptr;
    QLabel* dataDirValueLabel_ = nullptr;
    QLabel* logDirValueLabel_ = nullptr;
    QLabel* contentStatsValueLabel_ = nullptr;
    QLabel* indexStatsValueLabel_ = nullptr;
    QLabel* moduleStatsValueLabel_ = nullptr;
    QLabel* dataSourceValueLabel_ = nullptr;
    QLabel* dataHintLabel_ = nullptr;

    QLabel* helpTextLabel_ = nullptr;
    QLabel* shortcutHintLabel_ = nullptr;
    QLabel* helpDocLabel_ = nullptr;

    QLabel* feedbackEmailLabel_ = nullptr;
    QLabel* feedbackGuideLabel_ = nullptr;
    QLabel* feedbackIssueLabel_ = nullptr;

    QLabel* expansionHintLabel_ = nullptr;

    QPushButton* openDataDirButton_ = nullptr;
    QPushButton* openLogDirButton_ = nullptr;
    QPushButton* openReadmeButton_ = nullptr;
    QPushButton* copyFeedbackEmailButton_ = nullptr;
};
