#include "BackupCrypto.h"
#include <stdexcept>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif
namespace pw {
#ifdef Q_OS_WIN
namespace {
void check(NTSTATUS status) {
    if (status < 0)
        throw std::runtime_error(
            "Không thể mã hóa/giải mã bản sao: mật khẩu sai, tệp hỏng hoặc lỗi Windows CNG.");
}
struct Algorithm {
    BCRYPT_ALG_HANDLE value = nullptr;
    ~Algorithm() {
        if (value)
            BCryptCloseAlgorithmProvider(value, 0);
    }
};
struct Key {
    BCRYPT_KEY_HANDLE value = nullptr;
    ~Key() {
        if (value)
            BCryptDestroyKey(value);
    }
};
struct Sensitive {
    QByteArray bytes;
    explicit Sensitive(QByteArray data) : bytes(std::move(data)) {}
    ~Sensitive() {
        if (!bytes.isEmpty())
            SecureZeroMemory(bytes.data(), SIZE_T(bytes.size()));
    }
};
PUCHAR ptr(QByteArray& b) {
    return reinterpret_cast<PUCHAR>(b.data());
}
QByteArray process(bool encrypt, QByteArray payload, QString password, QByteArray salt, QByteArray nonce,
                   QByteArray& tag) {
    Algorithm hash, aes;
    Key key;
    check(BCryptOpenAlgorithmProvider(&hash.value, BCRYPT_SHA256_ALGORITHM, nullptr,
                                      BCRYPT_ALG_HANDLE_HMAC_FLAG));
    Sensitive pass(password.toUtf8()), derived(QByteArray(32, 0));
    check(BCryptDeriveKeyPBKDF2(hash.value, ptr(pass.bytes), ULONG(pass.bytes.size()), ptr(salt),
                                ULONG(salt.size()), 600000, ptr(derived.bytes), 32, 0));
    check(BCryptOpenAlgorithmProvider(&aes.value, BCRYPT_AES_ALGORITHM, nullptr, 0));
    check(BCryptSetProperty(aes.value, BCRYPT_CHAINING_MODE,
                            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                            sizeof(BCRYPT_CHAIN_MODE_GCM), 0));
    check(BCryptGenerateSymmetricKey(aes.value, &key.value, nullptr, 0, ptr(derived.bytes), 32, 0));
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    QByteArray header = "PWBK1" + salt + nonce;
    info.pbNonce = ptr(nonce);
    info.cbNonce = ULONG(nonce.size());
    info.pbAuthData = ptr(header);
    info.cbAuthData = ULONG(header.size());
    info.pbTag = ptr(tag);
    info.cbTag = ULONG(tag.size());
    Sensitive output(QByteArray(payload.size(), 0));
    ULONG written = 0;
    auto status = encrypt ? BCryptEncrypt(key.value, ptr(payload), ULONG(payload.size()), &info, nullptr, 0,
                                          ptr(output.bytes), ULONG(output.bytes.size()), &written, 0)
                          : BCryptDecrypt(key.value, ptr(payload), ULONG(payload.size()), &info, nullptr, 0,
                                          ptr(output.bytes), ULONG(output.bytes.size()), &written, 0);
    check(status);
    return QByteArray(output.bytes.constData(), written);
}
} // namespace
#endif
QByteArray BackupCrypto::encrypt(const QByteArray& bytes, const QString& password) {
#ifdef Q_OS_WIN
    if (password.size() < 12 || password.size() > 1024)
        throw std::runtime_error("Mật khẩu bản sao phải dài 12–1024 ký tự.");
    if (bytes.isEmpty() || bytes.size() > 512 * 1024 * 1024)
        throw std::runtime_error("Bản sao trống hoặc quá 512 MiB.");
    QByteArray salt(16, 0), nonce(12, 0), tag(16, 0);
    check(BCryptGenRandom(nullptr, ptr(salt), 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
    check(BCryptGenRandom(nullptr, ptr(nonce), 12, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
    auto encrypted = process(true, bytes, password, salt, nonce, tag);
    return "PWBK1" + salt + nonce + tag + encrypted;
#else
    Q_UNUSED(bytes);
    Q_UNUSED(password);
    throw std::runtime_error("Mã hóa bản sao yêu cầu Windows CNG.");
#endif
}
QByteArray BackupCrypto::decrypt(const QByteArray& bytes, const QString& password) {
#ifdef Q_OS_WIN
    if (!bytes.startsWith("PWBK1") || bytes.size() <= 49 || bytes.size() > 512 * 1024 * 1024 + 49 ||
        password.size() > 1024)
        throw std::runtime_error("Bản sao mã hóa không hợp lệ.");
    auto tag = bytes.mid(33, 16);
    return process(false, bytes.mid(49), password, bytes.mid(5, 16), bytes.mid(21, 12), tag);
#else
    Q_UNUSED(bytes);
    Q_UNUSED(password);
    throw std::runtime_error("Giải mã bản sao yêu cầu Windows CNG.");
#endif
}
} // namespace pw
