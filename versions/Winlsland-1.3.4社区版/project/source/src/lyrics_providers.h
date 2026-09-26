#pragma once
#include "core.h"
namespace wi {
enum class LyricsError {none,network,timeout,limited,notFound,empty,parse,decrypt,tokenMissing,tokenInvalid,unavailable,cancelled};
const wchar_t* lyricsErrorText(LyricsError);
struct LyricsHttpRequest {std::wstring url,method=L"GET",headers;std::string body;int timeoutMs=6000;};
struct LyricsHttpResponse {unsigned status=0;std::string body;LyricsError error=LyricsError::none;int retries=0;};
using LyricsTransport=std::function<LyricsHttpResponse(const LyricsHttpRequest&,const std::function<bool()>&)>;
LyricsHttpResponse lyricsHttp(const LyricsHttpRequest&,const std::function<bool()>&);
struct LyricsProviderInfo {std::wstring id,name,formats;bool syllables,translation,romanization;int priority;};
struct LyricsCredentials {std::wstring mediaToken,accessToken,storefront=L"us",language=L"en-US";};
struct LyricsRequest {const Music& song;uint64_t requestId;std::function<bool()> valid;LyricsCredentials credentials;};
struct LyricsCandidate {LyricsMetadata metadata;std::wstring id,auxiliary;};
struct LyricsResponse {std::optional<LyricsDocument> document;LyricsError error=LyricsError::none;bool cacheHit=false;};
class LyricsProviderRegistry {
    fs::path store;
    LyricsTransport transport;
    std::map<std::wstring,double> backoff;
    LyricsHttpResponse get(const LyricsHttpRequest&,const LyricsRequest&);
    std::vector<LyricsCandidate> search(const std::wstring&,const LyricsRequest&,LyricsError&);
    LyricsResponse fetch(const std::wstring&,const LyricsCandidate&,const LyricsRequest&);
public:
    explicit LyricsProviderRegistry(fs::path path,LyricsTransport network=lyricsHttp):store(std::move(path)),transport(std::move(network)){}
    static const std::vector<LyricsProviderInfo>& providers();
    static std::vector<std::wstring> order(const Music&,int mode);
    LyricsResponse request(const LyricsRequest&,int mode);
    void clear(const Music&);
};
std::shared_ptr<Lyrics> projectLyrics(LyricsDocument);
}
