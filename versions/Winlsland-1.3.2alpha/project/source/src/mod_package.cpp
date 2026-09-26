#include "mod_package.h"
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../third_party/miniz/miniz.h"
#include <wincrypt.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
namespace wi {
namespace {
// Media skin packages: bounded in-memory archive and one expanded file at a time.
constexpr size_t MaxPackage=256*1024*1024,MaxFile=256*1024*1024,MaxExpanded=512*1024*1024;
std::wstring lower(std::wstring value){CharLowerBuffW(value.data(),(DWORD)value.size());return value;}
fs::path safeRelative(const std::string& raw){
    if(raw.empty()||raw.size()>400||raw.find('\0')!=raw.npos||raw.find('\\')!=raw.npos)throw std::runtime_error("模组文件损坏：包内路径无效");
    auto text=wide(raw);if(utf8(text)!=raw||text.find_first_of(L":<>\"|?*")!=text.npos)throw std::runtime_error("模组文件损坏：文件名编码或字符无效");
    fs::path path=text;if(path.is_absolute()||path.has_root_path())throw std::runtime_error("禁止包内绝对路径");
    int depth=0;for(auto& part:path){auto name=part.wstring();if(name.empty()||name==L"."||name==L".."||name.back()==L'.'||name.back()==L' '||++depth>16)throw std::runtime_error("禁止路径穿越、尾随点/空格或过深目录");
        for(auto c:name)if(c<32)throw std::runtime_error("文件名含控制字符");auto base=lower(name.substr(0,name.find('.')));
        if(base==L"con"||base==L"prn"||base==L"aux"||base==L"nul"||base==L"conin$"||base==L"conout$"||((base.rfind(L"com",0)==0||base.rfind(L"lpt",0)==0)&&base.size()==4&&base[3]>=L'0'&&base[3]<=L'9'))throw std::runtime_error("包内路径使用 Windows 保留设备名");
    }return path;
}
void noReparse(const fs::path& path){
    fs::path current;for(auto& part:fs::absolute(path)){current/=part;auto attr=GetFileAttributesW(current.c_str());if(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("缓存路径包含链接或重解析点");}
}
void validatePe(const std::string& bytes){
    if(bytes.size()<sizeof(IMAGE_DOS_HEADER))throw std::runtime_error("入口 DLL 损坏");IMAGE_DOS_HEADER dos{};memcpy(&dos,bytes.data(),sizeof(dos));
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||(size_t)dos.e_lfanew>bytes.size()-sizeof(DWORD)-sizeof(IMAGE_FILE_HEADER))throw std::runtime_error("入口不是有效 PE DLL");
    DWORD signature;IMAGE_FILE_HEADER file{};memcpy(&signature,bytes.data()+dos.e_lfanew,4);memcpy(&file,bytes.data()+dos.e_lfanew+4,sizeof(file));
    if(signature!=IMAGE_NT_SIGNATURE||file.Machine!=IMAGE_FILE_MACHINE_AMD64||!(file.Characteristics&IMAGE_FILE_DLL))throw std::runtime_error("入口必须是 Windows x64 DLL");
}
}
struct ModPackage::Impl {
    std::string archive;mz_zip_archive zip{};bool opened=false;
    struct Entry {mz_zip_archive_file_stat info;fs::path relative;};
    std::vector<Entry> entries;std::map<std::wstring,size_t> byName;
    ~Impl(){if(opened)mz_zip_reader_end(&zip);}
    std::string read(size_t index){auto& entry=entries.at(index);std::string out((size_t)entry.info.m_uncomp_size,'\0');if(!mz_zip_reader_extract_to_mem(&zip,entry.info.m_file_index,out.data(),out.size(),0))throw std::runtime_error("模组文件损坏：ZIP 解压或 CRC 校验失败");return out;}
    std::string read(const wchar_t* name){auto it=byName.find(lower(name));if(it==byName.end())throw std::runtime_error("包内文件缺失");return read(it->second);}
    void checkSignature(){
        bool data=byName.contains(L"signature.json"),signature=byName.contains(L"signature.p7s");
        if(!data&&!signature)return;if(!data||!signature)throw std::runtime_error("模组签名无效：签名清单与 CMS 必须同时存在");
        auto message=read(L"signature.json"),sig=read(L"signature.p7s");
        CRYPT_VERIFY_MESSAGE_PARA para{sizeof(para)};para.dwMsgAndCertEncodingType=X509_ASN_ENCODING|PKCS_7_ASN_ENCODING;
        const BYTE* parts[]={(const BYTE*)message.data()};DWORD sizes[]={(DWORD)message.size()};PCCERT_CONTEXT cert=nullptr;
        if(!CryptVerifyDetachedMessageSignature(&para,0,(const BYTE*)sig.data(),(DWORD)sig.size(),1,parts,sizes,&cert))throw std::runtime_error("模组签名无效：CMS 校验失败");
        CERT_CHAIN_PARA chainPara{sizeof(chainPara)};LPSTR usage=(LPSTR)"1.3.6.1.5.5.7.3.3";chainPara.RequestedUsage.dwType=USAGE_MATCH_TYPE_AND;chainPara.RequestedUsage.Usage={1,&usage};PCCERT_CHAIN_CONTEXT chain=nullptr;
        BOOL built=CertGetCertificateChain(nullptr,cert,nullptr,cert->hCertStore,&chainPara,CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL,nullptr,&chain);
        bool trusted=built&&chain&&chain->TrustStatus.dwErrorStatus==CERT_TRUST_NO_ERROR;
        if(chain)CertFreeCertificateChain(chain);CertFreeCertificateContext(cert);
        if(!trusted)throw std::runtime_error("模组签名无效：证书未受系统信任、用途错误或已过期");
        using namespace winrt::Windows::Data::Json;
        try{auto manifest=JsonObject::Parse(wide(message));size_t count=0;
            for(auto& e:entries){if(e.info.m_is_directory)continue;auto name=e.relative.generic_wstring();if(name==L"signature.json"||name==L"signature.p7s")continue;
                if(!manifest.HasKey(name)||manifest.GetNamedValue(name).ValueType()!=JsonValueType::String||utf8(manifest.GetNamedString(name).c_str())!=sha256(read(byName.at(lower(name)))))throw std::runtime_error("模组签名无效：文件摘要不匹配或未被签名覆盖");++count;
            }if(manifest.Size()!=count)throw std::runtime_error("模组签名无效：清单含多余文件");
        }catch(const winrt::hresult_error&){throw std::runtime_error("模组签名无效：签名清单 JSON 错误");}
    }
};
ModPackage::ModPackage(const fs::path& source):impl(std::make_unique<Impl>()),path(fs::absolute(source)){
    if(lower(path.extension().wstring())!=L".wimod")throw std::runtime_error("请选择 .wimod 文件");
    noReparse(path);std::error_code ec;auto size=fs::file_size(path,ec);if(ec||size<22||size>MaxPackage)throw std::runtime_error("模组文件损坏或超过 128 MiB 限制");
    impl->archive=readFile(path,MaxPackage);if(impl->archive.size()!=size)throw std::runtime_error("模组文件读取失败");hash=sha256(impl->archive);
    if(!mz_zip_reader_init_mem(&impl->zip,impl->archive.data(),impl->archive.size(),0))throw std::runtime_error("模组文件损坏：不是受支持的 ZIP");impl->opened=true;
    auto count=mz_zip_reader_get_num_files(&impl->zip);if(count==0||count>4096)throw std::runtime_error("模组文件数量无效或超过限制");
    uint64_t total=0;
    for(mz_uint i=0;i<count;++i){Impl::Entry entry{};if(!mz_zip_reader_file_stat(&impl->zip,i,&entry.info))throw std::runtime_error("ZIP 目录损坏");
        if(!entry.info.m_is_supported||entry.info.m_is_encrypted||entry.info.m_uncomp_size>MaxFile||entry.info.m_uncomp_size>MaxExpanded-total)throw std::runtime_error("模组文件损坏：加密、压缩格式不支持或解压大小超限");total+=entry.info.m_uncomp_size;
        if(((entry.info.m_external_attr>>16)&0170000)==0120000||(entry.info.m_external_attr&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("模组包禁止符号链接");
        auto length=mz_zip_reader_get_filename(&impl->zip,i,nullptr,0);if(length<2||length>401)throw std::runtime_error("ZIP 文件名长度无效");std::string filename(length,'\0');mz_zip_reader_get_filename(&impl->zip,i,filename.data(),length);filename.resize(length-1);
        if(entry.info.m_is_directory&&filename.back()=='/')filename.pop_back();entry.relative=safeRelative(filename);auto canonical=lower(entry.relative.generic_wstring());
        if(impl->byName.contains(canonical))throw std::runtime_error("模组文件损坏：包内存在重复文件路径");impl->byName[canonical]=impl->entries.size();impl->entries.push_back(entry);
    }
    if(!impl->byName.contains(L"mod.json"))throw std::runtime_error("缺少包根目录 mod.json");
    if(impl->entries[impl->byName[L"mod.json"]].info.m_uncomp_size>256*1024)throw std::runtime_error("mod.json 过大");
    // Retain readable manifest metadata for the manager even when incompatible.
    // validate() remains mandatory before either install or executable extraction.
    try{parseModManifest(impl->read(L"mod.json"),manifest);
        auto entry=safeRelative(manifest.entry);if(lower(entry.extension().wstring())!=L".dll"||!impl->byName.contains(lower(entry.generic_wstring())))throw std::runtime_error("缺少入口 DLL："+manifest.entry);
        validatePe(impl->read(impl->byName.at(lower(entry.generic_wstring()))));
    }catch(const std::exception& e){manifestError=e.what();}
}
ModPackage::~ModPackage()=default;
const std::string& ModPackage::bytes()const{return impl->archive;}
void ModPackage::validate(){
    for(size_t i=0;i<impl->entries.size();++i)if(!impl->entries[i].info.m_is_directory)impl->read(i);
    try{impl->checkSignature();signature=impl->byName.contains(L"signature.p7s")?"签名有效（Windows 信任链）":"未签名";}
    catch(...){signature="签名无效";throw;}
    if(!manifestError.empty())throw std::runtime_error(manifestError);
}
fs::path ModPackage::extract(const fs::path& root){
    if(!manifestError.empty())throw std::runtime_error(manifestError);
    auto version=manifest.version=="版本号未填写"?"unknown":manifest.version;
    // Version strings have been parsed, but a hash remains the immutable cache identity.
    auto versionPath=safeRelative(version);if(versionPath.has_parent_path())throw std::runtime_error("缓存版本路径无效");
    auto directory=fs::absolute(root)/wide(manifest.id)/versionPath/wide(hash);noReparse(directory);fs::create_directories(directory);
    for(size_t i=0;i<impl->entries.size();++i){auto& entry=impl->entries[i];auto dest=directory/entry.relative;noReparse(dest);
        if(entry.info.m_is_directory){fs::create_directories(dest);continue;}
        auto content=impl->read(i);if(fs::exists(dest)&&readFile(dest,MaxFile)==content)continue;fs::create_directories(dest.parent_path());writeAtomic(dest,content);
    }
    return directory/safeRelative(manifest.entry);
}
}

