#include "services.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QPointer>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrent>
#include <stdexcept>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

static QByteArray sha(const QByteArray &b) {
    return QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex();
}
static QByteArray hmac(const QByteArray &key, const QByteArray &data) {
    return QMessageAuthenticationCode::hash(data, key, QCryptographicHash::Sha256);
}
static QByteArray form(const QList<QPair<QString, QString>> &fields) {
    QByteArray b;
    for (const auto &[k, v] : fields) {
        if (!b.isEmpty())
            b += '&';
        b += QUrl::toPercentEncoding(k) + '=' + QUrl::toPercentEncoding(v);
    }
    return b;
}
static void fail(const QString &msg) {
    throw std::runtime_error(msg.toUtf8().constData());
}
void Job::finish(const QString &error) {
    if (complete)
        return;
    complete = true;
    for (auto *r : std::as_const(replies))
        r->abort();
    emit finished(error);
    deleteLater();
}
void Job::cancel() {
    finish("已取消");
}
QList<QByteArray> SseDecoder::feed(const QByteArray &data, bool eof) {
    pending += data;
    if (eof && !pending.endsWith('\n'))
        pending += '\n';
    QList<QByteArray> output;
    while (true) {
        auto n = pending.indexOf('\n');
        if (n < 0)
            break;
        auto line = pending.left(n);
        pending.remove(0, n + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty()) {
            if (!event.isEmpty()) {
                event.chop(1);
                output.append(event);
                event.clear();
            }
        } else if (line.startsWith("data:")) {
            auto v = line.mid(5);
            if (v.startsWith(' '))
                v.remove(0, 1);
            event += v + '\n';
        }
    }
    if (eof && !event.isEmpty()) {
        event.chop(1);
        output.append(event);
        event.clear();
    }
    return output;
}
QJsonObject Services::translationBody(const Model &m, const QString &text, const QString &prompt) {
    QString message = prompt;
    if (message.contains("{text}"))
        message.replace("{text}", text);
    else
        message += "\n\n" + text;
    QJsonObject root{{"model", m.model},
                     {"stream", true},
                     {"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", message}}}}};
    if (m.model.startsWith("deepseek-", Qt::CaseInsensitive) ||
        QUrl(m.endpoint).host().compare("api.deepseek.com", Qt::CaseInsensitive) == 0)
        root["thinking"] = QJsonObject{{"type", m.thinking ? "enabled" : "disabled"}};
    else
        root["enable_thinking"] = m.thinking;
    return root;
}
Job *Services::translate(const Model &m, const QString &text, const QString &prompt) {
    auto *job = new Job(this);
    QTimer::singleShot(0, job, [=, this] {
        if (job->complete)
            return;
        if (m.key.trimmed().isEmpty()) {
            job->finish("请配置 API Key");
            return;
        }
        QUrl url(m.endpoint.trimmed());
        if (url.path().isEmpty() || url.path() == "/")
            url.setPath("/chat/completions");
        else if (url.path() == "/v1" || url.path() == "/v1/")
            url.setPath("/v1/chat/completions");
        if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http")) {
            job->finish("API 地址无效");
            return;
        }
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("Authorization", "Bearer " + m.key.trimmed().toUtf8());
        req.setRawHeader("Accept", "text/event-stream");
        req.setTransferTimeout(60000);
        auto *r =
            network.post(req, QJsonDocument(translationBody(m, text, prompt)).toJson(QJsonDocument::Compact));
        job->replies.insert(r);
        connect(job, &QObject::destroyed, r, [r] {
            r->abort();
            r->deleteLater();
        });
        auto parser = std::make_shared<SseDecoder>();
        auto received = std::make_shared<bool>(false);
        auto consume = [=](bool eof) {
            for (const auto &event : parser->feed(r->readAll(), eof)) {
                if (job->complete)
                    return;
                if (event == "[DONE]") {
                    job->finish(*received ? QString{} : QStringLiteral("模型未返回翻译正文"));
                    return;
                }
                QJsonParseError e;
                auto doc = QJsonDocument::fromJson(event, &e);
                if (e.error != QJsonParseError::NoError) {
                    job->finish("流式响应 JSON 无效");
                    return;
                }
                auto root = doc.object();
                if (root.contains("error")) {
                    job->finish(root["error"].toObject()["message"].toString("API 返回错误"));
                    return;
                }
                auto choices = root["choices"].toArray();
                if (choices.isEmpty())
                    continue;
                auto chunk = choices[0].toObject()["delta"].toObject()["content"].toString();
                if (!chunk.isEmpty()) {
                    *received = true;
                    emit job->text(chunk);
                }
            }
        };
        connect(r, &QNetworkReply::readyRead, job, [=] {
            if (r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200)
                consume(false);
        });
        connect(r, &QNetworkReply::finished, job, [=] {
            if (job->complete)
                return;
            if (r->error() != QNetworkReply::NoError) {
                job->finish(QString("请求失败（HTTP %1）：%2")
                                .arg(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                                .arg(r->errorString()));
                return;
            }
            consume(true);
            if (!job->complete)
                job->finish(*received ? QString{} : QStringLiteral("模型未返回翻译正文"));
        });
    });
    return job;
}
void Services::post(Job *job, const QUrl &url, const QByteArray &body, const QByteArray &type,
                    Callback callback, const QMap<QByteArray, QByteArray> &headers) {
    if (job->complete)
        return;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, type);
    req.setTransferTimeout(60000);
    for (auto i = headers.begin(); i != headers.end(); ++i)
        req.setRawHeader(i.key(), i.value());
    auto *r = network.post(req, body);
    job->replies.insert(r);
    connect(job, &QObject::destroyed, r, [r] {
        r->abort();
        r->deleteLater();
    });
    connect(r, &QNetworkReply::finished, job, [=] {
        if (job->complete)
            return;
        if (r->error() != QNetworkReply::NoError) {
            job->finish(QString("OCR 请求失败（HTTP %1）：%2")
                            .arg(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                            .arg(r->errorString()));
            return;
        }
        QJsonParseError e;
        auto d = QJsonDocument::fromJson(r->readAll(), &e);
        if (e.error != QJsonParseError::NoError || !d.isObject()) {
            job->finish("OCR 响应无效");
            return;
        }
        try {
            callback(d.object());
        } catch (const std::exception &ex) {
            job->finish(QString::fromUtf8(ex.what()));
        }
    });
}
QString Services::parseOcr(const QString &provider, const QJsonObject &root) {
    QJsonArray items;
    QString key;
    if (provider == "Baidu") {
        if (root.contains("error_code"))
            fail(root["error_msg"].toString("百度 OCR 错误"));
        if (!root["words_result"].isArray())
            fail("百度 OCR 响应缺少识别结果");
        items = root["words_result"].toArray();
        key = "words";
    } else if (provider == "Tencent") {
        auto r = root["Response"].toObject();
        if (r.contains("Error"))
            fail(r["Error"].toObject()["Message"].toString("腾讯 OCR 错误"));
        if (!r["TextDetections"].isArray())
            fail("腾讯 OCR 响应缺少识别结果");
        items = r["TextDetections"].toArray();
        key = "DetectedText";
    } else {
        auto code = root.contains("code") ? root["code"] : root["status"];
        if (!code.isUndefined() && code.toVariant().toInt() != 10000)
            fail(root["message"].toString("火山 OCR 错误"));
        auto data = root["data"].toObject();
        if (!data["ocr_infos"].isArray())
            fail("火山 OCR 响应缺少识别结果");
        items = data["ocr_infos"].toArray();
        key = "text";
    }
    QStringList lines;
    for (const auto &v : items) {
        auto text = v.toObject()[key].toString();
        if (!text.trimmed().isEmpty())
            lines.append(text);
    }
    return lines.join('\n');
}
QString Services::localOcr(QImage image) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    struct Uninit {
        ~Uninit() {
            winrt::uninit_apartment();
        }
    } cleanup;
    using namespace winrt::Windows;
    auto languages = Media::Ocr::OcrEngine::AvailableRecognizerLanguages();
    Media::Ocr::OcrEngine engine{nullptr};
    for (auto l : languages) {
        if (QString::fromWCharArray(l.LanguageTag().c_str()).startsWith("zh-Hans")) {
            engine = Media::Ocr::OcrEngine::TryCreateFromLanguage(l);
            break;
        }
    }
    if (!engine)
        engine = Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
    if (!engine && languages.Size())
        engine = Media::Ocr::OcrEngine::TryCreateFromLanguage(languages.GetAt(0));
    if (!engine)
        fail("Windows OCR 不可用，请在 Windows 设置中安装中文或英文 OCR 语言包");
    auto max = int(Media::Ocr::OcrEngine::MaxImageDimension());
    if (image.width() > max || image.height() > max)
        image = image.scaled(max, max, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    image = image.convertToFormat(QImage::Format_ARGB32);
    Storage::Streams::DataWriter writer;
    writer.WriteBytes(
        winrt::array_view<const uint8_t>(image.constBits(), image.constBits() + image.sizeInBytes()));
    auto bitmap = Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(
        writer.DetachBuffer(), Graphics::Imaging::BitmapPixelFormat::Bgra8, image.width(), image.height(),
        Graphics::Imaging::BitmapAlphaMode::Ignore);
    auto result = engine.RecognizeAsync(bitmap).get();
    QStringList lines;
    for (auto line : result.Lines())
        lines.append(QString::fromWCharArray(line.Text().c_str()));
    return lines.join('\n');
}
Job *Services::recognize(const OcrConfig &c, const QImage &image) {
    auto *job = new Job(this);
    QTimer::singleShot(0, job, [=, this] {
        if (job->complete)
            return;
        if (image.isNull()) {
            job->finish("截图为空");
            return;
        }
        if (c.provider == "Windows") {
            auto *w = new QFutureWatcher<QPair<QString, QString>>(job);
            connect(w, &QFutureWatcherBase::finished, job, [=] {
                if (job->complete)
                    return;
                auto result = w->result();
                if (result.second.isEmpty())
                    emit job->text(result.first);
                job->finish(result.second);
            });
            w->setFuture(QtConcurrent::run([image] {
                try {
                    return qMakePair(localOcr(image), QString{});
                } catch (const winrt::hresult_error &e) {
                    return qMakePair(QString{}, QString::fromWCharArray(e.message().c_str()));
                } catch (const std::exception &e) {
                    return qMakePair(QString{}, QString::fromUtf8(e.what()));
                }
            }));
            return;
        }
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        auto b64 = QString::fromLatin1(png.toBase64());
        auto parse = [=](const QJsonObject &root) {
            emit job->text(parseOcr(c.provider, root));
            job->finish();
        };
        auto now = QDateTime::currentDateTimeUtc();
        if (c.provider == "Baidu") {
            if (c.baiduKey.isEmpty() || c.baiduSecret.isEmpty()) {
                job->finish("请配置百度 OCR 密钥");
                return;
            }
            post(job, QUrl("https://aip.baidubce.com/oauth/2.0/token"),
                 form({{"grant_type", "client_credentials"},
                       {"client_id", c.baiduKey},
                       {"client_secret", c.baiduSecret}}),
                 "application/x-www-form-urlencoded", [=, this](const QJsonObject &root) {
                     auto token = root["access_token"].toString();
                     if (token.isEmpty())
                         fail(root["error_description"].toString("百度授权失败"));
                     QUrl url("https://aip.baidubce.com/rest/2.0/ocr/v1/general_basic");
                     QUrlQuery q;
                     q.addQueryItem("access_token", token);
                     url.setQuery(q);
                     post(job, url, form({{"image", b64}, {"detect_language", "true"}}),
                          "application/x-www-form-urlencoded", parse);
                 });
        } else if (c.provider == "Tencent") {
            if (c.tencentId.isEmpty() || c.tencentKey.isEmpty()) {
                job->finish("请配置腾讯云 OCR 密钥");
                return;
            }
            auto body = QJsonDocument(QJsonObject{{"ImageBase64", b64}}).toJson(QJsonDocument::Compact);
            auto date = now.toString("yyyy-MM-dd").toUtf8(),
                 timestamp = QByteArray::number(now.toSecsSinceEpoch());
            QByteArray headers =
                "content-type:application/json; charset=utf-8\nhost:ocr.tencentcloudapi.com\n";
            auto canonical = "POST\n/\n\n" + headers + "\ncontent-type;host\n" + sha(body);
            auto scope = date + "/ocr/tc3_request";
            auto sign = "TC3-HMAC-SHA256\n" + timestamp + '\n' + scope + '\n' + sha(canonical);
            auto key = hmac(hmac(hmac("TC3" + c.tencentKey.toUtf8(), date), "ocr"), "tc3_request");
            auto auth = "TC3-HMAC-SHA256 Credential=" + c.tencentId.toUtf8() + "/" + scope +
                        ", SignedHeaders=content-type;host, Signature=" + hmac(key, sign).toHex();
            post(job, QUrl("https://ocr.tencentcloudapi.com/"), body, "application/json; charset=utf-8",
                 parse,
                 {{"Authorization", auth},
                  {"X-TC-Action", "GeneralBasicOCR"},
                  {"X-TC-Version", "2018-11-19"},
                  {"X-TC-Timestamp", timestamp},
                  {"X-TC-Region", "ap-beijing"}});
        } else if (c.provider == "Volcengine") {
            if (c.access.isEmpty() || c.secret.isEmpty()) {
                job->finish("请配置火山引擎 OCR 密钥");
                return;
            }
            auto body = form({{"image_base64", b64}, {"mode", "default"}});
            auto date = now.toString("yyyyMMdd").toUtf8(),
                 stamp = now.toString("yyyyMMdd'T'HHmmss'Z'").toUtf8();
            QByteArray query = "Action=MultiLanguageOCR&Version=2022-08-31",
                       scope = date + "/cn-north-1/cv/request";
            auto canonical = "POST\n/\n" + query + "\nhost:visual.volcengineapi.com\nx-date:" + stamp +
                             "\n\nhost;x-date\n" + sha(body);
            auto sign = "HMAC-SHA256\n" + stamp + '\n' + scope + '\n' + sha(canonical);
            auto key = hmac(hmac(hmac(hmac(c.secret.toUtf8(), date), "cn-north-1"), "cv"), "request");
            auto auth = "HMAC-SHA256 Credential=" + c.access.toUtf8() + "/" + scope +
                        ", SignedHeaders=host;x-date, Signature=" + hmac(key, sign).toHex();
            post(job, QUrl("https://visual.volcengineapi.com/?" + QString::fromLatin1(query)), body,
                 "application/x-www-form-urlencoded", parse, {{"X-Date", stamp}, {"Authorization", auth}});
        } else
            job->finish("不支持的 OCR 服务");
    });
    return job;
}
