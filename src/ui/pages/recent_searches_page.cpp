#include "ui/pages/recent_searches_page.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

RecentSearchesPage::RecentSearchesPage(QWidget* parent) : QWidget(parent)
{
    LOG_DEBUG(LogCategory::UiMainWindow,
              QStringLiteral("page constructed name=recent_searches mode=placeholder"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->addWidget(new QLabel(QStringLiteral("最近搜索"), this));
    headerLayout->addStretch();
    headerLayout->addWidget(new QPushButton(QStringLiteral("清空（占位）"), this));
    mainLayout->addLayout(headerLayout);

    auto* historyList = new QListWidget(this);
    historyList->addItems(
        {QStringLiteral("最近搜索项占位 1"), QStringLiteral("最近搜索项占位 2"), QStringLiteral("最近搜索项占位 3")});
    mainLayout->addWidget(historyList, 1);

    mainLayout->addWidget(new QLabel(QStringLiteral("说明：本轮不做历史持久化，仅静态占位。"), this));
}
