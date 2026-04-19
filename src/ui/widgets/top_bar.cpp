#include "ui/widgets/top_bar.h"

#include "shared/constants.h"

#include <QLabel>
#include <QVBoxLayout>

TopBar::TopBar(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(UiConstants::kTopBarHeight);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 10, 16, 10);
    layout->setSpacing(2);

    titleLabel_ = new QLabel(QStringLiteral("首页"), this);
    subtitleLabel_ = new QLabel(UiConstants::kDefaultTopSubtitle, this);

    layout->addWidget(titleLabel_);
    layout->addWidget(subtitleLabel_);
}

void TopBar::setPageTitle(const QString& title, const QString& subtitle)
{
    titleLabel_->setText(title);
    subtitleLabel_->setText(subtitle.isEmpty() ? UiConstants::kDefaultTopSubtitle : subtitle);
}

