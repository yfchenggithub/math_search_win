#pragma once

#include "domain/models/render_section.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace domain::models {

struct ContentFragment {
    QString type;
    QString text;
    QString latex;
    QJsonObject extra;
};

struct VariableDef {
    QString name;
    QString latex;
    QString description;
    bool required = false;
    QJsonObject extra;
};

struct ConditionDef {
    QString id;
    QString title;
    QVector<ContentFragment> content;
    bool required = false;
    QString scope;
    QJsonObject extra;
};

struct ConclusionDef {
    QString id;
    QString title;
    QVector<ContentFragment> content;
    QJsonObject extra;
};

struct PlainContent {
    QString statement;
    QString explanation;
    QString proof;
    QString examples;
    QString traps;
    QString summary;
    QJsonObject extra;

    bool isEmpty() const;
};

struct AssetRefs {
    QString cover;
    QString svg;
    QString png;
    QString pdf;
    QString mp4;
    QJsonObject extra;
};

struct ShareInfo {
    QString title;
    QString desc;
    QJsonObject extra;
};

struct RelationsInfo {
    QStringList prerequisites;
    QStringList relatedIds;
    QString similar;
    QJsonObject extra;
};

struct ExamInfo {
    double frequency = 0.0;
    double score = 0.0;
    bool hasFrequency = false;
    bool hasScore = false;
    QJsonObject extra;
};

struct ConclusionExt {
    ShareInfo share;
    RelationsInfo relations;
    ExamInfo exam;
    QJsonObject extra;
};

struct ConclusionIdentity {
    QString slug;
    QString module;
    QString knowledgeNode;
    QStringList altNodes;
    QJsonObject extra;
};

struct ConclusionMeta {
    QString title;
    QStringList aliases;
    int difficulty = 0;
    QString category;
    QStringList tags;
    QString summary;
    bool isPro = false;
    QString remarks;
    QJsonObject extra;
};

struct ConclusionContent {
    int renderSchemaVersion = 0;
    QString primaryFormula;
    QVector<VariableDef> variables;
    QVector<ConditionDef> conditions;
    QVector<ConclusionDef> conclusions;
    QVector<RenderSection> sections;
    PlainContent plain;
    QJsonObject extra;

    bool hasSections() const;
};

struct ConclusionRecord {
    QString id;
    int schemaVersion = 0;
    QString type;
    QString status;
    ConclusionIdentity identity;
    ConclusionMeta meta;
    ConclusionContent content;
    AssetRefs assets;
    ConclusionExt ext;
    QJsonObject extra;

    bool hasStructuredSections() const;
};

}  // namespace domain::models

