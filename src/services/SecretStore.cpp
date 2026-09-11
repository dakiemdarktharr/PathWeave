#include "SecretStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#elif defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

namespace pw {
QString SecretStore::file(const QString& key) const {
    return directory_ + "/" +
           QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex()) +
           ".dpapi";
}

QString SecretStore::serviceName() const {
    const auto scope = QCryptographicHash::hash(directory_.toUtf8(), QCryptographicHash::Sha256)
                           .toHex()
                           .left(24);
    return QStringLiteral("com.pathweave.credentials.") + QString::fromLatin1(scope);
}

#if defined(Q_OS_MACOS)
namespace {
CFStringRef makeString(const QString& value) {
    const auto bytes = value.toUtf8();
    return CFStringCreateWithBytes(kCFAllocatorDefault,
                                   reinterpret_cast<const UInt8*>(bytes.constData()),
                                   CFIndex(bytes.size()), kCFStringEncodingUTF8, false);
}
CFMutableDictionaryRef keychainQuery(CFStringRef service, CFStringRef account, bool returnData) {
    auto query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    if (returnData) {
        CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
    }
    return query;
}
QString keychainError(const char* operation, OSStatus status) {
    return QStringLiteral("%1 (Keychain OSStatus %2)").arg(QString::fromLatin1(operation)).arg(status);
}
} // namespace
#endif

void SecretStore::put(const QString& key, const QString& value) {
    if (value.isEmpty()) {
#ifdef Q_OS_MACOS
        const auto service = makeString(serviceName());
        const auto account = makeString(key);
        auto query = keychainQuery(service, account, false);
        const auto status = SecItemDelete(query);
        CFRelease(query);
        CFRelease(account);
        CFRelease(service);
        if (status != errSecSuccess && status != errSecItemNotFound)
            throw std::runtime_error(keychainError("Cannot remove credential", status).toStdString());
        return;
#else
        QFile::remove(file(key));
        return;
#endif
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
#elif defined(Q_OS_MACOS)
    const auto service = makeString(serviceName());
    const auto account = makeString(key);
    const auto bytes = value.toUtf8();
    const auto data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(bytes.constData()),
                                   CFIndex(bytes.size()));
    auto query = keychainQuery(service, account, false);
    CFTypeRef existing = nullptr;
    auto status = SecItemCopyMatching(query, &existing);
    if (existing)
        CFRelease(existing);
    if (status == errSecSuccess) {
        auto updates = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                 &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(updates, kSecValueData, data);
        status = SecItemUpdate(query, updates);
        CFRelease(updates);
    } else if (status == errSecItemNotFound) {
        CFDictionarySetValue(query, kSecValueData, data);
        CFDictionarySetValue(query, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlock);
        status = SecItemAdd(query, nullptr);
    }
    CFRelease(query);
    CFRelease(data);
    CFRelease(account);
    CFRelease(service);
    if (status != errSecSuccess)
        throw std::runtime_error(keychainError("Cannot store credential", status).toStdString());
#else
    Q_UNUSED(key);
    throw std::runtime_error("Secure credential storage is not available on this platform");
#endif
}

QString SecretStore::get(const QString& key) const {
#ifdef Q_OS_WIN
    QFile f(file(key));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    auto bytes = f.readAll();
    DATA_BLOB in{DWORD(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        throw std::runtime_error("Cannot decrypt credentials for this Windows user");
    QString result = QString::fromUtf8(reinterpret_cast<char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
#elif defined(Q_OS_MACOS)
    const auto service = makeString(serviceName());
    const auto account = makeString(key);
    auto query = keychainQuery(service, account, true);
    CFTypeRef result = nullptr;
    const auto status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    CFRelease(account);
    CFRelease(service);
    if (status == errSecItemNotFound)
        return {};
    if (status != errSecSuccess || !result)
        throw std::runtime_error(keychainError("Cannot read credential", status).toStdString());
    const auto data = static_cast<CFDataRef>(result);
    const auto bytes = CFDataGetBytePtr(data);
    const auto length = CFDataGetLength(data);
    const auto value = QString::fromUtf8(reinterpret_cast<const char*>(bytes), int(length));
    CFRelease(data);
    return value;
#else
    Q_UNUSED(key);
    return {};
#endif
}

void SecretStore::clear() {
#ifdef Q_OS_MACOS
    const auto service = makeString(serviceName());
    auto query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    const auto status = SecItemDelete(query);
    CFRelease(query);
    CFRelease(service);
    if (status != errSecSuccess && status != errSecItemNotFound)
        throw std::runtime_error(keychainError("Cannot clear credentials", status).toStdString());
#elif defined(Q_OS_WIN)
    QDir d(directory_);
    for (auto f : d.entryList({"*.dpapi"}, QDir::Files))
        if (!d.remove(f))
            throw std::runtime_error("Cannot remove credential");
#else
    QDir d(directory_);
    for (auto f : d.entryList({"*.dpapi"}, QDir::Files))
        if (!d.remove(f))
            throw std::runtime_error("Cannot remove credential");
#endif
}
} // namespace pw
