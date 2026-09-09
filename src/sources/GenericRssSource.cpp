#include "Sources.h"
#include <QXmlStreamReader>
#include <stdexcept>
namespace pw {
GenericRssSource::GenericRssSource(NetworkService& n) : PublicSource("rss", "RSS / Atom", n) {}
QUrl GenericRssSource::endpoint(const SearchQuery&) const {
    return QUrl(config_["url"].toString());
}
QJsonArray GenericRssSource::parse(const QByteArray& b) const {
    QXmlStreamReader xml(b);
    QJsonArray out;
    QJsonObject j;
    bool inItem = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isDTD())
            throw std::runtime_error("DTD is not accepted in feeds");
        if (xml.isStartElement()) {
            auto n = xml.name().toString();
            if (n == "item" || n == "entry") {
                inItem = true;
                j = {{"company", config_["company"].toString("RSS source")},
                     {"employment_type", "unknown"},
                     {"workplace_mode", "unknown"}};
            } else if (inItem) {
                if (n == "title")
                    j["title"] = xml.readElementText();
                else if (n == "description" || n == "summary" || n == "content")
                    j["description"] = plain(xml.readElementText(QXmlStreamReader::IncludeChildElements));
                else if (n == "guid" || n == "id")
                    j["external_id"] = xml.readElementText();
                else if (n == "pubDate" || n == "published")
                    j["posted_at"] = xml.readElementText();
                else if (n == "link") {
                    auto href = xml.attributes().value("href").toString();
                    j["canonical_url"] = href.isEmpty() ? xml.readElementText() : href;
                }
            }
        } else if (xml.isEndElement() && (xml.name() == u"item" || xml.name() == u"entry")) {
            out.append(j);
            inItem = false;
        }
    }
    if (xml.hasError())
        throw std::runtime_error("Invalid RSS/Atom XML");
    return out;
}
} // namespace pw
