//
// Created by Moritz Herzog on 22.10.26.
//

#ifndef STRUCTURASYSTEMS_CREDENTIALSTORE_H
#define STRUCTURASYSTEMS_CREDENTIALSTORE_H

#include <string>

namespace StructuraSystems::Client {
    /**
     * Thin synchronous wrapper around QtKeychain. Passwords are stored in the OS keychain (Credential Manager,
     * macOS Keychain, Secret Service). The service name is the application name, the account is chosen by the caller.
     */
    class CredentialStore {
    public:
        /** Outcome of a read: a missing entry is not an error, so it must never be confused with a failed access. */
        enum class ReadStatus {
            Found,
            NotFound,
            Error
        };

        struct ReadResult {
            ReadStatus Status = ReadStatus::Error;
            std::string Password;
        };

        /** Prefix of all account keys; keeps the entries apart from other applications in the Windows Credential Manager. */
        static inline const std::string ACCOUNT_PREFIX = "StructuraSystems/";

        CredentialStore() = default;
        ~CredentialStore() = default;

        /**
         * Builds the account key "StructuraSystems/username@serverPath". '%' and '@' in the username are
         * percent-encoded so that different (username, serverPath) pairs never share a key.
         */
        static std::string accountKey(const std::string &username, const std::string &serverPath);

        /** Reads the password of the given account. See ReadStatus for the possible outcomes. */
        ReadResult readPassword(const std::string &account);

        /** Stores the password. @return true on success. */
        bool writePassword(const std::string &account, const std::string &password);

        /** Removes the entry. A missing entry counts as success. @return true on success. */
        bool deletePassword(const std::string &account);

        /** false if the last operation failed because the keychain is not usable (e.g. no Secret Service running). */
        [[nodiscard]] bool isAvailable() const;

        /** Error message of the last failed operation, empty if it succeeded. */
        [[nodiscard]] const std::string &lastError() const;

    private:
        void setResult(bool available, const std::string &error);

        bool Available = true;
        std::string LastError;
    };
}

#endif //STRUCTURASYSTEMS_CREDENTIALSTORE_H
