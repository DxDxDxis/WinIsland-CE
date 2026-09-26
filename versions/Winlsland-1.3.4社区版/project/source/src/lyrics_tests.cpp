#include "lyrics_providers.h"
#include "lyrics_crypto.h"
#include <winrt/Windows.Data.Json.h>
namespace wi {
int lyricsNativeTests(const fs::path& fixtures,const fs::path& output){
 // wWinMain has already called OleInitialize (STA). Do not call
 // winrt::init_apartment(MTA) here: RPC_E_CHANGED_MODE escapes before the
 // test body and makes the GUI-subsystem process terminate.
 std::ostringstream report;int failures=0;auto check=[&](bool ok,const char* name){report<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;};auto read=[&](const char* file){return readFile(fixtures/file);};
 try {
 auto lrc=parseNativeLyrics(read("sample.lrc"));
 check(lrc&&lrc->lines.size()==3&&lrc->lines[0].start==.5&&lrc->lines[1].start==2&&lrc->offsetMs==500,"LRC repeat tags, compatible positive offset");
 auto q=decryptQrc(read("sample.qrc.hex"));auto qExpected=read("sample.qrc.txt");qExpected.erase(std::remove(qExpected.begin(),qExpected.end(),'\r'),qExpected.end());check(q&&*q==qExpected,"QRC decrypt vs independent Python fixture");auto qrc=q?parseNativeLyrics(*q,"qrc-text"):std::nullopt;check(qrc&&qrc->lines.size()==2&&qrc->lines[0].text==L"Hello"&&qrc->lines[0].start==1&&qrc->lines[0].end==2&&qrc->lines[0].syllables.size()==2&&qrc->lines[0].syllables[1].start==1.4,"QRC absolute syllable times");
 auto krc=parseNativeLyrics(read("sample.krc.b64"),"krc");check(krc&&krc->lines.size()==2&&krc->lines[0].translation==L"你好"&&krc->lines[0].romanization==L"he llo"&&krc->lines[0].syllables[1].start==1.4,"KRC zlib, relative syllables, translation and romanization");
 auto yrc=parseNativeLyrics(read("sample.yrc"),"yrc");check(yrc&&yrc->lines.size()==2&&yrc->lines[0].text==L"Hello"&&yrc->lines[0].syllables.size()==2&&yrc->lines[0].syllables[0].text==L"Hel"&&yrc->lines[0].syllables[0].start==1&&yrc->lines[0].syllables[0].end==1.4&&yrc->lines[0].syllables[1].text==L"lo"&&yrc->lines[0].syllables[1].start==1.4&&yrc->lines[0].syllables[1].end==2&&yrc->lines[1].syllables[0].text==L"world"&&yrc->lines[1].end==3,"YRC marker order, syllable text and duration");
 auto ttml=parseTtmlLyrics(read("sample.ttml"));check(ttml&&ttml->lines.size()==2&&ttml->lines[0].text==L"Hello"&&ttml->lines[0].translation==L"你好"&&ttml->lines[0].romanization==L"he llo"&&ttml->lines[0].background.size()==1&&ttml->lines[0].syllables.size()==2&&ttml->lines[1].end==3,"TTML namespaces, words, auxiliary tracks, background, dur");
 auto net=parseProviderLyrics(read("netease.json"),L"netease");check(net&&net->provider==L"netease"&&net->format==L"YRC"&&net->hasTranslation&&net->hasRomanization,"NetEase JSON prefers YRC and merges auxiliary tracks");
 auto apple=parseProviderLyrics(read("apple.json"),L"apple_music");check(apple&&apple->provider==L"apple_music"&&apple->lines.size()==2,"Apple JSON and TTML");
 check(!parseNativeLyrics("")&&!decryptQrc("00xyz")&&!decryptKrc("AAAA")&&!parseProviderLyrics("{bad",L"netease")&&!parseNativeLyrics("[1000](1,2,0)x","yrc"),"empty, damaged, invalid JSON, incomplete times");
 check(!parseTtmlLyrics("<!DOCTYPE tt [<!ENTITY x SYSTEM 'file:///c:/Windows/win.ini'>]><tt><p begin='1s'>&x;</p></tt>"),"DTD and external entities prohibited");
 auto plain=parseNativeLyrics("unsynchronized words","text");check(plain&&plain->lines.empty()&&plain->plain==L"unsynchronized words","unsynchronized lyrics");
 auto mixed=Lyrics::parse("[00:01.00]first\n[00:01.00]translation\n[offset:-500]");check(mixed->lines.size()==1&&mixed->lines[0].first==1.5&&mixed->lines[0].second==L"first / translation","legacy mixed lyrics and negative offset preserved");
 const std::string data="{\"id\":\"1\",\"lv\":\"-1\"}";check(neteaseEapi("/api/song/lyric/v1",data)==read("eapi.expected"),"CNG EAPI AES-ECB+MD5 vs independent fixture");check(neteaseWeapi(data,"abcdefghijklmnop")==read("weapi.expected"),"CNG WeAPI AES-CBC+RSA vs independent fixture");
 auto secret=protectLyricsSecret(L"synthetic-test-token");check(secret!=L"synthetic-test-token"&&unprotectLyricsSecret(secret)==L"synthetic-test-token","DPAPI credential round trip");
 Settings settings;settings.reduceMotion=true;settings.settingsReduceMotion=false;settings.settingsDisableBlur=true;auto config=output.parent_path()/L"config-test.xml";settings.save(config);Settings restored;restored.load(config);check(restored.reduceMotion&&!restored.settingsReduceMotion&&restored.settingsDisableBlur,"settings-only animation and blur persist independently of island");
 Music song;song.title=L"Hello";song.artist=L"Tester";song.album=L"Fixture";song.duration=3;song.platform=L"QQ 音乐";check(LyricsProviderRegistry::order(song,0).front()==L"qq_music"&&LyricsProviderRegistry::order(song,9)==std::vector<std::wstring>{L"apple_music"},"deterministic automatic and manual provider order");
 int calls=0;bool valid=true;auto dir=output.parent_path()/L"cache-test";fs::create_directories(dir);
 LyricsTransport mock=[&](const LyricsHttpRequest& q,const std::function<bool()>& current){++calls;if(!current())return LyricsHttpResponse{0,{},LyricsError::cancelled};if(q.url.find(L"cloudsearch")!=q.url.npos)return LyricsHttpResponse{200,R"({"code":200,"result":{"songs":[{"id":1,"name":"Hello","ar":[{"name":"Tester"}],"al":{"name":"Fixture"},"dt":3000}]}})"};if(q.url.find(L"lyric")!=q.url.npos)return LyricsHttpResponse{200,read("netease.json")};return LyricsHttpResponse{503,{},LyricsError::unavailable};};
 LyricsProviderRegistry registry(dir,mock);LyricsRequest req{song,1,[&]{return valid;},{}};auto first=registry.request(req,3);check(first.document&&calls==2&&!first.cacheHit,"provider search, match, fetch through injected transport");calls=0;auto cached=registry.request(req,3);check(cached.document&&cached.cacheHit&&calls==0&&cached.document->lines[0].translation==L"你好"&&cached.document->lines[0].syllables.size()==2,"rich cache hit with words and auxiliary tracks");
 registry.clear(song);calls=0;registry.request(req,3);check(calls==2,"explicit cache invalidation");registry.clear(song);valid=false;calls=0;auto cancelled=registry.request(req,3);check(cancelled.error==LyricsError::cancelled&&calls==0,"cancel old generation before network");valid=true;auto noToken=registry.request(req,9);check(noToken.error==LyricsError::tokenMissing,"Apple missing token diagnostic");
 int cancellationCalls=0;LyricsProviderRegistry racing(dir,[&](const auto&,const auto&){++cancellationCalls;valid=false;return LyricsHttpResponse{200,R"({"result":{"songs":[]}})"};});auto race=racing.request(req,0);check(!race.document&&race.error==LyricsError::cancelled&&cancellationCalls==1,"superseded generation stops fallback chain");valid=true;
 int retries=0;LyricsProviderRegistry timeout(output.parent_path()/L"timeout-cache",[&](const auto&,const auto&){++retries;return LyricsHttpResponse{0,{},LyricsError::timeout};});auto timedout=timeout.request(req,3);check(timedout.error==LyricsError::timeout&&retries==2,"bounded timeout retry");
 auto start=GetTickCount64();auto immediate=lyricsHttp({L"https://example.com"},[]{return false;});check(immediate.error==LyricsError::cancelled&&GetTickCount64()-start<100,"native transport pre-cancellation");
 }catch(std::exception const& e){report<<"EXCEPTION "<<e.what()<<'\n';++failures;}catch(...){report<<"EXCEPTION unknown\n";++failures;}
 report<<"failures="<<failures<<'\n';writeAtomic(output,report.str());return failures?1:0;
}
}
