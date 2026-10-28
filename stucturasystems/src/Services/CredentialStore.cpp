//
// Created by Moritz Herzog on 22.10.26.
//

#include "CredentialStore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QString>
#include <qtkeychain/keychain.h>

namespace StructuraSystems::Client {
    namespace {
        /** Runs the job and blocks (while processing events) until it has finished. */
        void runSynchronously(QKeychain::Job &job) {
            QEventLoop loop;
            QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
            job.setAutoDelete(false);
            job.start();
            // start() is queued, so finished() is always emitted after the loop is running.
            loop.exec();
        }

        /** Errors that mean the keychain itself cannot be used (as opposed to e.g. a missing entry). */
        bool isBackendFailure(QKeychain::Error error) {
            return error == QKeychain::AccessDenied ||
                   error == QKeychain::NoBackendAvailable || error == QKeychain::NotImplemented ||
                   error == QKeychain::OtherError;
        }
    }

    std::string CredentialStore::accountKey(const std::string &username, const std::string &serverPath) {
        std::string escapedUsername;
        for (const char character : username) {
            if (character == '%')
                escapedUsername += "%25";
            else if (character == '@')
                escapedUsername += "%40";
            else
                escapedUsername += character;
        }
        return ACCOUNT_PREFIX + escapedUsername + "@" + serverPath;
    }

    CredentialStore::ReadResult CredentialStore::readPassword(const std::string &account) {
        QKeychain::ReadPasswordJob job(QCoreApplication::applicationName());
        job.setKey(QString::fromStdString(account));
        runSynchronously(job);

        if (job.error() == QKeychain::NoError) {
            setResult(true, "");
            return {ReadStatus::Found, job.textData().toStdString()};
        }
        if (job.error() == QKeychain::EntryNotFound) {
            setResult(true, "");
            return {ReadStatus::NotFound, ""};
        }
        setResult(!isBackendFailure(job.error()), job.errorString().toStdString());
        qWarning() << "Could not read the password from the keychain:" << job.errorString();
        return {ReadStatus::Error, ""};
    }

    bool CredentialStore::writePassword(const std::string &account, const std::string &password) {
        QKeychain::WritePasswordJob job(QCoreApplication::applicationName());
        job.setKey(QString::fromStdString(account));
        job.setTextData(QString::fromStdString(password));
        runSynchronously(job);

        if (job.error() == QKeychain::NoError) {
            setResult(true, "");
            return true;
        }
        setResult(!isBackendFailure(job.error()), job.errorString().toStdString());
        qWarning() << "Could not write the password to the keychain:" << job.errorString();
        return false;
    }

    bool CredentialStore::deletePassword(const std::string &account) {
        QKeychain::DeletePasswordJob job(QCoreApplication::applicationName());
        job.setKey(QString::fromStdString(account));
        runSynchronously(job);

        if (job.error() == QKeychain::NoError || job.error() == QKeychain::EntryNotFound) {
            setResult(true, "");
            return true;
        }
        setResult(!isBackendFailure(job.error()), job.errorString().toStdString());
        qWarning() << "Could not delete the password from the keychain:" << job.errorString();
        return false;
    }

    bool CredentialStore::isAvailable() const {
        return Available;
    }

    const std::string &CredentialStore::lastError() const {
        return LastError;
    }

    void CredentialStore::setResult(bool available, const std::string &error) {
        Available = available;
        LastError = error;
    }
}
