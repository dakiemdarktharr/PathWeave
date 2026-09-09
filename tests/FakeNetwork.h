#pragma once
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <cstring>
class FakeReply : public QNetworkReply {
  public:
    FakeReply(const QNetworkRequest& request, int code, QByteArray payload, int delay, QObject* parent,
              QUrl redirect = {})
        : QNetworkReply(parent), payload_(std::move(payload)) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        if (!redirect.isEmpty())
            setAttribute(QNetworkRequest::RedirectionTargetAttribute, redirect);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, code);
        if (code == 429)
            setRawHeader("Retry-After", "120");
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(delay, this, [this] {
            if (isFinished())
                return;
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }
    void abort() override {
        if (isFinished())
            return;
        setError(OperationCanceledError, "fixture cancellation");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return payload_.size() - offset_ + QNetworkReply::bytesAvailable();
    }

  protected:
    qint64 readData(char* data, qint64 maximum) override {
        qint64 count = std::min(maximum, qint64(payload_.size()) - offset_);
        if (count <= 0)
            return -1;
        std::memcpy(data, payload_.constData() + offset_, size_t(count));
        offset_ += count;
        return count;
    }

  private:
    QByteArray payload_;
    qint64 offset_ = 0;
};
class FakeNetwork : public QNetworkAccessManager {
  public:
    int status = 200, delay = 0, requests = 0;
    QByteArray payload = "{\"jobs\":[]}";
    QMap<QString, QUrl> redirects;
    QList<QUrl> urls;

  protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        ++requests;
        urls << request.url();
        auto redirect = redirects.value(request.url().path());
        return new FakeReply(request, redirect.isEmpty() ? status : 302, payload, delay, this, redirect);
    }
};
