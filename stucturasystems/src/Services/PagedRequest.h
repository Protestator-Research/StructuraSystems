//---------------------------------------------------------
// Pagination of the list routes of the SysML v2 API (REST/HTTP-PSM).
//---------------------------------------------------------
#pragma once
//---------------------------------------------------------
// External Classes
//---------------------------------------------------------
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace StructuraSystems::Client {
    /**
     * The answer of one GET request.
     */
    struct HttpResponse {
        long Status = 0;
        std::string Body;
        /** The raw header block, one "Name: value" line per header (as collected by libcurl). */
        std::string Headers;
    };

    /**
     * Sends one GET request. Is replaced by a fake in the tests.
     * Implementations throw SysMLv2::API::EXCEPTIONS::ConnectionError if the server cannot be reached; a status outside
     * of 2xx can be returned, getAllPages and getJson turn it into a SysMLv2::API::EXCEPTIONS::HTTPException.
     * @param url The complete URL.
     * @param bearerToken The token for the Authorization header, empty for no header.
     */
    using HttpTransport = std::function<HttpResponse(const std::string &url, const std::string &bearerToken)>;

    /**
     * Reads the target of the Link header (RFC 8288) with rel="next" from a raw header block.
     * Handles several links in one header, several Link headers, a case-insensitive header name, quoted and unquoted rel
     * values and rel values with several relations ("next last"). If the block contains several responses (e.g. after
     * "100 Continue"), only the last one counts.
     * @param headers The raw header block.
     * @return The URL of the next page, std::nullopt if there is none.
     */
    std::optional<std::string> nextPageUrl(const std::string &headers);

    /**
     * Value of a header (case-insensitive name, trimmed) of the last response of a raw header block.
     * @return The value, empty if the header is missing.
     */
    std::string headerValue(const std::string &headers, const std::string &name);

    /**
     * Creates the URL of the following page. The server writes absolute URLs, but the token must only be sent to the
     * server the client is connected to, and a reverse proxy can change host and path. Thus only the query of the link
     * (the cursor) is taken and appended to the URL of the request that was just answered.
     * @param requestUrl The URL that was just requested.
     * @param linkTarget The target of rel="next".
     * @return The URL of the next request, std::nullopt if the link has no query.
     */
    std::optional<std::string> followLink(const std::string &requestUrl, const std::string &linkTarget);

    /**
     * Joins the server address and an endpoint ("projects", "projects/{id}/commits?branchId=...") to a URL.
     */
    std::string buildUrl(const std::string &serverAddress, const std::string &endpoint);

    /**
     * Downloads a list route completely: the first page and all pages that are linked with rel="next".
     * @param firstUrl The URL of the first page.
     * @param bearerToken Token for the Authorization header.
     * @param transport Sends the requests.
     * @return The entries of all pages in the order of the server.
     * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx.
     * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable (thrown by the transport).
     * @throws std::runtime_error If a page is no JSON array or the server links to a page that was requested before.
     */
    std::vector<nlohmann::json> getAllPages(const std::string &firstUrl, const std::string &bearerToken, const HttpTransport &transport);

    /**
     * Downloads a single resource and parses it.
     * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx.
     * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
     * @throws std::runtime_error If the answer is no JSON.
     */
    nlohmann::json getJson(const std::string &url, const std::string &bearerToken, const HttpTransport &transport);

    /**
     * The transport that uses libcurl, with the same settings as the SysMLAPIImplementation of the sysmllib.
     */
    HttpTransport curlTransport();
}
