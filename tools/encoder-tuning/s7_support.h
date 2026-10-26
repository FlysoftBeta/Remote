// Standalone S7 measurement tool. No application / prior-art encoder code used.
#include <windows.h>
#include <d3d11.h>
#include <d3d10.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vpl/mfxdispatcher.h>
#include <vpl/mfxvideo.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
static const std::string root = "C:/src/tmp/t1-encoder-tuning/";
static void check(mfxStatus s, const char* what) {
    if (s < 0) throw std::runtime_error(std::string(what)+"="+std::to_string(s));
}
static void hrcheck(HRESULT h, const char* what) {
    if (FAILED(h)) throw std::runtime_error(std::string(what)+" HRESULT="+std::to_string((unsigned long)h));
}
static double tick() { LARGE_INTEGER n,f; QueryPerformanceCounter(&n); QueryPerformanceFrequency(&f); return 1000.0*double(n.QuadPart)/double(f.QuadPart); }
static std::string luidstr(LUID l) { char b[64]; sprintf_s(b,"%08lx:%08lx",(unsigned long)l.HighPart,l.LowPart); return b; }
struct GPU {
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    UINT index=0;
    LUID luid{};
    GPU() {
        ComPtr<IDXGIFactory1> factory; hrcheck(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");
        for (UINT i=0;;++i) {
            ComPtr<IDXGIAdapter1> a;
            if (factory->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
            std::wcout<<L"DXGI adapter "<<i<<L" "<<d.Description<<L" vendor="<<d.VendorId<<L" device="<<d.DeviceId<<L"\n";
            if (d.VendorId==0x8086 && !adapter) { adapter=a; index=i; luid=d.AdapterLuid; }
        }
        if (!adapter) throw std::runtime_error("No Intel adapter");
        D3D_FEATURE_LEVEL fl;
        hrcheck(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&device,&fl,&ctx),"D3D11CreateDevice");
        ComPtr<ID3D10Multithread> mt; hrcheck(ctx.As(&mt),"ID3D10Multithread"); mt->SetMultithreadProtected(TRUE);
        std::cout<<"SELECTED_DXGI index="<<index<<" LUID="<<luidstr(luid)<<" video_support=1 multithread_protected="<<mt->GetMultithreadProtected()<<"\n";
    }
};
struct Loader {
    mfxLoader loader=nullptr; mfxU32 selected=0;
    void filter(const char* name, mfxU32 value) {
        mfxConfig cfg=MFXCreateConfig(loader); if (!cfg) throw std::runtime_error("MFXCreateConfig");
        mfxVariant v{}; v.Type=MFX_VARIANT_TYPE_U32; v.Data.U32=value;
        check(MFXSetConfigFilterProperty(cfg,(const mfxU8*)name,v),name);
    }
    Loader(GPU& gpu) {
        loader=MFXLoad(); if (!loader) throw std::runtime_error("MFXLoad");
        filter("mfxImplDescription.Impl",MFX_IMPL_TYPE_HARDWARE);
        filter("mfxImplDescription.mfxEncoderDescription.encoder.CodecID",MFX_CODEC_AVC);
        filter("mfxImplDescription.AccelerationMode",MFX_ACCEL_MODE_VIA_D3D11);
        bool found=false;
        for(mfxU32 i=0;;++i) {
            mfxHDL h=nullptr; mfxStatus s=MFXEnumImplementations(loader,i,MFX_IMPLCAPS_IMPLDESCSTRUCTURE,&h);
            if (s==MFX_ERR_NOT_FOUND) break; check(s,"enumerate");
            auto* d=(mfxImplDescription*)h;
            std::cout<<"IMPLEMENTATION index="<<i<<" name="<<d->ImplName<<" API="<<d->ApiVersion.Major<<"."<<d->ApiVersion.Minor
                     <<" accel="<<d->AccelerationMode<<" VendorImplID="<<d->VendorImplID<<" DeviceID="<<d->Dev.DeviceID;
            if (!found && d->VendorImplID==gpu.index) { selected=i; found=true; }
            MFXDispReleaseImplDescription(loader,h);
            h=nullptr; s=MFXEnumImplementations(loader,i,MFX_IMPLCAPS_DEVICE_ID_EXTENDED,&h);
            if (s>=0 && h) {
                auto* e=(mfxExtendedDeviceId*)h; LUID l{}; memcpy(&l,e->DeviceLUID,8);
                std::cout<<" extended_LUID_status="<<s<<" valid="<<e->LUIDValid<<" LUID="<<luidstr(l);
                MFXDispReleaseImplDescription(loader,h);
            } else std::cout<<" extended_LUID_status="<<s<<" DXGI_LUID="<<luidstr(gpu.luid)<<" (VendorImplID adapter index mapping)";
            h=nullptr; s=MFXEnumImplementations(loader,i,MFX_IMPLCAPS_IMPLPATH,&h);
            if (s>=0 && h) { std::cout<<" runtime="<<(const char*)h; MFXDispReleaseImplDescription(loader,h); }
            std::cout<<"\n";
        }
        if (!found) throw std::runtime_error("No D3D11 AVC implementation for selected Intel adapter");
    }
    ~Loader() { if(loader) MFXUnload(loader); }
};
// Separate texture for every surface (no arrays). GetHDL returns texture/subresource pair.
struct Texture { ComPtr<ID3D11Texture2D> tex; D3D11_TEXTURE2D_DESC desc{}; };
struct Allocator {
    GPU& gpu; mfxFrameAllocator api{};
    explicit Allocator(GPU& g):gpu(g) {
        api.pthis=this; api.Alloc=alloc; api.Free=freeframes; api.GetHDL=gethdl; api.Lock=lock; api.Unlock=unlock;
    }
    static mfxStatus MFX_CDECL alloc(mfxHDL p,mfxFrameAllocRequest* req,mfxFrameAllocResponse* res) {
        auto& self=*(Allocator*)p;
        if (!req || !res) return MFX_ERR_NULL_PTR;
        if (req->Info.FourCC!=MFX_FOURCC_NV12 && req->Info.FourCC!=MFX_FOURCC_RGB4) return MFX_ERR_UNSUPPORTED;
        *res={}; res->mids=new mfxMemId[req->NumFrameSuggested]{};
        for (unsigned i=0;i<req->NumFrameSuggested;++i) {
            auto* t=new Texture;
            t->desc.Width=req->Info.Width; t->desc.Height=req->Info.Height;
            t->desc.Format=req->Info.FourCC==MFX_FOURCC_NV12?DXGI_FORMAT_NV12:DXGI_FORMAT_B8G8R8A8_UNORM;
            t->desc.MipLevels=1; t->desc.ArraySize=1; t->desc.SampleDesc.Count=1;
            t->desc.Usage=D3D11_USAGE_DEFAULT; t->desc.BindFlags=D3D11_BIND_RENDER_TARGET;
            HRESULT h=self.gpu.device->CreateTexture2D(&t->desc,nullptr,&t->tex);
            if (FAILED(h)) { delete t; freeframes(p,res); return MFX_ERR_MEMORY_ALLOC; }
            res->mids[i]=t; ++res->NumFrameActual;
        }
        return MFX_ERR_NONE;
    }
    static mfxStatus MFX_CDECL freeframes(mfxHDL,mfxFrameAllocResponse* r) {
        if (!r) return MFX_ERR_NULL_PTR;
        for(unsigned i=0;i<r->NumFrameActual;++i) delete (Texture*)r->mids[i];
        delete[] r->mids; *r={}; return MFX_ERR_NONE;
    }
    static mfxStatus MFX_CDECL gethdl(mfxHDL,mfxMemId mid,mfxHDL* h) {
        if(!mid || !h) return MFX_ERR_NULL_PTR;
        auto* pair=(mfxHDLPair*)h; pair->first=((Texture*)mid)->tex.Get(); pair->second=nullptr; return MFX_ERR_NONE;
    }
    static mfxStatus MFX_CDECL lock(mfxHDL,mfxMemId,mfxFrameData*) { return MFX_ERR_UNSUPPORTED; }
    static mfxStatus MFX_CDECL unlock(mfxHDL,mfxMemId,mfxFrameData*) { return MFX_ERR_NONE; }
};
struct Session {
    mfxSession s=nullptr; Allocator allocator;
    Session(Loader& l,GPU& g):allocator(g) {
        check(MFXCreateSession(l.loader,l.selected,&s),"MFXCreateSession");
        try {
            check(MFXVideoCORE_SetHandle(s,MFX_HANDLE_D3D11_DEVICE,g.device.Get()),"SetHandle");
            check(MFXVideoCORE_SetFrameAllocator(s,&allocator.api),"SetFrameAllocator");
        } catch (...) { MFXClose(s); s=nullptr; throw; }
    }
    ~Session() { if(s) MFXClose(s); }
};
struct Row {
    std::string name; int fps=60, lp=MFX_CODINGOPTION_ON, rc=MFX_RATECONTROL_CBR, kbps=12000;
    int vbv=1, ldb=MFX_CODINGOPTION_OFF, maxframe=0, intra=0, qp=0, slices=1, maxslice=0;
    int profile=MFX_PROFILE_AVC_CONSTRAINED_BASELINE, fourcc=MFX_FOURCC_NV12, tu=7;
};
struct Params {
    mfxVideoParam p{}; mfxExtCodingOption2 c2{}; mfxExtCodingOption3 c3{}; mfxExtBuffer* ext[2]{};
    Params() { bind(); }
    Params(const Params& b):p(b.p),c2(b.c2),c3(b.c3) { bind(); }
    Params& operator=(const Params& b) { p=b.p;c2=b.c2;c3=b.c3;bind();return *this; }
    void bind() {
        c2.Header.BufferId=MFX_EXTBUFF_CODING_OPTION2; c2.Header.BufferSz=sizeof(c2);
        c3.Header.BufferId=MFX_EXTBUFF_CODING_OPTION3; c3.Header.BufferSz=sizeof(c3);
        ext[0]=(mfxExtBuffer*)&c2; ext[1]=(mfxExtBuffer*)&c3; p.ExtParam=ext; p.NumExtParam=2;
    }
    explicit Params(const Row& r,bool system=false):Params() {
        p.AsyncDepth=1; p.IOPattern=system?MFX_IOPATTERN_IN_SYSTEM_MEMORY:MFX_IOPATTERN_IN_VIDEO_MEMORY;
        auto& m=p.mfx; m.CodecId=MFX_CODEC_AVC; m.CodecProfile=(mfxU16)r.profile; m.CodecLevel=r.fps==60?MFX_LEVEL_AVC_42:MFX_LEVEL_AVC_4;
        m.TargetUsage=(mfxU16)r.tu; m.LowPower=(mfxU16)r.lp; m.RateControlMethod=(mfxU16)r.rc;
        m.TargetKbps=(mfxU16)r.kbps; m.MaxKbps=(mfxU16)r.kbps;
        m.BufferSizeInKB=(mfxU16)((r.kbps*r.vbv+8*r.fps-1)/(8*r.fps)); m.InitialDelayInKB=m.BufferSizeInKB/2;
        m.GopPicSize=65535; m.GopRefDist=1; m.IdrInterval=0; m.NumRefFrame=1; m.NumSlice=(mfxU16)r.slices;
        auto& f=m.FrameInfo; f.FourCC=r.fourcc; f.Width=1920; f.Height=1088; f.CropW=1920; f.CropH=1080;
        f.ChromaFormat=MFX_CHROMAFORMAT_YUV420; f.PicStruct=MFX_PICSTRUCT_PROGRESSIVE; f.FrameRateExtN=r.fps; f.FrameRateExtD=1;
        c2.AdaptiveI=MFX_CODINGOPTION_OFF; c2.AdaptiveB=MFX_CODINGOPTION_OFF;
        c2.IntRefType=(mfxU16)r.intra; c2.IntRefCycleSize=r.intra?(mfxU16)r.fps:0; c2.IntRefQPDelta=(mfxI16)r.qp;
        c2.MaxFrameSize=r.maxframe?(mfxU32)(r.kbps*1000*1.5/(8*r.fps)):0;
        c2.MaxSliceSize=r.maxslice; c3.LowDelayBRC=(mfxU16)r.ldb;
    }
    std::map<std::string,long long> fields() const {
        const auto& m=p.mfx; const auto& f=m.FrameInfo;
        return {{"AsyncDepth",p.AsyncDepth},{"IOPattern",p.IOPattern},{"CodecId",m.CodecId},{"LowPower",m.LowPower},
            {"RateControlMethod",m.RateControlMethod},{"TargetKbps",m.TargetKbps},{"MaxKbps",m.MaxKbps},
            {"BufferSizeInKB",m.BufferSizeInKB},{"InitialDelayInKB",m.InitialDelayInKB},{"TargetUsage",m.TargetUsage},
            {"GopRefDist",m.GopRefDist},{"NumRefFrame",m.NumRefFrame},{"GopPicSize",m.GopPicSize},{"IdrInterval",m.IdrInterval},
            {"NumSlice",m.NumSlice},{"CodecProfile",m.CodecProfile},{"CodecLevel",m.CodecLevel},{"FourCC",f.FourCC},
            {"Width",f.Width},{"Height",f.Height},{"CropW",f.CropW},{"CropH",f.CropH},{"FrameRateExtN",f.FrameRateExtN},
            {"FrameRateExtD",f.FrameRateExtD},{"ChromaFormat",f.ChromaFormat},{"PicStruct",f.PicStruct},
            {"AdaptiveI",c2.AdaptiveI},{"AdaptiveB",c2.AdaptiveB},{"IntRefType",c2.IntRefType},{"IntRefCycleSize",c2.IntRefCycleSize},
            {"IntRefQPDelta",c2.IntRefQPDelta},{"MaxFrameSize",c2.MaxFrameSize},{"MaxSliceSize",c2.MaxSliceSize},{"LowDelayBRC",c3.LowDelayBRC}};
    }
};
static std::string fields(const Params& p) { std::ostringstream s; for(auto& x:p.fields()) s<<x.first<<"="<<x.second<<";"; return s.str(); }
static std::string diffs(const Params& a,const Params& b) {
    std::ostringstream s; auto af=a.fields(),bf=b.fields();
    for(auto& x:af) if(x.second!=bf[x.first]) s<<x.first<<":"<<x.second<<"->"<<bf[x.first]<<";";
    return s.str();
}
