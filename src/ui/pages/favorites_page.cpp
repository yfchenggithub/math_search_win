#include "ui/pages/favorites_page.h"

#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

FavoritesPage::FavoritesPage(QWidget* parent) : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    mainLayout->addWidget(new QLabel(QStringLiteral("收藏"), this));

    auto* searchInput = new QLineEdit(this);
    searchInput->setPlaceholderText(QStringLiteral("收藏搜索框占位"));
    mainLayout->addWidget(searchInput);

    auto* favoritesList = new QListWidget(this);
    favoritesList->addItems({QStringLiteral("收藏条目占位 1"), QStringLiteral("收藏条目占位 2")});
    mainLayout->addWidget(favoritesList, 1);

    auto* emptyStateFrame = new QFrame(this);
    emptyStateFrame->setFrameShape(QFrame::StyledPanel);
    auto* emptyStateLayout = new QVBoxLayout(emptyStateFrame);
    emptyStateLayout->addWidget(new QLabel(QStringLiteral("空状态占位：暂无收藏内容"), emptyStateFrame));
    mainLayout->addWidget(emptyStateFrame);
}

