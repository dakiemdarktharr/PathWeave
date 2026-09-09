#pragma once
#include <QJsonObject>
#include <QStringList>
#include <QVector>
namespace pw {
struct Field {
    QString name, label, type = "TEXT", options, reference;
    bool required = false;
};
struct Entity {
    QString name, label;
    QVector<Field> fields;
};
const QVector<Entity>& schema();
const Entity& entity(const QString& name);
QStringList tableNames();
QString normalize(QString text);
QString canonicalUrl(const QString& text);
QString jobIdentityKey(const QJsonObject& job);
QVector<Field> surveyFields();
QStringList validateSurvey(const QJsonObject& answers);
int profileCompletion(const QJsonObject& answers);
QString displayValue(const QString& value);
} // namespace pw
