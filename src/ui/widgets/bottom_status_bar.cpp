#include "ui/widgets/bottom_status_bar.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"
#include "shared/constants.h"

#include <QHBoxLayout>
#include <QLabel>

BottomStatusBar::BottomStatusBar(QWidget* parent) : QWidget(parent)
{
    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("BottomStatusBar constructor begin"));

    setFixedHeight(UiConstants::kBottomBarHeight);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(12);

    modeLabel_ = new QLabel(UiConstants::kStatusOffline, this);
    dataStatusLabel_ = new QLabel(UiConstants::kStatusData, this);
    versionStatusLabel_ = new QLabel(UiConstants::kStatusVersion, this);

    layout->addWidget(modeLabel_);
    layout->addWidget(dataStatusLabel_);
    layout->addWidget(versionStatusLabel_);
    layout->addStretch();

    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("BottomStatusBar constructor complete"));
}

void BottomStatusBar::setDataStatusText(const QString& text)
{
    if (dataStatusLabel_ == nullptr) {
        return;
    }
    dataStatusLabel_->setText(text.trimmed().isEmpty() ? UiConstants::kStatusData : text.trimmed());
}

void BottomStatusBar::setVersionStatusText(const QString& text)
{
    if (versionStatusLabel_ == nullptr) {
        return;
    }
    versionStatusLabel_->setText(text.trimmed().isEmpty() ? UiConstants::kStatusVersion : text.trimmed());
}
