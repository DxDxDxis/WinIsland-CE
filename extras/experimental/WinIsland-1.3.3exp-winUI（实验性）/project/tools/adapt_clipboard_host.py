from pathlib import Path
p=Path('source/src/settings_host.inc');s=p.read_text('utf-8')
anchor='                    if(action=="translate")throw'
idx=s.index(anchor)
new='''                    if(action=="export"){
                        // Native dialog owned by the host; no arbitrary file writes in the client.
                        ComPtr<IFileSaveDialog> dialog;winrt::check_hresult(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));
                        COMDLG_FILTERSPEC types[]={{L"纯文本 (*.txt)",L"*.txt"},{L"Markdown (*.md)",L"*.md"}};
                        dialog->SetFileTypes(2,types);dialog->SetDefaultExtension(L"txt");dialog->SetFileName(L"复制粘贴.txt");dialog->SetTitle(L"导出已保存的复制内容");
                        auto hr=dialog->Show(hwnd);if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED)){put(response,L"cancelled",true);}
                        else {winrt::check_hresult(hr);ComPtr<IShellItem> item;winrt::check_hresult(dialog->GetResult(&item));PWSTR selected=nullptr;winrt::check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH,&selected));fs::path path(selected);CoTaskMemFree(selected);
                            jobs.post([this,request,params,path]{try{auto entry=clipboard->command("get",params).GetNamedObject(L"entry");auto part=text(params,L"part");auto content=entry.GetNamedString(L"text");auto translation=entry.GetNamedString(L"translation",L"");
                                if(part=="translation"||part=="both"){if(translation.empty())throw std::runtime_error("本条记录没有译文");content=part=="translation"?translation:content+L"\\r\\n\\r\\n"+translation;}else if(part!="original")throw std::runtime_error("导出内容无效");
                                auto bytes=std::string("\\xef\\xbb\\xbf")+utf8(content.c_str());HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("无法创建导出文件；请选择新的文件名，已有文件不会覆盖");
                                DWORD written=0;BOOL success=WriteFile(file,bytes.data(),(DWORD)bytes.size(),&written,nullptr);if(success)success=FlushFileBuffers(file);CloseHandle(file);if(!success||written!=bytes.size()){DeleteFileW(path.c_str());throw std::runtime_error("导出写入失败");}
                                auto r=ok();put(r,L"exported",true);SettingsBridge::reply(request,utf8(r.Stringify().c_str()));
                            }catch(const std::exception& e){SettingsBridge::reply(request,utf8(error("CLIPBOARD_EXPORT",e.what()).Stringify().c_str()));}catch(...){SettingsBridge::reply(request,utf8(error("CLIPBOARD_EXPORT","导出失败").Stringify().c_str()));}});continue;
                        }
                    }
                    else '''
s=s[:idx]+new+s[idx:].lstrip()
s=s.replace('monitor.event("Electron 设置已应用并持久化")','monitor.event("WinUI 设置已应用并持久化")')
p.write_text(s,encoding='utf-8')
p=Path('settings-winui/clients.h');s=p.read_text('utf-8').replace('L"transfer.call"};','L"transfer.call",L"clipboard.call"};').replace('payload.size()>65536','payload.size()>2*1024*1024')
s=s.replace('auto until=GetTickCount64()+(command==L"transfer.call"&&text(params,L"action")==L"drag"?65000:8000);handle pipe;','auto until=GetTickCount64()+((command==L"transfer.call"&&text(params,L"action")==L"drag")||(command==L"clipboard.call"&&text(params,L"action")==L"export")?65000:8000);handle pipe;')
s=s.replace('struct TransferClient', 'struct ClipboardClient {Transport& t;auto call(hstring action,JsonObject p=JsonObject{}){put(p,L"action",action);return t.call(L"clipboard.call",p);}};\nstruct TransferClient')
p.write_text(s,encoding='utf-8')
