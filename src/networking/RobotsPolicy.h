#pragma once
#include <QUrl>
#include <QRegularExpression>
#include <QVector>
namespace pw {
// RFC 9309: combine equally matching groups; longest matching rule wins, Allow wins ties.
class RobotsPolicy {
    struct Rule {
        QString pattern;
        bool allow;
    };
    struct Group {
        QStringList agents;
        QVector<Rule> rules;
    };
    QVector<Group> groups_;

  public:
    explicit RobotsPolicy(const QByteArray& bytes) {
        Group group;
        bool rules = false;
        for (auto line : QString::fromUtf8(bytes.left(512 * 1024)).split('\n')) {
            line = line.section('#', 0, 0).trimmed();
            auto key = line.section(':', 0, 0).trimmed().toLower();
            auto value = line.section(':', 1).trimmed();
            if (key == "user-agent") {
                if (rules) {
                    groups_ << group;
                    group = {};
                    rules = false;
                }
                group.agents << value.toLower();
            } else if ((key == "allow" || key == "disallow") && !group.agents.isEmpty()) {
                rules = true;
                if (!value.isEmpty())
                    group.rules << Rule{value, key == "allow"};
            }
        }
        if (!group.agents.isEmpty())
            groups_ << group;
    }
    static QString encoded(QString s) {
        // Decode percent-encoded unreserved ASCII only; preserve reserved delimiters.
        QByteArray b = s.toUtf8(), out;
        for (int i = 0; i < b.size(); ++i) {
            if (b[i] == '%' && i + 2 < b.size()) {
                bool ok = false;
                int c = b.mid(i + 1, 2).toInt(&ok, 16);
                if (ok) {
                    bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                      (c >= '0' && c <= '9') || QByteArray("-._~").contains(char(c));
                    if (unreserved)
                        out += char(c);
                    else
                        out += "%" + b.mid(i + 1, 2).toUpper();
                    i += 2;
                    continue;
                }
            }
            if (static_cast<unsigned char>(b[i]) >= 128)
                out += "%" + QByteArray::number(static_cast<unsigned char>(b[i]), 16).toUpper();
            else
                out += b[i];
        }
        return QString::fromLatin1(out);
    }
    bool allows(const QUrl& url, QString agent = "pathweave") const {
        int bestAgent = -1;
        QVector<Rule> rules;
        for (const auto& g : groups_) {
            int match = -1;
            for (auto a : g.agents) {
                if (a == "*")
                    match = std::max(match, 0);
                else if (agent.toLower().contains(a))
                    match = std::max(match, int(a.size()));
            }
            if (match < 0)
                continue;
            if (match > bestAgent) {
                bestAgent = match;
                rules.clear();
            }
            if (match == bestAgent)
                rules += g.rules;
        }
        QString path = encoded(url.path(QUrl::FullyEncoded) +
                               (url.hasQuery() ? "?" + url.query(QUrl::FullyEncoded) : ""));
        if (path.isEmpty())
            path = "/";
        int length = -1;
        bool allow = true;
        for (auto r : rules) {
            auto pattern = encoded(r.pattern);
            bool end = pattern.endsWith('$');
            if (end)
                pattern.chop(1);
            QString expression = "^";
            for (auto piece : pattern.split('*'))
                expression += QRegularExpression::escape(piece) + ".*";
            expression.chop(2);
            if (end)
                expression += "$";
            int specificity = pattern.toUtf8().size() - pattern.count('*');
            if (QRegularExpression(expression).match(path).hasMatch() &&
                (specificity > length || (specificity == length && r.allow))) {
                length = specificity;
                allow = r.allow;
            }
        }
        return allow;
    }
};
} // namespace pw
