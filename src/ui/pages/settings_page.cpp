#include "ui/pages/settings_page.h"

#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    mainLayout->addWidget(new QLabel(QStringLiteral("设置/关于"), this));

    auto* softwareInfoBox = new QGroupBox(QStringLiteral("软件信息"), this);
    auto* softwareInfoLayout = new QVBoxLayout(softwareInfoBox);
    softwareInfoLayout->addWidget(new QLabel(QStringLiteral("名称：高中数学二级结论搜索系统"), softwareInfoBox));
    softwareInfoLayout->addWidget(new QLabel(QStringLiteral("版本：MVP v0.1"), softwareInfoBox));
    mainLayout->addWidget(softwareInfoBox);

    auto* dataInfoBox = new QGroupBox(QStringLiteral("数据信息"), this);
    auto* dataInfoLayout = new QVBoxLayout(dataInfoBox);
    dataInfoLayout->addWidget(new QLabel(QStringLiteral("本地数据状态：未加载"), dataInfoBox));
    dataInfoLayout->addWidget(new QLabel(QStringLiteral("数据目录：./data（占位）"), dataInfoBox));
    mainLayout->addWidget(dataInfoBox);

    auto* helpBox = new QGroupBox(QStringLiteral("使用帮助"), this);
    auto* helpLayout = new QVBoxLayout(helpBox);
    helpLayout->addWidget(new QLabel(QStringLiteral("帮助内容占位：后续补充使用说明与快捷键。"), helpBox));
    mainLayout->addWidget(helpBox);

    auto* contactBox = new QGroupBox(QStringLiteral("联系与反馈"), this);
    auto* contactLayout = new QVBoxLayout(contactBox);
    contactLayout->addWidget(new QLabel(QStringLiteral("反馈入口占位：support@example.com"), contactBox));
    mainLayout->addWidget(contactBox);

    mainLayout->addStretch();
}

