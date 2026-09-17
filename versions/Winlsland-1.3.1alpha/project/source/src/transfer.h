#pragma once
#include "core.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
namespace wi {
using TransferJson = winrt::Windows::Data::Json::JsonObject;
struct TransferEntry {
    std::wstring id,name,extension,original,saved,mode=L"pending",state=L"pending",error;
    double size=0,imported=0,progress=0;
};
struct TransferSnapshot {
    bool enabled=false; uint64_t revision=0,viewRequest=0;
    std::wstring remember,error,selected;
    std::vector<TransferEntry> entries;
    TransferJson preferences;
};
// The host owns all records. Views never write the index or delete source files.
class TransferStore {
    fs::path root; std::mutex mutex; TransferSnapshot state;
    std::map<std::wstring,std::shared_ptr<std::atomic_bool>> cancellations;
    Jobs copies; bool closing=false,storageFault=false; HANDLE ownership=nullptr;
    std::atomic<double> viewInteractionUntil{0};
    void persist(); // caller holds mutex; atomic replacement, backup of last valid generation
    TransferEntry& find(const std::wstring&);
    void queueCopy(const std::wstring&);
public:
    explicit TransferStore(const fs::path&);
    ~TransferStore();
    TransferSnapshot snapshot();
    bool refreshSnapshot(TransferSnapshot&);
    bool enabled();
    bool viewInteractionActive()const{return now()<viewInteractionUntil.load();}
    TransferJson command(const std::string&,const TransferJson&);
    std::vector<std::wstring> import(const std::vector<fs::path>&);
    void choose(const std::vector<std::wstring>&,const std::wstring&,bool remember=false);
    fs::path resolve(const std::wstring&,bool original=false);
    void requestView(const std::wstring& id=L"");
};
void transferDragFile(const fs::path&);
bool transferDragActive();
class TransferWidget {
    struct Impl; std::unique_ptr<Impl> impl;
    friend int transferWidgetTest(const fs::path&);
public:
    TransferWidget(TransferStore&,HWND island,std::function<void(int,const std::wstring&)> receiver,std::function<void()> openPage);
    ~TransferWidget();
    void sync(const RECT& island,double idleWidth,double idleHeight,bool reduced,double shoulderDip=4);
    void receive(const std::vector<fs::path>&);
    void receiverAction(int);
    void diagnosticDrop(const fs::path&,int);
};
int transferWidgetTest(const fs::path&);
}
