// Native lyrics parsing. Protocol behavior adapted from WXRIW/Lyricify-Lyrics-Helper,
// Apache-2.0; see source/third_party/lyricify. All times in seconds.
#include "core.h"
#include "lyrics_native.h"
#include "../third_party/miniz/miniz.h"
#include <wincrypt.h>
#include <xmllite.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <charconv>

namespace wi {
namespace {
constexpr size_t limit = 8*1024*1024;
using namespace winrt::Windows::Data::Json;
std::wstring clean(std::wstring s) {
    auto b=s.find_first_not_of(L" \t\r\n\ufeff");
    return b==s.npos?L"":s.substr(b,s.find_last_not_of(L" \t\r\n")-b+1);
}
bool number(std::wstring_view s,double& out) {
    if(s.empty()||s.size()>24)return false;
    std::wstring copy(s); wchar_t* end=nullptr; out=wcstod(copy.c_str(),&end);
    return end==copy.data()+copy.size() && std::isfinite(out) && std::abs(out)<86400000;
}
double clockTime(std::wstring s) {
    double v=0,total=0;
    if(s.ends_with(L"ms"))return number(s.substr(0,s.size()-2),v)?v/1000:-1;
    if(s.ends_with(L"s"))s.pop_back();
    size_t p=0;
    for(;;){auto e=s.find(L':',p);if(!number(std::wstring_view(s).substr(p,e==s.npos?e:e-p),v)||v<0)return -1;
        total=total*60+v;if(e==s.npos)break;p=e+1;}
    return total<=86400?total:-1;
}
bool pairTime(std::wstring_view s,double &a,double &b) {
    auto p=s.find(L',');if(p==s.npos)return false;auto e=s.find(L',',p+1);
    return number(s.substr(0,p),a)&&number(s.substr(p+1,e==s.npos?e:e-p-1),b)&&a>=0&&b>=0;
}
void finish(LyricsDocument& d) {
    std::stable_sort(d.lines.begin(),d.lines.end(),[](auto& a,auto& b){return a.start<b.start;});
    for(size_t i=0;i<d.lines.size();++i){auto& l=d.lines[i];
        if(l.end<=l.start)l.end=i+1<d.lines.size()?std::max(l.start,d.lines[i+1].start):l.start+8;
        d.characterSynchronized|=!l.syllables.empty(); d.hasTranslation|=!l.translation.empty();d.hasRomanization|=!l.romanization.empty();
    }
}
std::string inflateLyrics(std::string const& data) {
    if(data.empty()||data.size()>limit)return {};
    mz_stream z{};z.next_in=(const unsigned char*)data.data();z.avail_in=(unsigned)data.size();
    if(mz_inflateInit(&z)!=MZ_OK)return {};
    std::string out;char block[16384];int status=MZ_OK;
    while(status==MZ_OK){z.next_out=(unsigned char*)block;z.avail_out=sizeof(block);status=mz_inflate(&z,MZ_NO_FLUSH);
        auto count=sizeof(block)-z.avail_out;if(out.size()+count>limit){status=MZ_DATA_ERROR;break;}out.append(block,count);}
    mz_inflateEnd(&z);if(status!=MZ_STREAM_END)return {};
    if(out.starts_with("\xef\xbb\xbf"))out.erase(0,3);return out;
}
void metadata(LyricsDocument &d,const std::wstring& text) {
    std::wistringstream in(text);std::wstring s;
    while(std::getline(in,s)) {s=clean(s);if(s.size()<4||s[0]!=L'[')continue;auto p=s.find(L':'),e=s.find(L']');if(p==s.npos||e==s.npos||p>e)continue;
        auto k=s.substr(1,p-1),v=s.substr(p+1,e-p-1);double n=0;
        if(k==L"offset"&&number(v,n))d.offsetMs=(int)n;
        if(k==L"ti")d.metadata.title=v;if(k==L"ar")d.metadata.artist=v;if(k==L"al")d.metadata.album=v;
    }
}
std::optional<LyricsDocument> timed(std::wstring text,std::wstring format) {
    LyricsDocument d;d.format=format;metadata(d,text);double offset=d.offsetMs/1000.;
    std::wistringstream in(text);std::wstring s;std::string language;
    while(std::getline(in,s)) {
        s=clean(s);if(s.empty())continue;if(s.starts_with(L"[language:")&&s.back()==L']'){language=utf8(s.substr(10,s.size()-11));continue;}
        auto close=s.find(L']');if(s[0]!=L'['||close==s.npos)continue;
        double a,b;if(!pairTime(std::wstring_view(s).substr(1,close-1),a,b))continue;
        LyricsLine l;l.start=std::max(0.,a/1000-offset);l.end=std::max(l.start,(a+b)/1000-offset);
        auto body=s.substr(close+1);
        // QRC stores the word before its (absolute start,duration) marker;
        // KRC/YRC store the marker before the word.  Keep these grammars
        // separate so a malformed marker cannot discard the rest of a line.
        auto addSyllable=[&](double start,double duration,std::wstring word){
            if(word.empty()||!std::isfinite(start)||!std::isfinite(duration)||start<0||duration<0)return;
            if(l.syllables.size()>=10000)throw std::runtime_error("lyrics syllable limit");
            double begin=start/1000-offset,end=(start+duration)/1000-offset;
            begin=std::max(0.,begin);end=std::max(begin,end);l.text+=word;l.syllables.push_back({begin,end,std::move(word)});
        };
        if(format==L"QRC"){
            size_t textStart=0,marker=body.find(L'(');
            while(marker!=body.npos){
                auto closeWord=body.find(L')',marker+1);if(closeWord==body.npos)break;
                double start,duration;
                if(pairTime(std::wstring_view(body).substr(marker+1,closeWord-marker-1),start,duration))
                    addSyllable(start,duration,body.substr(textStart,marker-textStart));
                textStart=closeWord+1;marker=body.find(L'(',textStart);
            }
        }else{
            wchar_t open=format==L"KRC"?L'<':L'(',endMark=format==L"KRC"?L'>':L')';
            size_t marker=body.find(open);
            while(marker!=body.npos){
                auto closeWord=body.find(endMark,marker+1);if(closeWord==body.npos)break;
                auto next=body.find(open,closeWord+1);
                double start,duration;
                if(pairTime(std::wstring_view(body).substr(marker+1,closeWord-marker-1),start,duration)){
                    if(format==L"KRC")start+=a;
                    addSyllable(start,duration,body.substr(closeWord+1,next==body.npos?body.npos:next-closeWord-1));
                }
                marker=next;
            }
        }
        if(!l.text.empty())d.lines.push_back(std::move(l));if(d.lines.size()>10000)return {};
    }
    if(!language.empty())try {auto o=JsonObject::Parse(wide(lyricsBase64(language,true)));
        for(auto v:o.GetNamedArray(L"content")){auto track=v.GetObject();int type=(int)track.GetNamedNumber(L"type",-1);if(type!=0&&type!=1)continue;auto lines=track.GetNamedArray(L"lyricContent");
            for(uint32_t i=0;i<lines.Size()&&i<d.lines.size();++i){std::wstring t;for(auto part:lines.GetAt(i).GetArray()){if(type==0&&!t.empty())t+=L" ";t+=part.GetString();}
                (type==0?d.lines[i].romanization:d.lines[i].translation)=t;}}
    }catch(...){} // Auxiliary corruption must not discard valid primary lyrics.
    finish(d);return d.lines.empty()?std::nullopt:std::optional(std::move(d));
}
void findXml(const LyricsXml& n,const std::function<void(const LyricsXml&)>& fn){fn(n);for(auto& c:n.children)findXml(c,fn);}
}
std::string lyricsBase64(const std::string& data,bool decode){
    if(data.empty()||data.size()>limit*2)return {};DWORD n=0;
    if(decode){if(!CryptStringToBinaryA(data.data(),(DWORD)data.size(),CRYPT_STRING_BASE64,nullptr,&n,nullptr,nullptr)||n>limit)return {};std::string out(n,'\0');if(!CryptStringToBinaryA(data.data(),(DWORD)data.size(),CRYPT_STRING_BASE64,(BYTE*)out.data(),&n,nullptr,nullptr))return {};out.resize(n);return out;}
    if(!CryptBinaryToStringA((const BYTE*)data.data(),(DWORD)data.size(),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&n))return {};std::string out(n,'\0');if(!CryptBinaryToStringA((const BYTE*)data.data(),(DWORD)data.size(),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,out.data(),&n))return {};out.resize(n);while(!out.empty()&&!out.back())out.pop_back();return out;
}
std::optional<std::string> decryptKrc(const std::string& raw){try{auto bytes=raw.starts_with("krc1")?raw:lyricsBase64(raw,true);if(bytes.size()<5||!bytes.starts_with("krc1"))return {};bytes.erase(0,4);
    constexpr unsigned char key[]={0x40,0x47,0x61,0x77,0x5e,0x32,0x74,0x47,0x51,0x36,0x31,0x2d,0xce,0xd2,0x6e,0x69};for(size_t i=0;i<bytes.size();++i)bytes[i]^=key[i%16];auto out=inflateLyrics(bytes);return out.empty()?std::nullopt:std::optional(out);}catch(...){return {};}}
std::optional<std::string> decryptQrc(const std::string& hex){try{if(hex.empty()||hex.size()>limit*2||hex.size()%16)return {};std::string bytes;bytes.reserve(hex.size()/2);
    auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};for(size_t i=0;i<hex.size();i+=2){int a=digit(hex[i]),b=digit(hex[i+1]);if(a<0||b<0)return {};bytes.push_back((char)((a<<4)|b));}
    auto out=inflateLyrics(qrcTransform(bytes,false));return out.empty()?std::nullopt:std::optional(out);}catch(...){return {};}}

