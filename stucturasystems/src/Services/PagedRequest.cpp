//---------------------------------------------------------
// External Classes
//---------------------------------------------------------
#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <curl/curl.h>
#include <sysmlv2/service/online/ConnectionError.h>
#include <sysmlv2/service/online/HttpException.h>
#include <sysmlv2/service/online/NotEnoughMemoryError.h>

//---------------------------------------------------------
// Internal Classes
//---------------------------------------------------------
#include "PagedRequest.h"

namespace StructuraSystems::Client {
    namespace {
        struct CurlDeleter {
            void operator()(CURL *handle) const { curl_easy_cleanup(handle); }
        };
        struct CurlSlistDeleter {
            void operator()(curl_slist *list) const { curl_slist_free_all(list); }
        };

        std::once_flag CurlGlobalInitFlag;

        size_t appendToString(char *contents, size_t size, size_t count, void *target) {
            static_cast<std::string *>(target)->append(contents, size * count);
            return size * count;
        }

        bool equalsIgnoreCase(const std::string &first, const std::string &second) {
            return first.size() == second.size() && std::equal(first.begin(), first.end(), second.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            });
        }

        std::string trim(const std::string &text) {
            const auto first = text.find_first_not_of(" \t");
            if (first == std::string::npos)
                return {};
            return text.substr(first, text.find_last_not_of(" \t") - first + 1);
        }

        /** The values of all headers with this name in the last response of the block (folded lines are joined). */
        std::vector<std::string> headerValues(const std::string &headers, const std::string &name) {
            std::vector<std::string> values;
            // Index of the value that a folded line continues, -1 if the previous header is another one.
            long long current = -1;
            size_t position = 0;
            while (position < headers.size()) {
                auto lineEnd = headers.find('\n', position);
                if (lineEnd == std::string::npos)
                    lineEnd = headers.size();
                std::string line = headers.substr(position, lineEnd - position);
                position = lineEnd + 1;
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                if (line.rfind("HTTP/", 0) == 0) {
                    // A new response (after 100 Continue or a redirect): only the last one counts.
                    values.clear();
                    current = -1;
                    continue;
                }
                if (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
                    if (current >= 0)
                        values[static_cast<size_t>(current)] += " " + trim(line);
                    continue;
                }
                current = -1;
                const auto colon = line.find(':');
                if (colon == std::string::npos || !equalsIgnoreCase(trim(line.substr(0, colon)), name))
                    continue;
                values.push_back(trim(line.substr(colon + 1)));
                current = static_cast<long long>(values.size()) - 1;
            }
            return values;
        }

        /** Reads a quoted string or a token starting at position; position ends behind it. */
        std::string readParameterValue(const std::string &text, size_t &position) {
            std::string value;
            if (position < text.size() && text[position] == '"') {
                ++position;
                while (position < text.size() && text[position] != '"') {
                    if (text[position] == '\\' && position + 1 < text.size())
                        ++position;
                    value += text[position++];
                }
                if (position < text.size())
                    ++position;
            } else {
                while (position < text.size() && text[position] != ';' && text[position] != ',')
                    value += text[position++];
                value = trim(value);
            }
            return value;
        }

        /** True if the value of a rel parameter (one or several space separated relations) contains "next". */
        bool relContainsNext(const std::string &relations) {
            size_t position = 0;
            while (position < relations.size()) {
                const auto start = relations.find_first_not_of(" \t", position);
                if (start == std::string::npos)
                    break;
                auto end = relations.find_first_of(" \t", start);
                if (end == std::string::npos)
                    end = relations.size();
                if (equalsIgnoreCase(relations.substr(start, end - start), "next"))
                    return true;
                position = end;
            }
            return false;
        }

        /** The target of the first link with rel="next" in the value of one Link header. */
        std::optional<std::string> nextInLinkValue(const std::string &value) {
            size_t position = 0;
            while (position < value.size()) {
                // Next link: <target>
                const auto open = value.find('<', position);
                if (open == std::string::npos)
                    return std::nullopt;
                const auto close = value.find('>', open);
                if (close == std::string::npos)
                    return std::nullopt;
                const std::string target = trim(value.substr(open + 1, close - open - 1));
                position = close + 1;

                // Its parameters, up to the comma that separates the next link.
                bool isNext = false;
                while (position < value.size() && value[position] != ',') {
                    if (value[position] != ';') {
                        ++position;
                        continue;
                    }
                    ++position;
                    const auto nameStart = value.find_first_not_of(" \t", position);
                    if (nameStart == std::string::npos) {
                        position = value.size();
                        break;
                    }
                    auto nameEnd = value.find_first_of("=;,", nameStart);
                    if (nameEnd == std::string::npos)
                        nameEnd = value.size();
                    const std::string name = trim(value.substr(nameStart, nameEnd - nameStart));
                    position = nameEnd;
                    if (position < value.size() && value[position] == '=') {
                        ++position;
                        const auto parameterValue = readParameterValue(value, position);
                        if (equalsIgnoreCase(name, "rel") && relContainsNext(parameterValue))
                            isNext = true;
                    }
                }
                if (isNext && !target.empty())
                    return target;
            }
            return std::nullopt;
        }

