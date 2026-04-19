#pragma once

#include "domain/models/search_result_models.h"

#include <QString>
#include <QWidget>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTextBrowser;
class QWebEngineView;

namespace domain::services {
class SearchService;
class SuggestService;
}

namespace infrastructure::data {
class ConclusionContentRepository;
class ConclusionIndexRepository;
}

namespace domain::adapters {
struct ConclusionDetailViewData;
}

namespace ui::detail {
class DetailHtmlRenderer;
struct DetailHtmlRenderResult;
}

class SearchPage : public QWidget {
    Q_OBJECT

public:
    explicit SearchPage(domain::services::SearchService* searchService,
                        domain::services::SuggestService* suggestService,
                        const infrastructure::data::ConclusionContentRepository* contentRepository,
                        const infrastructure::data::ConclusionIndexRepository* indexRepository,
                        QWidget* parent = nullptr);

    void setBackendStatus(bool indexReady, bool contentReady);
    void setInitialQuery(const QString& query);
    void setInitialModule(const QString& module);
    void triggerSearchFromRecent(const QString& query, const QString& module = QString());

private slots:
    void onQueryTextChanged(const QString& text);
    void onQueryReturnPressed();
    void onSearchButtonClicked();
    void onSuggestionClicked(QListWidgetItem* item);
    void onResultSelectionChanged();
    void onFilterChanged();
    void onSortChanged();
    void onClearFiltersClicked();

private:
    enum class SortMode {
        ScoreDesc = 0,
        TitleAsc,
        DifficultyAsc,
        DifficultyDesc,
    };

    void buildUi();
    void connectSignals();
    void rebuildFilterOptions();
    void resetToEmptyState();
    void updateStatusLine(const QString& status, const QString& summary = QString());

    void runSuggest(const QString& query);
    void runSearch(const QString& query, const QString& triggerSource);
    void clearSuggestions();
    void renderResults(const QVector<domain::models::SearchHit>& hits);
    void renderDetailForDocId(const QString& docId);
    void renderDetailInFallbackBrowser(const domain::adapters::ConclusionDetailViewData& detailView);
    void showDetailPlaceholder(const QString& message);
    void showDetailError(const QString& message);
    bool loadDetailHtmlIntoWebView(const ui::detail::DetailHtmlRenderResult& renderResult);
    void activateTextFallbackMode(const QString& reason);
    void applySort(QVector<domain::models::SearchHit>* hits) const;
    SortMode currentSortMode() const;

    QString selectedModuleFilter() const;
    QString selectedCategoryFilter() const;
    QString selectedTagFilter() const;
    QString filtersSignature() const;
    QString buildSuggestSignature(const QString& query) const;
    QString buildSearchSignature(const QString& query) const;

    QStringList collectCategoryOptions() const;
    static QStringList uniqueSortedCaseInsensitive(const QStringList& values);
    static QString joinNonEmpty(const QStringList& values, const QString& separator);

private:
    domain::services::SearchService* searchService_ = nullptr;
    domain::services::SuggestService* suggestService_ = nullptr;
    const infrastructure::data::ConclusionContentRepository* contentRepository_ = nullptr;
    const infrastructure::data::ConclusionIndexRepository* indexRepository_ = nullptr;

    bool indexReady_ = false;
    bool contentReady_ = false;
    bool suppressSuggestRefresh_ = false;
    bool webDetailEnabled_ = false;

    QString lastSuggestSignature_;
    QString lastSearchSignature_;
    QVector<domain::models::SearchHit> currentHits_;

    QLineEdit* queryInput_ = nullptr;
    QPushButton* searchButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QListWidget* suggestionList_ = nullptr;
    QComboBox* moduleFilterCombo_ = nullptr;
    QComboBox* categoryFilterCombo_ = nullptr;
    QComboBox* tagFilterCombo_ = nullptr;
    QComboBox* sortCombo_ = nullptr;
    QPushButton* clearFiltersButton_ = nullptr;
    QListWidget* resultList_ = nullptr;
    QWebEngineView* detailWebView_ = nullptr;
    QTextBrowser* detailBrowser_ = nullptr;
    std::unique_ptr<ui::detail::DetailHtmlRenderer> detailHtmlRenderer_;
};
