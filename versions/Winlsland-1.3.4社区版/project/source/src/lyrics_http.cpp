#include "lyrics_providers.h"
#include <winhttp.h>
namespace wi {
const wchar_t* lyricsErrorText(LyricsError e){switch(e){case LyricsError::none:return L"";case LyricsError::network:return L"网络失败";case LyricsError::timeout:return L"请求超时";case LyricsError::limited:return L"接口限流";case LyricsError::notFound:return L"未找到匹配歌曲";case LyricsError::empty:return L"歌词为空";case LyricsError::parse:return L"格式解析失败";case LyricsError::decrypt:return L"歌词解密失败";case LyricsError::tokenMissing:return L"请在音乐与歌词设置中配置 Apple Music Token";case LyricsError::tokenInvalid:return L"Apple Music Token 无效或无权限";case LyricsError::cancelled:return L"请求已取消";default:return L"来源不可用";}}
namespace {
struct Internet {HINTERNET h{};~Internet(){if(h)WinHttpCloseHandle(h);}};
struct AsyncRequest {
    HINTERNET h{};HANDLE ready=CreateEventW(nullptr,FALSE,FALSE,nullptr),closed=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    DWORD error=0,bytes=0;bool callbackInstalled=false;
    static void CALLBACK callback(HINTERNET,DWORD_PTR context,DWORD status,LPVOID info,DWORD length){if(!context)return;auto& s=*(AsyncRequest*)context;
        if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING){SetEvent(s.closed);return;}
        if(status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)s.error=((WINHTTP_ASYNC_RESULT*)info)->dwError;
        if(status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE)s.bytes=length;
        if(status==WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE||status==WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE||status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE||status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)SetEvent(s.ready);
    }
    bool wait(const std::function<bool()>& valid,ULONGLONG until,LyricsHttpResponse& r){for(;;){if(!valid()){r.error=LyricsError::cancelled;return false;}if(GetTickCount64()>=until){r.error=LyricsError::timeout;return false;}if(WaitForSingleObject(ready,20)==WAIT_OBJECT_0){if(error){r.error=error==ERROR_WINHTTP_TIMEOUT?LyricsError::timeout:LyricsError::network;return false;}return true;}}}
    ~AsyncRequest(){if(h){WinHttpCloseHandle(h);if(callbackInstalled)WaitForSingleObject(closed,INFINITE);}if(ready)CloseHandle(ready);if(closed)CloseHandle(closed);}
};
}
LyricsHttpResponse lyricsHttp(const LyricsHttpRequest& q,const std::function<bool()>& valid){
    LyricsHttpResponse r;if(!valid()){r.error=LyricsError::cancelled;return r;}URL_COMPONENTS u{};u.dwStructSize=sizeof(u);u.dwHostNameLength=u.dwUrlPathLength=u.dwExtraInfoLength=(DWORD)-1;
    if(!WinHttpCrackUrl(q.url.c_str(),0,0,&u)||u.nScheme!=INTERNET_SCHEME_HTTPS||u.dwUserNameLength||u.dwPasswordLength){r.error=LyricsError::unavailable;return r;}
    std::wstring host(u.lpszHostName,u.dwHostNameLength),path(u.lpszUrlPath,u.dwUrlPathLength);if(u.dwExtraInfoLength)path.append(u.lpszExtraInfo,u.dwExtraInfoLength);
    Internet session{WinHttpOpen(L"WinIsland/1.3.4",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,WINHTTP_FLAG_ASYNC)};
    if(!session.h){r.error=LyricsError::network;return r;}WinHttpSetTimeouts(session.h,2000,2000,3000,3000);Internet connection{WinHttpConnect(session.h,host.c_str(),u.nPort,0)};if(!connection.h){r.error=LyricsError::network;return r;}
    // Buffer and request body outlive the final HANDLE_CLOSING callback, even on cancellation.
    char block[16384];AsyncRequest request;request.h=WinHttpOpenRequest(connection.h,q.method.c_str(),path.c_str(),nullptr,nullptr,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    if(!request.h||!request.ready||!request.closed){r.error=LyricsError::network;return r;}
    DWORD_PTR context=(DWORD_PTR)&request;WinHttpSetOption(request.h,WINHTTP_OPTION_CONTEXT_VALUE,&context,sizeof(context));
    request.callbackInstalled=WinHttpSetStatusCallback(request.h,AsyncRequest::callback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0)!=WINHTTP_INVALID_STATUS_CALLBACK;
    if(!request.callbackInstalled){r.error=LyricsError::network;return r;}
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect));
    DWORD decompress=WINHTTP_DECOMPRESSION_FLAG_GZIP|WINHTTP_DECOMPRESSION_FLAG_DEFLATE;WinHttpSetOption(request.h,WINHTTP_OPTION_DECOMPRESSION,&decompress,sizeof(decompress));
    auto until=GetTickCount64()+std::clamp(q.timeoutMs,100,15000);
    auto start=[&](BOOL ok){if(!ok&&GetLastError()!=ERROR_IO_PENDING){r.error=LyricsError::network;return false;}return request.wait(valid,until,r);};
    if(!start(WinHttpSendRequest(request.h,q.headers.c_str(),(DWORD)q.headers.size(),q.body.empty()?nullptr:(void*)q.body.data(),(DWORD)q.body.size(),(DWORD)q.body.size(),context)))return r;
    if(!start(WinHttpReceiveResponse(request.h,nullptr)))return r;DWORD n=sizeof(r.status);if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&r.status,&n,nullptr)){r.error=LyricsError::network;return r;}
    for(;;){if(!start(WinHttpReadData(request.h,block,sizeof(block),nullptr)))return r;if(!request.bytes)break;if(r.body.size()+request.bytes>8*1024*1024){r.error=LyricsError::parse;r.body.clear();return r;}r.body.append(block,request.bytes);}
    if(r.status==429)r.error=LyricsError::limited;else if(r.status==401||r.status==403)r.error=LyricsError::tokenInvalid;else if(r.status<200||r.status>=300)r.error=LyricsError::unavailable;return r;
}
}
