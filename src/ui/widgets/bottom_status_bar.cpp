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

    layout->addWidget(new QLabel(UiConstants::kStatusOffline, this));
    layout->addWidget(new QLabel(UiConstants::kStatusData, this));
    layout->addWidget(new QLabel(UiConstants::kStatusVersion, this));
    layout->addStretch();

    LOG_DEBUG(LogCategory::UiMainWindow, QStringLiteral("BottomStatusBar constructor complete"));
}
