#include "ui/pages/search_page.h"

#include "core/logging/log_categories.h"
#include "core/logging/logger.h"

#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSplitter>
#include <QTextEdit>
#include <QVBoxLayout>

SearchPage::SearchPage(QWidget* parent) : QWidget(parent)
{
    LOG_INFO(LogCategory::SearchEngine, QStringLiteral("SearchPage constructed"));
    LOG_INFO(LogCategory::SearchIndex, QStringLiteral("search index loading is not connected yet (MVP static shell)"));
    LOG_INFO(LogCategory::DetailRender, QStringLiteral("detail render is placeholder (WebEngine/KaTeX not connected yet)"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    mainLayout->addWidget(new QLabel(QStringLiteral("搜索"), this));

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    auto* leftPanel = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);

    auto* searchInput = new QLineEdit(leftPanel);
    searchInput->setPlaceholderText(QStringLiteral("输入结论关键词或标签"));
    leftLayout->addWidget(searchInput);

    auto* moduleFilterBox = new QGroupBox(QStringLiteral("模块筛选（占位）"), leftPanel);
    auto* moduleFilterLayout = new QVBoxLayout(moduleFilterBox);
    auto* moduleFilterList = new QListWidget(moduleFilterBox);
    moduleFilterList->addItems({QStringLiteral("函数"), QStringLiteral("三角"), QStringLiteral("立体几何")});
    moduleFilterLayout->addWidget(moduleFilterList);
    leftLayout->addWidget(moduleFilterBox);

    auto* tagFilterBox = new QGroupBox(QStringLiteral("标签筛选（占位）"), leftPanel);
    auto* tagFilterLayout = new QVBoxLayout(tagFilterBox);
    auto* tagFilterList = new QListWidget(tagFilterBox);
    tagFilterList->addItems({QStringLiteral("高频"), QStringLiteral("易错"), QStringLiteral("压轴")});
    tagFilterLayout->addWidget(tagFilterList);
    leftLayout->addWidget(tagFilterBox);

    auto* resultListBox = new QGroupBox(QStringLiteral("结果列表（占位）"), leftPanel);
    auto* resultListLayout = new QVBoxLayout(resultListBox);
    auto* resultList = new QListWidget(resultListBox);
    resultList->addItems({QStringLiteral("结果 1"), QStringLiteral("结果 2"), QStringLiteral("结果 3")});
    resultListLayout->addWidget(resultList);
    leftLayout->addWidget(resultListBox, 1);

    auto* rightPanel = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);

    rightLayout->addWidget(new QLabel(QStringLiteral("详情区"), rightPanel));

    auto* detailFrame = new QFrame(rightPanel);
    detailFrame->setFrameShape(QFrame::StyledPanel);
    auto* detailLayout = new QVBoxLayout(detailFrame);
    detailLayout->setContentsMargins(12, 12, 12, 12);
    detailLayout->setSpacing(8);
    detailLayout->addWidget(new QLabel(QStringLiteral("详情区占位容器"), detailFrame));
    // 后续这里接 DetailRenderWidget / QWebEngineView。
    auto* detailPlaceholder = new QTextEdit(detailFrame);
    detailPlaceholder->setReadOnly(true);
    detailPlaceholder->setPlainText(QStringLiteral("本轮仅静态占位，不接入 WebEngine，不渲染公式。"));
    detailLayout->addWidget(detailPlaceholder);
    rightLayout->addWidget(detailFrame, 1);

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({500, 700});

    mainLayout->addWidget(splitter, 1);
}