std::wstring LyricsXml::attribute(const wchar_t* key) const {for(auto& [k,v]:attributes)if(k==key)return v;return {};}
std::wstring LyricsXml::text() const {std::wstring out=value;for(auto& c:children)out+=c.text();return out;}
std::optional<LyricsXml> readLyricsXml(const std::string& data){try{
    if(data.empty()||data.size()>limit)return {};ComPtr<IStream> stream;stream.Attach(SHCreateMemStream((const BYTE*)data.data(),(UINT)data.size()));ComPtr<IXmlReader> reader;
    if(!stream||FAILED(CreateXmlReader(__uuidof(IXmlReader),(void**)reader.GetAddressOf(),nullptr)))return {};
    reader->SetProperty(XmlReaderProperty_DtdProcessing,DtdProcessing_Prohibit);reader->SetProperty(XmlReaderProperty_MaxElementDepth,64);if(FAILED(reader->SetInput(stream.Get())))return {};
    LyricsXml root;std::vector<LyricsXml*> stack{&root};XmlNodeType type;HRESULT hr;size_t count=0;
    while((hr=reader->Read(&type))==S_OK){if(++count>100000)return {};const wchar_t* p=nullptr;UINT length=0;
        if(type==XmlNodeType_Element){reader->GetLocalName(&p,&length);LyricsXml node;node.name.assign(p,length);bool empty=reader->IsEmptyElement();
            if(reader->MoveToFirstAttribute()==S_OK)do{const wchar_t *name,*v;UINT n,m;reader->GetLocalName(&name,&n);reader->GetValue(&v,&m);node.attributes.emplace_back(std::wstring(name,n),std::wstring(v,m));}while(reader->MoveToNextAttribute()==S_OK);
            reader->MoveToElement();stack.back()->children.push_back(std::move(node));if(!empty)stack.push_back(&stack.back()->children.back());
        }else if(type==XmlNodeType_EndElement){if(stack.size()<2)return {};stack.pop_back();}
        // XmlLite exposes Text/CDATA/Whitespace on the Windows SDK used by
        // this branch; SignificantWhitespace is not a portable XmlLite enum
        // and is already represented by Whitespace when validation is off.
        else if(type==XmlNodeType_Text||type==XmlNodeType_CDATA||type==XmlNodeType_Whitespace){reader->GetValue(&p,&length);LyricsXml node;node.value.assign(p,length);stack.back()->children.push_back(std::move(node));}
    }if(FAILED(hr)||stack.size()!=1)return {};return root;
}catch(...){return {};}}

