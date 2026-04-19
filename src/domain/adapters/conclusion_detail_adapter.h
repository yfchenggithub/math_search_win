#pragma once

#include "domain/models/conclusion_record.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace domain::adapters {

struct DetailBlockViewData {
    QString type;
    models::RenderBlockKnownType knownType = models::RenderBlockKnownType::Unknown;
    const models::RenderBlock* sourceBlock = nullptr;
};

struct DetailSectionViewData {
    QString key;
    QString title;
    QString blockType;
    QVector<DetailBlockViewData> blocks;
    const models::RenderSection* sourceSection = nullptr;
};

struct ConclusionDetailViewData {
    QString id;
    QString title;
    QString module;
    QString knowledgeNode;
    QStringList altNodes;
    int difficulty = 0;
    QString category;
    QStringList tags;
    QStringList aliases;
    QString summary;
    QString primaryFormula;

    // Non-owning pointers to large fields; valid while source ConclusionRecord remains alive.
    const QVector<models::VariableDef>* variables = nullptr;
    const QVector<models::ConditionDef>* conditions = nullptr;
    const QVector<models::ConclusionDef>* conclusions = nullptr;
    const QVector<models::RenderSection>* rawSections = nullptr;

    QVector<DetailSectionViewData> sections;

    QString plainStatement;
    QString plainExplanation;
    QString plainProof;
    QString plainExamples;
    QString plainTraps;
    QString plainSummary;

    const models::AssetRefs* assets = nullptr;
    const models::RelationsInfo* relations = nullptr;
    const models::ShareInfo* shareInfo = nullptr;
    const models::ExamInfo* examInfo = nullptr;

    bool isPro = false;
};

class ConclusionDetailAdapter final {
public:
    static ConclusionDetailViewData toViewData(const models::ConclusionRecord& record);
};

}  // namespace domain::adapters

