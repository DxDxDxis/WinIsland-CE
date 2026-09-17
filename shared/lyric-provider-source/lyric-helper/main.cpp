#include "module.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSslSocket>
#include <cstdio>

// One bounded request per child process. No login, cookie store, UI, or player injection.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false\n*.info=false"));
    const auto args = app.arguments();
    if (args.size() != 3 || !QRegularExpression("^[0-9]{1,16}$").match(args[2]).hasMatch() ||
        (args[1] != "lyric" && args[1] != "lyric_new")) return 2;
    QSslSocket::setActiveBackend(QStringLiteral("schannel"));
    NeteaseCloudMusicApi api;
    const QVariantMap parameters{{"id", args[2]}};
    auto reply = args[1] == "lyric_new" ? api.lyric_new(parameters) : api.lyric(parameters);
    // Never export server cookies or raw network logging to the parent/diagnostic report.
    const QVariantMap output{{"http_status", reply.value("http_status")},
                             {"network_error", reply.value("network_error")},
                             {"body", reply.value("body")},
                             {"transport", "QCloudMusicApi/Qt-Schannel"}};
    const auto bytes = QJsonDocument::fromVariant(output).toJson(QJsonDocument::Compact);
    if (bytes.size() > 1024 * 1024) return 3;
    fwrite(bytes.constData(), 1, bytes.size(), stdout);
    fflush(stdout);
    return 0;
}