void mergeLyricsTrack(LyricsDocument& primary,const LyricsDocument& auxiliary,bool roma){
    for(auto& line:primary.lines){auto best=auxiliary.lines.end();double distance=.081;for(auto it=auxiliary.lines.begin();it!=auxiliary.lines.end();++it){double d=std::abs(it->start-line.start);if(d<distance){distance=d;best=it;}}
        if(best!=auxiliary.lines.end())(roma?line.romanization:line.translation)=best->text;
    }finish(primary);
}
std::optional<LyricsDocument> parseTtmlLyrics(const std::string& raw){try{
    auto xml=readLyricsXml(raw);if(!xml)return {};LyricsDocument d;d.format=L"TTML";
    std::map<std::wstring,size_t> keys;
    std::function<void(const LyricsXml&,LyricsLine&,double,double)> collect;
    collect=[&](const LyricsXml& n,LyricsLine& line,double begin,double end){
        auto role=n.attribute(L"role");double b=clockTime(n.attribute(L"begin")),e=clockTime(n.attribute(L"end"));if(b<0)b=begin;if(e<0){auto duration=clockTime(n.attribute(L"dur"));e=duration>=0?b+duration:end;}
        if(role==L"x-translation"){line.translation+=n.text();return;}if(role==L"x-roman"){line.romanization+=n.text();return;}
        if(role==L"x-bg"){LyricsLine bg;bg.start=b;bg.end=e;for(auto& c:n.children)collect(c,bg,b,e);line.background.push_back(std::move(bg));return;}
        if(n.name.empty()){line.text+=n.value;return;}if(n.name==L"br"){line.text+=L"\n";return;}
        bool leaf=std::none_of(n.children.begin(),n.children.end(),[](auto& c){return !c.name.empty();});
        if(n.name==L"span"&&leaf&&!n.attribute(L"begin").empty()){auto text=n.text();line.syllables.push_back({b,std::max(b,e),text});line.text+=text;return;}
        for(auto& c:n.children)collect(c,line,b,e);
    };
    findXml(*xml,[&](const LyricsXml& n){if(n.name==L"tt")d.language=n.attribute(L"lang");if(n.name!=L"p")return;auto b=clockTime(n.attribute(L"begin"));if(b<0)return;LyricsLine line;line.start=b;line.end=clockTime(n.attribute(L"end"));auto duration=clockTime(n.attribute(L"dur"));if(line.end<0&&duration>=0)line.end=b+duration;line.agent=n.attribute(L"agent");for(auto& c:n.children)collect(c,line,b,line.end);line.text=clean(line.text);if(!line.text.empty()){keys[n.attribute(L"key")]=d.lines.size();d.lines.push_back(std::move(line));}});
    findXml(*xml,[&](const LyricsXml& n){if(n.name!=L"translation"&&n.name!=L"transliteration")return;bool roman=n.name==L"transliteration";findXml(n,[&](const LyricsXml& t){auto key=t.attribute(L"for");if(!key.empty()&&keys.contains(key)){auto value=clean(t.text());(roman?d.lines[keys[key]].romanization:d.lines[keys[key]].translation)=value;}});});
    finish(d);d.original=raw;return d.lines.empty()?std::nullopt:std::optional(std::move(d));
}catch(...){return {};}}
std::optional<LyricsDocument> parseNativeLyrics(const std::string& raw,const std::string& fmt){try{
    if(raw.empty()||raw.size()>limit)return {};auto f=fmt;std::transform(f.begin(),f.end(),f.begin(),[](unsigned char c){return (char)tolower(c);});
    if(f=="ttml"||f=="xml")return parseTtmlLyrics(raw);
    if(f=="krc"||f=="qrc"){auto plain=f=="krc"?decryptKrc(raw):decryptQrc(raw);if(!plain)return {};auto d=parseNativeLyrics(*plain,f+"-text");if(d)d->original=raw;return d;}
    auto text=wide(raw);if(text.size()&&text[0]==0xfeff)text.erase(0,1);
    if(f=="qrc-text"&&text.find(L'<')!=text.npos){auto x=readLyricsXml(raw);if(!x)return {};std::wstring content;findXml(*x,[&](auto& n){if(n.name==L"Lyric_1")content=n.attribute(L"LyricContent");});if(!content.empty())text=content;}
    if(f=="krc-text"||f=="qrc-text"||f=="yrc"){auto d=timed(text,f=="krc-text"?L"KRC":f=="qrc-text"?L"QRC":L"YRC");if(d)d->original=raw;return d;}
    LyricsDocument d;d.format=L"LRC";metadata(d,text);auto legacy=Lyrics::parse(raw); // reuse the existing strict LRC grammar and offset convention
    if(!legacy)return {};for(auto& [t,s]:legacy->lines)d.lines.push_back({t,0,s});if(d.lines.empty()){if(text.find(L'<')!=text.npos||text.find(L'{')!=text.npos)return {};d.plain=clean(text);d.format=L"TEXT";if(d.plain.empty())return {};}
    finish(d);d.original=raw;return d;
}catch(...){return {};}}
std::optional<LyricsDocument> parseProviderLyrics(const std::string& raw,const std::wstring& provider){try{
    using namespace winrt::Windows::Data::Json;auto o=JsonObject::Parse(wide(raw));std::optional<LyricsDocument> result;
    if(provider==L"netease"){
        auto track=[&](const wchar_t* name,const char* fmt){auto obj=o.GetNamedObject(name,JsonObject());return parseNativeLyrics(utf8(obj.GetNamedString(L"lyric",L"").c_str()),fmt);};
        result=track(L"yrc","yrc");bool word=bool(result);if(!result)result=track(L"lrc","lrc");if(!result)return {};
        auto tr=track(word?L"ytlrc":L"tlyric","lrc");if(!tr)tr=track(L"tlyric","lrc");auto ro=track(word?L"yromalrc":L"romalrc","lrc");if(!ro)ro=track(L"romalrc","lrc");if(tr)mergeLyricsTrack(*result,*tr,false);if(ro)mergeLyricsTrack(*result,*ro,true);result->degradedToLines=!word;
    }else if(provider==L"apple_music"){
        std::function<void(IJsonValue,int)> visit=[&](IJsonValue v,int depth){if(result||depth>32)return;if(v.ValueType()==JsonValueType::Object){for(auto kv:v.GetObject()){if(kv.Key()==L"ttml"&&kv.Value().ValueType()==JsonValueType::String)result=parseTtmlLyrics(utf8(kv.Value().GetString().c_str()));else visit(kv.Value(),depth+1);}}else if(v.ValueType()==JsonValueType::Array)for(auto x:v.GetArray())visit(x,depth+1);};visit(o,0);
    }if(result)result->provider=provider;return result;
}catch(...){return {};}}
} // namespace wi
