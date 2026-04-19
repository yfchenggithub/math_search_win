#include "ui/pages/home_page.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

HomePage::HomePage(QWidget* parent) : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    auto* titleLabel = new QLabel(QStringLiteral("首页"), this);
    mainLayout->addWidget(titleLabel);

    auto* searchInput = new QLineEdit(this);
    searchInput->setPlaceholderText(QStringLiteral("输入关键词（例如：导数二级结论）"));
    mainLayout->addWidget(searchInput);

    auto* hotKeywordsBox = new QGroupBox(QStringLiteral("热门关键词"), this);
    auto* hotKeywordsLayout = new QVBoxLayout(hotKeywordsBox);
    auto* hotKeywordsList = new QListWidget(hotKeywordsBox);
    hotKeywordsList->addItems(
        {QStringLiteral("三角函数"), QStringLiteral("导数"), QStringLiteral("圆锥曲线"), QStringLiteral("数列")});
    hotKeywordsLayout->addWidget(hotKeywordsList);
    mainLayout->addWidget(hotKeywordsBox);

    auto* quickEntryBox = new QGroupBox(QStringLiteral("模块快捷入口"), this);
    auto* quickEntryLayout = new QHBoxLayout(quickEntryBox);
    quickEntryLayout->addWidget(new QPushButton(QStringLiteral("代数"), quickEntryBox));
    quickEntryLayout->addWidget(new QPushButton(QStringLiteral("几何"), quickEntryBox));
    quickEntryLayout->addWidget(new QPushButton(QStringLiteral("概率统计"), quickEntryBox));
    mainLayout->addWidget(quickEntryBox);

    auto* recentSummaryBox = new QGroupBox(QStringLiteral("最近搜索摘要"), this);
    auto* recentSummaryLayout = new QVBoxLayout(recentSummaryBox);
    auto* recentSummaryText = new QTextEdit(recentSummaryBox);
    recentSummaryText->setReadOnly(true);
    recentSummaryText->setPlainText(QStringLiteral("最近搜索摘要占位：\n- 暂无搜索记录\n- 下一轮接入真实最近搜索数据"));
    recentSummaryLayout->addWidget(recentSummaryText);
    mainLayout->addWidget(recentSummaryBox);

    mainLayout->addStretch();
}

