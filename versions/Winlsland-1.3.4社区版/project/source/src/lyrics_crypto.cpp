// Protocol constants and framing from WXRIW/Lyricify-Lyrics-Helper (Apache-2.0).
// Native AES/RSA/MD5 implementation uses Windows CNG, credentials use user DPAPI.
#include "core.h"
#include "lyrics_native.h"
#include "lyrics_crypto.h"
#include <bcrypt.h>
#include <wincrypt.h>
namespace wi { namespace {
struct Alg {BCRYPT_ALG_HANDLE h{};~Alg(){if(h)BCryptCloseAlgorithmProvider(h,0);}};
struct Key {BCRYPT_KEY_HANDLE h{};~Key(){if(h)BCryptDestroyKey(h);}};
void check(NTSTATUS s){if(s<0)throw std::runtime_error("lyrics crypto failed");}
std::string hex(std::string_view s,bool upper=false){const char* digits=upper?"0123456789ABCDEF":"0123456789abcdef";std::string out;for(unsigned char c:s){out+=digits[c>>4];out+=digits[c&15];}return out;}
std::string unhex(const std::string& s){std::string out;for(size_t i=0;i<s.size();i+=2)out+=(char)std::stoi(s.substr(i,2),nullptr,16);return out;}
std::string aes(const std::string& text,const std::string& secret,bool cbc){Alg alg;Key key;check(BCryptOpenAlgorithmProvider(&alg.h,BCRYPT_AES_ALGORITHM,nullptr,0));
    check(BCryptSetProperty(alg.h,BCRYPT_CHAINING_MODE,(PUCHAR)(cbc?BCRYPT_CHAIN_MODE_CBC:BCRYPT_CHAIN_MODE_ECB),cbc?sizeof(BCRYPT_CHAIN_MODE_CBC):sizeof(BCRYPT_CHAIN_MODE_ECB),0));
    check(BCryptGenerateSymmetricKey(alg.h,&key.h,nullptr,0,(PUCHAR)secret.data(),(ULONG)secret.size(),0));std::string iv="0102030405060708",out(text.size()+16,'\0');ULONG n=0;
    check(BCryptEncrypt(key.h,(PUCHAR)text.data(),(ULONG)text.size(),nullptr,cbc?(PUCHAR)iv.data():nullptr,cbc?16:0,(PUCHAR)out.data(),(ULONG)out.size(),&n,BCRYPT_BLOCK_PADDING));out.resize(n);return out;
}
std::string md5(const std::string& text){Alg alg;check(BCryptOpenAlgorithmProvider(&alg.h,BCRYPT_MD5_ALGORITHM,nullptr,0));std::string hash(16,'\0');check(BCryptHash(alg.h,nullptr,0,(PUCHAR)text.data(),(ULONG)text.size(),(PUCHAR)hash.data(),16));return hex(hash);}
std::string form(std::string s){std::string out;for(unsigned char c:s){if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')out+=c;else out+="%"+hex(std::string(1,(char)c),true);}return out;}
}
std::string neteaseEapi(const std::string& path,const std::string& json){return "params="+hex(aes(path+"-36cd479b6b5-"+json+"-36cd479b6b5-"+md5("nobody"+path+"use"+json+"md5forencrypt"),"e82ckenh8dichen8",false),true);}
std::string neteaseWeapi(const std::string& json,const std::string& testKey){
    std::string secret=testKey;if(secret.empty()){secret.resize(16);check(BCryptGenRandom(nullptr,(PUCHAR)secret.data(),16,BCRYPT_USE_SYSTEM_PREFERRED_RNG));const char* alphabet="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";for(auto& c:secret)c=alphabet[(unsigned char)c%62];}if(secret.size()!=16)throw std::invalid_argument("WeAPI key length");
    auto params=lyricsBase64(aes(lyricsBase64(aes(json,"0CoJUm6Qyw8W8jud",true)),secret,true));
    auto modulus=unhex("e0b509f6259df8642dbc35662901477df22677ec152b5ff68ace615bb7b725152b3ab17a876aea8a5aa76d2e417629ec4ee341f56135fccf695280104e0312ecbda92557c93870114af6c9d05c4f7f0c3685b7a46bee255932575cce10b424d813cfe4875d3e82047b97ddef52741d546b8e289dc6935b3ece0462db0a22b8e7");
    BCRYPT_RSAKEY_BLOB header{BCRYPT_RSAPUBLIC_MAGIC,1024,3,128,0,0};std::string blob((char*)&header,sizeof(header));blob.append("\1\0\1",3);blob+=modulus;Alg alg;Key key;check(BCryptOpenAlgorithmProvider(&alg.h,BCRYPT_RSA_ALGORITHM,nullptr,0));check(BCryptImportKeyPair(alg.h,nullptr,BCRYPT_RSAPUBLIC_BLOB,&key.h,(PUCHAR)blob.data(),(ULONG)blob.size(),0));std::reverse(secret.begin(),secret.end());std::string input(112,'\0');input+=secret;std::string out(128,'\0');ULONG count=0;
    check(BCryptEncrypt(key.h,(PUCHAR)input.data(),128,nullptr,nullptr,0,(PUCHAR)out.data(),128,&count,BCRYPT_PAD_NONE));return "params="+form(params)+"&encSecKey="+hex(out);
}
std::wstring protectLyricsSecret(const std::wstring& value){if(value.empty())return {};auto s=utf8(value);if(s.size()>8192||s.find_first_of("\r\n")!=s.npos)throw std::invalid_argument("invalid credential");DATA_BLOB in{(DWORD)s.size(),(BYTE*)s.data()},out{};if(!CryptProtectData(&in,L"WinIsland lyrics",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))throw std::runtime_error("DPAPI protect failed");auto encoded=lyricsBase64(std::string((char*)out.pbData,out.cbData));SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);SecureZeroMemory(s.data(),s.size());return wide(encoded);}
std::wstring unprotectLyricsSecret(const std::wstring& value){auto s=lyricsBase64(utf8(value),true);if(s.empty())return {};DATA_BLOB in{(DWORD)s.size(),(BYTE*)s.data()},out{};if(!CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))return {};auto result=wide(std::string((char*)out.pbData,out.cbData));SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return result;}
}