        std::string withoutQuery(const std::string &url) {
            return url.substr(0, url.find('?'));
        }

        void checkStatus(const HttpResponse &response) {
            if (response.Status < 200 || response.Status >= 300)
                throw SysMLv2::API::EXCEPTIONS::HTTPException(response.Status, response.Body, headerValue(response.Headers, "Retry-After"));
        }
    }

    std::optional<std::string> nextPageUrl(const std::string &headers) {
        for (const auto &value : headerValues(headers, "Link"))
            if (const auto next = nextInLinkValue(value))
                return next;
        return std::nullopt;
    }

    std::string headerValue(const std::string &headers, const std::string &name) {
        const auto values = headerValues(headers, name);
        return values.empty() ? std::string() : values.back();
    }

    std::optional<std::string> followLink(const std::string &requestUrl, const std::string &linkTarget) {
        const auto query = linkTarget.find('?');
        if (query == std::string::npos || query + 1 >= linkTarget.size())
            return std::nullopt;
        return withoutQuery(requestUrl) + linkTarget.substr(query);
    }

    std::string buildUrl(const std::string &serverAddress, const std::string &endpoint) {
        std::string url = serverAddress;
        if (url.empty() || url.back() != '/')
            url += '/';
        const auto first = endpoint.find_first_not_of('/');
        if (first != std::string::npos)
            url += endpoint.substr(first);
        return url;
    }

    std::vector<nlohmann::json> getAllPages(const std::string &firstUrl, const std::string &bearerToken, const HttpTransport &transport) {
        std::vector<nlohmann::json> entries;
        std::set<std::string> requested;
        std::string url = firstUrl;
        while (true) {
            if (!requested.insert(url).second)
                throw std::runtime_error("The server links to a page that was already loaded: " + url);

            const auto response = transport(url, bearerToken);
            checkStatus(response);

            auto page = nlohmann::json::parse(response.Body, nullptr, false);
            if (!page.is_array())
                throw std::runtime_error("The server did not return a list.");
            for (auto &entry : page)
                entries.push_back(std::move(entry));

            const auto link = nextPageUrl(response.Headers);
            if (!link.has_value())
                break;
            const auto next = followLink(url, *link);
            if (!next.has_value())
                break;
            url = *next;
        }
        return entries;
    }

    nlohmann::json getJson(const std::string &url, const std::string &bearerToken, const HttpTransport &transport) {
        const auto response = transport(url, bearerToken);
        checkStatus(response);
        auto json = nlohmann::json::parse(response.Body, nullptr, false);
        if (json.is_discarded())
            throw std::runtime_error("The server did not return valid JSON.");
        return json;
    }

    HttpTransport curlTransport() {
        return [](const std::string &url, const std::string &bearerToken) {
            std::call_once(CurlGlobalInitFlag, []() { curl_global_init(CURL_GLOBAL_ALL); });

            HttpResponse response;
            std::unique_ptr<CURL, CurlDeleter> connection(curl_easy_init());
            if (!connection)
                throw SysMLv2::API::EXCEPTIONS::NotEnoughMemoryError();

            std::unique_ptr<curl_slist, CurlSlistDeleter> headerList;
            auto appendHeader = [&headerList](const std::string &header) {
                curl_slist *extended = curl_slist_append(headerList.get(), header.c_str());
                if (!extended)
                    throw SysMLv2::API::EXCEPTIONS::NotEnoughMemoryError();
                headerList.release();
                headerList.reset(extended);
            };
            appendHeader("Accept: application/json");
            if (!bearerToken.empty())
                appendHeader("Authorization: Bearer " + bearerToken);

            CURL *handle = connection.get();
            curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
            curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
            curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, appendToString);
            curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response.Body);
            curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, appendToString);
            curl_easy_setopt(handle, CURLOPT_HEADERDATA, &response.Headers);
            curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headerList.get());
            curl_easy_setopt(handle, CURLOPT_SSL_OPTIONS, (long) CURLSSLOPT_NATIVE_CA);
            // The token must not follow a redirect to another server.
            curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
            curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(handle, CURLOPT_TIMEOUT, 120L);
            curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);

            const CURLcode result = curl_easy_perform(handle);
            if (result != CURLE_OK) {
                throw SysMLv2::API::EXCEPTIONS::ConnectionError(
                    static_cast<SysMLv2::API::EXCEPTIONS::CONNECTION_ERROR_TYPE>(result));
            }
            curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response.Status);
            return response;
        };
    }
}
