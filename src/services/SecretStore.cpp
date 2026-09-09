#include "SecretStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif
namespace pw {
QString SecretStore::file(const QString& key) const {
    return directory_ + "/" +
           QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex()) +
           ".dpapi";
}
void SecretStore::put(const QString& key, const QString& value) {
    if (value.isEmpty()) {
        QFile::remove(file(key));
        return;
    }
#ifdef Q_OS_WIN
    QByteArray bytes = value.toUtf8();
    DATA_BLOB input{DWORD(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())}, out{};
    if (!CryptProtectData(&input, L"PathWeave local credentials", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out))
        throw std::runtime_error("Windows DPAPI encryption failed");
    QDir().mkpath(directory_);
    QSaveFile f(file(key));
    bool ok = f.open(QIODevice::WriteOnly) &&
              f.write(reinterpret_cast<char*>(out.pbData), out.cbData) == out.cbData && f.commit();
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    bytes.fill(0);
    if (!ok)
        throw std::runtime_error("Credential write failed");
#else
    Q_UNUSED(key);
    throw std::runtime_error("Secure credential storage requires Windows DPAPI");
#endif
}
QString SecretStore::get(const QString& key) const {
    QFile f(file(key));
    if (!f.open(QIODevice::ReadOnly))
        return {};
#ifdef Q_OS_WIN
    auto bytes = f.readAll();
    DATA_BLOB in{DWORD(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        throw std::runtime_error("Cannot decrypt credentials for this Windows user");
    QString result = QString::fromUtf8(reinterpret_cast<char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
#else
    return {};
#endif
}
void SecretStore::clear() {
    QDir d(directory_);
    for (auto f : d.entryList({"*.dpapi"}, QDir::Files))
        if (!d.remove(f))
            throw std::runtime_error("Cannot remove credential");
}
} // namespace pw
