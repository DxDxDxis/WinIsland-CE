#include "core.h"
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
namespace wi {
class Activation
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          IActivateAudioInterfaceCompletionHandler, Microsoft::WRL::FtmBase> {
  public:
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT result = E_FAIL;
    ComPtr<IUnknown> object;
    ~Activation() {
        CloseHandle(done);
    }
    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation *op) override {
        op->GetActivateResult(&result, &object);
        SetEvent(done);
        return S_OK;
    }
};
struct Capture {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    explicit Capture(DWORD pid) {
        AUDIOCLIENT_ACTIVATION_PARAMS activation{};
        activation.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        activation.ProcessLoopbackParams.TargetProcessId = pid;
        activation.ProcessLoopbackParams.ProcessLoopbackMode =
            PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        PROPVARIANT prop{};
        prop.vt = VT_BLOB;
        prop.blob.cbSize = sizeof(activation);
        prop.blob.pBlobData = (BYTE *)&activation;
        auto callback = Microsoft::WRL::Make<Activation>();
        ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
        HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                                 __uuidof(IAudioClient), &prop, callback.Get(), &operation);
        if (FAILED(hr) || WaitForSingleObject(callback->done, 2500) != WAIT_OBJECT_0 ||
            FAILED(callback->result))
            throw std::runtime_error("process audio unavailable");
        callback->object.As(&client);
        WAVEFORMATEX f{WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16, 0};
        if (!client ||
            FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, &f,
                                      nullptr)) ||
            FAILED(client->GetService(IID_PPV_ARGS(&capture))) || FAILED(client->Start()))
            throw std::runtime_error("process audio init");
    }
    ~Capture() {
        if (client)
            client->Stop();
    }
    float peak() {
        UINT32 n = 0;
        float peak = 0;
        for (int i = 0; i < 128; i++) {
            if (FAILED(capture->GetNextPacketSize(&n)) || !n)
                break;
            BYTE *data = nullptr;
            DWORD flags;
            UINT64 a, b;
            if (FAILED(capture->GetBuffer(&data, &n, &flags, &a, &b)))
                break;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                auto *p = (const int16_t *)data;
                for (UINT32 j = 0; j < n * 2; j++)
                    peak = std::max(peak, std::abs((int)p[j]) / 32768.f);
            }
            capture->ReleaseBuffer(n);
        }
        return peak < .0001f ? 0 : peak;
    }
};
Audio::Audio() {
    thread = std::thread([this] { run(); });
}
Audio::~Audio() {
    stop = true;
    thread.join();
}
void Audio::select(const Music *m) {
    std::lock_guard l(mu);
    std::wstring id = m ? m->key() : L"";
    if (id == source)
        return;
    source = id;
    platform = m ? m->platform : L"";
    frame = {};
    frame.source = id;
}
AudioFrame Audio::snapshot() {
    std::lock_guard l(mu);
    return frame;
}
void Audio::run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct Session {
        DWORD pid;
        ComPtr<IAudioSessionControl2> control;
        ComPtr<ISimpleAudioVolume> volume;
        ComPtr<IAudioEndpointVolume> endpoint;
        ComPtr<IAudioMeterInformation> meter;
    };
    std::vector<Session> sessions;
    std::map<DWORD, std::unique_ptr<Capture>> captures;
    std::map<DWORD, double> retry;
    std::wstring selected;
    double nextScan = 0;
    AudioFrame next;
    while (!stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        Measure work(AudioWork);
        std::wstring id, app;
        {
            std::lock_guard l(mu);
            id = source;
            app = platform;
        }
        if (id != selected) {
            sessions.clear();
            captures.clear();
            retry.clear();
            selected = id;
            next = {};
            next.source = id;
            nextScan = 0;
        }
        if (id.empty())
            continue;
        if (now() >= nextScan) {
            nextScan = now() + 2;
            sessions.clear();
            auto pids = processIds(app == L"媒体集成测试" ? L"WinIsland-MediaFixture.exe" : app);
            ComPtr<IMMDeviceEnumerator> enumerator;
            ComPtr<IMMDeviceCollection> devices;
            if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                           IID_PPV_ARGS(&enumerator))) &&
                SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) {
                UINT count = 0;
                devices->GetCount(&count);
                for (UINT i = 0; i < count; i++) {
                    ComPtr<IMMDevice> d;
                    devices->Item(i, &d);
                    ComPtr<IAudioSessionManager2> m;
                    ComPtr<IAudioEndpointVolume> v;
                    ComPtr<IAudioSessionEnumerator> list;
                    if (!d || FAILED(d->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &m)) ||
                        FAILED(d->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &v)) ||
                        FAILED(m->GetSessionEnumerator(&list)))
                        continue;
                    int n = 0;
                    list->GetCount(&n);
                    for (int j = 0; j < n; j++) {
                        ComPtr<IAudioSessionControl> base;
                        Session s;
                        list->GetSession(j, &base);
                        if (!base || FAILED(base.As(&s.control)))
                            continue;
                        s.control->GetProcessId(&s.pid);
                        if (!pids.contains(s.pid))
                            continue;
                        base.As(&s.volume);
                        base.As(&s.meter);
                        s.endpoint = v;
                        sessions.push_back(s);
                    }
                }
            }
            std::set<DWORD> live;
            for (auto &s : sessions)
                live.insert(s.pid);
            for (auto i = captures.begin(); i != captures.end();)
                if (!live.contains(i->first))
                    i = captures.erase(i);
                else
                    ++i;
        }
        float peak = 0;
        next.available = false;
        next.muted = !sessions.empty();
        std::map<DWORD, float> measured;
        for (auto &s : sessions)
            try {
                AudioSessionState state;
                BOOL mute = false, outMute = false;
                float gain = 0, out = 0;
                if (FAILED(s.control->GetState(&state)) || state == AudioSessionStateExpired)
                    continue;
                if (!captures.contains(s.pid)) {
                    if (retry[s.pid] <= now())
                        try {
                            captures[s.pid] = std::make_unique<Capture>(s.pid);
                        } catch (...) {
                            monitor.event("进程音频回环不可用；尝试真实会话峰值备用来源");
                            retry[s.pid] = now() + 10;
                        }
                }
                float measuredPeak = 0;
                if (captures.contains(s.pid)) {
                    if (!measured.contains(s.pid))
                        measured[s.pid] = captures[s.pid]->peak();
                    measuredPeak = measured[s.pid];
                } else if (!s.meter || FAILED(s.meter->GetPeakValue(&measuredPeak)))
                    continue;
                if (!s.volume || FAILED(s.volume->GetMute(&mute)) ||
                    FAILED(s.volume->GetMasterVolume(&gain)) || FAILED(s.endpoint->GetMute(&outMute)) ||
                    FAILED(s.endpoint->GetMasterVolumeLevelScalar(&out)))
                    continue;
                next.available = true;
                bool silent = mute || outMute || gain <= 0 || out <= 0;
                if (!silent)
                    next.muted = false;
                if (!silent && state == AudioSessionStateActive)
                    peak = std::max(peak, measuredPeak * out);
            } catch (...) {
                captures.erase(s.pid);
                nextScan = 0;
            }
        std::move(next.peaks.begin() + 1, next.peaks.end(), next.peaks.begin());
        next.peaks.back() = std::clamp(peak, 0.f, 1.f);
        {
            std::lock_guard l(mu);
            if (source == selected)
                frame = next;
        }
    }
    captures.clear();
    sessions.clear();
    CoUninitialize();
}
} // namespace wi
