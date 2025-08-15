#pragma once

#include <QString>
#include <QJsonObject>
#include <QHttpHeaders>
#include <QUuid>

#include "IdMap.hpp"
#include "../Json/Jsonable.hpp"
#include "../Json/FromJsonFactory.hpp"
namespace StructuraSystems::Server {

    struct SessionEntry : public Jsonable
    {
        qint64 id;
        QString email;
        QString password;
        std::optional<QUuid> token;

        explicit SessionEntry(const QString& email, const QString& password)
            : id(nextId()), email(email), password(password)
        {
        }

        void startSession() { token = generateToken(); }

        void endSession() { token = std::nullopt; }

        QJsonObject toJson() const override
        {
            return token
                ? QJsonObject{ { "id", id },
                               { "token", token->toString(QUuid::StringFormat::WithoutBraces) } }
            : QJsonObject{};
        }

        bool operator==(const QString& otherToken) const
        {
            return token && *token == QUuid::fromString(otherToken);
        }

    private:
        QUuid generateToken() { return QUuid::createUuid(); }

        static qint64 nextId()
        {
            static qint64 lastId = 1;
            return lastId++;
        }
    };

    struct SessionEntryFactory : public FromJsonFactory<SessionEntry>
    {
        std::optional<SessionEntry> fromJson(const QJsonObject& json) const override
        {
            if (!json.contains("email") || !json.contains("password"))
                return std::nullopt;

            return SessionEntry(json.value("email").toString(), json.value("password").toString());
        }
    };

    class SessionApi
    {
    public:
        explicit SessionApi(const IdMap<SessionEntry>& sessions,
            std::unique_ptr<FromJsonFactory<SessionEntry>> factory)
            : sessions(sessions), factory(std::move(factory))
        {
        }

        QHttpServerResponse registerSession(const QHttpServerRequest& request)
        {
            //const auto json = byteArrayToJsonObject(request.body());
            //if (!json)
                return QHttpServerResponse(QHttpServerResponder::StatusCode::BadRequest);
            //const auto item = factory->fromJson(*json);
            //if (!item)
            //    return QHttpServerResponse(QHttpServerResponder::StatusCode::BadRequest);

            //const auto session = sessions.insert(item->id, *item);
            //session->startSession();
            //return QHttpServerResponse(session->toJson());
        }

        QHttpServerResponse login(const QHttpServerRequest& request)
        {
            //const auto json = byteArrayToJsonObject(request.body());

            //if (!json || !json->contains("email") || !json->contains("password"))
                return QHttpServerResponse(QHttpServerResponder::StatusCode::BadRequest);

            //auto maybeSession = std::find_if(
            //    sessions.begin(), sessions.end(),
            //    [email = json->value("email").toString(),
            //    password = json->value("password").toString()](const auto& it) {
            //        return it.password == password && it.email == email;
            //    });
            //if (maybeSession == sessions.end()) {
            //    return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
            //}
            //maybeSession->startSession();
            //return QHttpServerResponse(maybeSession->toJson());
        }

        QHttpServerResponse logout(const QHttpServerRequest& request)
        {
            const auto maybeToken = getTokenFromRequest(request);
            if (!maybeToken)
                return QHttpServerResponse(QHttpServerResponder::StatusCode::BadRequest);

            auto maybeSession = std::find(sessions.begin(), sessions.end(), *maybeToken);
            if (maybeSession != sessions.end())
                maybeSession->endSession();
            return QHttpServerResponse(QHttpServerResponder::StatusCode::Ok);
        }

        bool authorize(const QHttpServerRequest& request) const
        {
            const auto maybeToken = getTokenFromRequest(request);
            if (maybeToken) {
                const auto maybeSession = std::find(sessions.begin(), sessions.end(), *maybeToken);
                return maybeSession != sessions.end() && *maybeSession == *maybeToken;
            }
            return false;
        }

        static std::optional<QString> getTokenFromRequest(const QHttpServerRequest& request)
        {
            std::optional<QString> token;
            //if (auto bytes = getValueFromHeader(request.headers(), "token"); !bytes.isEmpty()) {
            //    token = bytes;
            //}
            return token;
        }

        static QByteArray getValueFromHeader(const QHttpHeaders& headers,
            QByteArrayView headerName)
        {
            return headers.value(headerName).toByteArray();
        }

    private:
        IdMap<SessionEntry> sessions;
        std::unique_ptr<FromJsonFactory<SessionEntry>> factory;
    };
}