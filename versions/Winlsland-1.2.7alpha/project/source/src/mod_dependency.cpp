#include "mod_package.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
namespace wi {
bool modSafeId(const std::string& s){return !s.empty()&&s.size()<=80&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c<128&&(std::isalnum(c)||c=='-'||c=='_');});}
std::string modTrim(const std::string& text){auto s=wide(text);auto a=s.find_first_not_of(L" \t\r\n\v\f\u00a0\u3000");return a==s.npos?"":utf8(s.substr(a,s.find_last_not_of(L" \t\r\n\v\f\u00a0\u3000")-a+1));}
namespace {
using namespace winrt::Windows::Data::Json;
std::string field(const JsonObject& j,const wchar_t* key,const char* fallback=""){
    if(!j.HasKey(key)||j.GetNamedValue(key).ValueType()!=JsonValueType::String)return fallback;
    auto s=modTrim(utf8(j.GetNamedString(key).c_str()));return s.empty()?fallback:s;
}
struct ParsedVersion {std::array<unsigned,3> numbers{};std::string pre;};
ParsedVersion parseVersion(std::string s){
    ParsedVersion out;size_t p=0;
    auto build=s.find('+');if(build!=s.npos&&(build+1==s.size()||!std::all_of(s.begin()+build+1,s.end(),[](unsigned char c){return c<128&&(std::isalnum(c)||c=='.'||c=='-');})))throw std::runtime_error("构建版本格式错误");
    for(int part=0;part<3;++part){size_t start=p;unsigned n=0;while(p<s.size()&&s[p]>='0'&&s[p]<='9'){if(n>1000000)throw std::runtime_error("版本号过大");n=n*10+(s[p++]-'0');}if(start==p)throw std::runtime_error("版本号格式错误");out.numbers[part]=n;if(part<2&&(p>=s.size()||s[p++]!='.'))throw std::runtime_error("版本号必须含主次修订号");}
    if(p<s.size()&&s[p]=='-')++p;
    if(p<s.size()&&s[p]!='+')out.pre=s.substr(p,s.find('+',p)-p);
    if(!out.pre.empty()&&!std::all_of(out.pre.begin(),out.pre.end(),[](unsigned char c){return c<128&&(std::isalnum(c)||c=='.'||c=='-');}))throw std::runtime_error("预发布版本格式错误");
    return out;
}
int compare(std::string a,std::string b){
    auto x=parseVersion(a),y=parseVersion(b);if(x.numbers!=y.numbers)return x.numbers<y.numbers?-1:1;
    if(x.pre==y.pre)return 0;if(x.pre.empty())return 1;if(y.pre.empty())return -1;
    size_t i=0,j=0;while(i<x.pre.size()&&j<y.pre.size()){
        if(std::isdigit((unsigned char)x.pre[i])&&std::isdigit((unsigned char)y.pre[j])){unsigned long long nx=0,ny=0;while(i<x.pre.size()&&std::isdigit((unsigned char)x.pre[i])){nx=std::min(1000000000ull,nx*10+(x.pre[i++]-'0'));}while(j<y.pre.size()&&std::isdigit((unsigned char)y.pre[j])){ny=std::min(1000000000ull,ny*10+(y.pre[j++]-'0'));}if(nx!=ny)return nx<ny?-1:1;}
        else {if(x.pre[i]!=y.pre[j])return x.pre[i]<y.pre[j]?-1:1;++i;++j;}
    }return i==x.pre.size()?(j==y.pre.size()?0:-1):1;
}
}
bool modVersionMatches(const std::string& version,const std::string& range){
    if(range.empty()||range=="*")return true;
    try{
        std::istringstream in(range);std::string token;bool any=false;
        while(in>>token){std::string op="=";size_t p=0;while(p<token.size()&&(token[p]=='<'||token[p]=='>'||token[p]=='='))++p;if(p){op=token.substr(0,p);token=token.substr(p);}if(token.empty()&&!(in>>token))return false;
            int c=compare(version,token);bool ok=op=="="||op=="=="?c==0:op==">="?c>=0:op=="<="?c<=0:op==">"?c>0:op=="<"?c<0:false;if(!ok)return false;any=true;
        }return any;
    }catch(...){return false;}
}
void parseModManifest(const std::string& bytes,ModManifest& out){
    try{
        auto j=JsonObject::Parse(wide(bytes));out.name=field(j,L"name","未命名模组");out.description=field(j,L"description","---");out.author=field(j,L"author","作者并未填写");out.version=field(j,L"version","版本号未填写");
        out.id=field(j,L"id");if(!modSafeId(out.id))throw std::runtime_error("模组 ID 无效：需要 ASCII 字母、数字、短横线或下划线");
        if(!j.HasKey(L"apiVersion")||j.GetNamedValue(L"apiVersion").ValueType()!=JsonValueType::Number||j.GetNamedNumber(L"apiVersion")!=1)throw std::runtime_error("API 版本不兼容（需要 1）");
        if(out.version!="版本号未填写")parseVersion(out.version);
        out.entry=field(j,L"entry");if(out.entry.empty())throw std::runtime_error("缺少入口 DLL");
        out.gameVersion=field(j,L"gameVersion");if(!out.gameVersion.empty()&&!modVersionMatches(utf8(Version),out.gameVersion))throw std::runtime_error("WinIsland 版本不满足 gameVersion："+out.gameVersion);
        if(j.HasKey(L"dependencies")){
            if(j.GetNamedValue(L"dependencies").ValueType()!=JsonValueType::Array)throw std::runtime_error("dependencies 必须为数组");
            for(auto dep:j.GetNamedArray(L"dependencies")){
                std::string id,range;
                if(dep.ValueType()==JsonValueType::String)id=modTrim(utf8(dep.GetString().c_str()));
                else if(dep.ValueType()==JsonValueType::Object){id=field(dep.GetObject(),L"id");range=field(dep.GetObject(),L"version");}
                else throw std::runtime_error("依赖项必须是 ID 字符串或 id/version 对象");
                if(!modSafeId(id)||id==out.id||out.dependencyVersions.contains(id))throw std::runtime_error("依赖 ID 无效、自依赖或重复");
                out.dependencies.push_back(id);out.dependencyVersions[id]=range;
            }
        }
    }catch(const winrt::hresult_error&){throw std::runtime_error("mod.json 格式错误或字段类型错误");}
}
}

