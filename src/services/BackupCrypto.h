#pragma once
#include <QByteArray>
#include <QString>
namespace pw {
class BackupCrypto {
  public:
    static QByteArray encrypt(const QByteArray& bytes, const QString& password);
    static QByteArray decrypt(const QByteArray& bytes, const QString& password);
};
} // namespace pw
