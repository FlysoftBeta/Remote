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
static const std::string root = "C:/src/tmp/s7/";
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
static std::vector<Row> rows() {
    std::vector<Row> out;
    for(int fps:{30,60}) {
        Row b; b.fps=fps;
        auto add=[&](std::string n,Row r) { r.name=std::to_string(fps)+"_"+n;out.push_back(r); };
        add("base_lp_cbr12",b);
        Row r=b; r.lp=MFX_CODINGOPTION_OFF; add("vme_cbr12",r);
        for(int kb:{10000,16000}) {r=b;r.kbps=kb;add("cbr"+std::to_string(kb/1000),r);}
        r=b;r.rc=MFX_RATECONTROL_VBR;add("vbr12",r);
        r=b;r.ldb=MFX_CODINGOPTION_ON;add("cbr12_ldb",r);
        r.rc=MFX_RATECONTROL_VBR;add("vbr12_ldb",r);
        r.maxframe=1;add("vbr12_ldb_maxframe",r);
        r=b;r.vbv=3;add("vbv3",r);
        for(int type:{1,2}) {r=b;r.intra=type;r.qp=-2;add(type==1?"intra_vertical":"intra_horizontal",r);}
        for(int n:{2,4}) {r=b;r.slices=n;add("slices"+std::to_string(n),r);}
        r=b;r.maxslice=1400;add("maxslice1400",r);
        r=b;r.profile=MFX_PROFILE_AVC_MAIN;add("main",r);
        r=b;r.profile=MFX_PROFILE_AVC_HIGH;add("high",r);
        r=b;r.fourcc=MFX_FOURCC_RGB4;add("rgb4",r);
        r=b;r.tu=4;add("tu4",r);
        // Non-LowPower controls isolate feature support from VDEnc limitations.
        r=b;r.lp=MFX_CODINGOPTION_OFF;r.ldb=MFX_CODINGOPTION_ON;add("vme_cbr12_ldb",r);
        r.rc=MFX_RATECONTROL_VBR;r.maxframe=1;add("vme_vbr12_ldb_maxframe",r);
        r=b;r.lp=MFX_CODINGOPTION_OFF;r.intra=1;r.qp=-2;add("vme_intra_vertical",r);
    }
    return out;
}
struct Result { Row row; bool system=false; int query=-999,init=-999,get=-999; Params actual; std::string diff; };
static Result probe(const Row& row,Loader& loader,GPU& gpu,bool system,std::ofstream& csv) {
    Session session(loader,gpu); Params wanted(row,system), queried(wanted), read;
    Result r; r.row=row;r.system=system;
    r.query=MFXVideoENCODE_Query(session.s,&wanted.p,&queried.p);
    // Init uses the ORIGINAL requested parameters: Query corrections are reported, never silently adopted.
    Params init(wanted); r.init=MFXVideoENCODE_Init(session.s,&init.p);
    if(r.init>=0) { r.get=MFXVideoENCODE_GetVideoParam(session.s,&read.p); if(r.get>=0) {r.actual=read;r.diff=diffs(wanted,read);} MFXVideoENCODE_Close(session.s); }
    csv<<row.name<<","<<(system?"system":"video")<<","<<r.query<<","<<r.init<<","<<r.get<<",\""<<diffs(wanted,queried)<<"\",\""<<r.diff<<"\",\""<<fields(wanted)<<"\",\""<<fields(queried)<<"\",\""<<(r.get>=0?fields(read):"")<<"\"\n"; csv.flush();
    std::cout<<"MATRIX "<<row.name<<" "<<(system?"system":"video")<<" Query="<<r.query<<" Init="<<r.init<<" Get="<<r.get<<" differences="<<r.diff<<"\n";
    return r;
}
static double pct(std::vector<double> v,double p) { if(v.empty())return 0;std::sort(v.begin(),v.end());return v[(size_t)std::max(0.0,std::ceil(p*v.size())-1)]; }
// Count access-unit starts from first_mb_in_slice, not slice NAL count (multi-slice streams).
struct Nals { int au=0,idr=0,sps=0,pps=0,invalid=0; };
static Nals parse(const uint8_t* data,size_t n) {
    Nals out;
    std::vector<size_t> starts, ends;
    for(size_t i=0;i+3<n;) {
        size_t prefix=0;
        if(data[i]==0 && data[i+1]==0 && data[i+2]==1) prefix=3;
        else if(i+4<n && data[i]==0 && data[i+1]==0 && data[i+2]==0 && data[i+3]==1) prefix=4;
        if(prefix) { if(!starts.empty())ends.push_back(i);starts.push_back(i+prefix);i+=prefix; } else ++i;
    }
    if(!starts.empty())ends.push_back(n);
    for(size_t j=0;j<starts.size();++j) {
        size_t s=starts[j],e=ends[j]; if(s>=e) {++out.invalid;continue;}
        if(data[s]&128)++out.invalid;
        int type=data[s]&31; if(type==7)++out.sps;if(type==8)++out.pps;
        if(type==1 || type==5) {
            std::vector<uint8_t> rbsp;int zeros=0;
            for(size_t k=s+1;k<e;++k) { if(zeros>=2 && data[k]==3) {zeros=0;continue;}rbsp.push_back(data[k]);zeros=data[k]==0?zeros+1:0; }
            // first_mb_in_slice == 0 has Exp-Golomb representation '1'.
            if(rbsp.empty())++out.invalid;
            else if(rbsp[0]&128) {++out.au;if(type==5)++out.idr;}
        }
    }
    return out;
}
// Luma: translating gradient + scrolling stripes + binary frame counter; UV neutral.
static void pattern(std::vector<uint8_t>& bytes,int w,int h,int frame,bool rgb) {
    if(rgb) { bytes.assign((size_t)w*h*4,0); for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        uint8_t l=(uint8_t)(16+((x+frame*3+y/4)%220)); if(((y+frame*5)/32)%2) l=(uint8_t)(235-l/2);
        if(y<64 && x<640)l=((frame>>(x/32))&1)?235:16;
        size_t k=((size_t)y*w+x)*4;bytes[k]=l;bytes[k+1]=l;bytes[k+2]=l;bytes[k+3]=255;
    }} else {bytes.assign((size_t)w*h*3/2,128);for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        uint8_t l=(uint8_t)(16+((x+frame*3+y/4)%220));if(((y+frame*5)/32)%2)l=(uint8_t)(235-l/2);
        if(y<64 && x<640)l=((frame>>(x/32))&1)?235:16;bytes[(size_t)y*w+x]=l;
    }}
}
static bool latency(const Result& result,Loader& loader,GPU& gpu,int frames,std::ofstream& summary) {
    const Row& row=result.row; Session session(loader,gpu); Params params(row,result.system);
    int init=MFXVideoENCODE_Init(session.s,&params.p);check((mfxStatus)init,"latency Init");
    Params read; check(MFXVideoENCODE_GetVideoParam(session.s,&read.p),"latency GetVideoParam");
    mfxFrameAllocRequest request{};check(MFXVideoENCODE_QueryIOSurf(session.s,&read.p,&request),"QueryIOSurf");
    mfxFrameAllocResponse response{};
    std::vector<mfxFrameSurface1> surfaces;
    std::vector<std::vector<uint8_t>> host;
    if(!result.system) {
        check(Allocator::alloc(&session.allocator,&request,&response),"external input allocation");
        surfaces.resize(response.NumFrameActual);
        for(size_t i=0;i<surfaces.size();++i) { surfaces[i].Info=read.p.mfx.FrameInfo;surfaces[i].Data.MemId=response.mids[i]; }
    } else {
        surfaces.resize(std::max<unsigned>(1,request.NumFrameSuggested));host.resize(surfaces.size());
        for(auto& s:surfaces)s.Info=read.p.mfx.FrameInfo;
    }
    std::cout<<"LATENCY_BEGIN "<<row.name<<" input="<<(result.system?"system":"video")<<" frames="<<frames<<" surfaces="<<surfaces.size()<<"\n";
    std::ofstream bitout(root+row.name+".h264",std::ios::binary), frameout(root+row.name+"-frames.csv");
    frameout<<"input,latency_ms,bytes,frame_type,timestamp,encode_status,sync_status\n";
    std::vector<uint8_t> bits(8*1024*1024),pixels; std::vector<double> times,sizes,idrs;
    uint64_t total=0;int submitted=0,outputs=0,busy=0,moredata=0;Nals all;
    bool okay=true;
    // Flush the UpdateSubresource upload before starting QPC, including GPU completion.
    D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT,0};ComPtr<ID3D11Query> uploadDone;
    if(!result.system)hrcheck(gpu.device->CreateQuery(&qd,&uploadDone),"upload event query");
    for(int frame=0;frame<frames+100;++frame) {
        mfxFrameSurface1* surface=nullptr;
        if(frame<frames) {
            size_t index=0;while(index<surfaces.size() && surfaces[index].Data.Locked)++index;
            if(index==surfaces.size()) {std::cout<<"ERROR all input surfaces locked\n";okay=false;break;}
            surface=&surfaces[index];const auto& f=surface->Info;
            pattern(pixels,f.Width,f.Height,frame,f.FourCC==MFX_FOURCC_RGB4);
            if(!result.system) {
                auto* tex=(Texture*)surface->Data.MemId;
                gpu.ctx->UpdateSubresource(tex->tex.Get(),0,nullptr,pixels.data(),f.Width*(f.FourCC==MFX_FOURCC_RGB4?4:1),0);
                gpu.ctx->End(uploadDone.Get());gpu.ctx->Flush();
                double deadline=tick()+5000;
                while(gpu.ctx->GetData(uploadDone.Get(),nullptr,0,0)==S_FALSE) {if(tick()>deadline)throw std::runtime_error("upload timeout");Sleep(0);}
            } else {
                host[index]=pixels;surface->Data.Pitch=(mfxU16)(f.Width*(f.FourCC==MFX_FOURCC_RGB4?4:1));
                if(f.FourCC==MFX_FOURCC_NV12) {surface->Data.Y=host[index].data();surface->Data.UV=surface->Data.Y+(size_t)f.Width*f.Height;}
                else {surface->Data.B=host[index].data();surface->Data.G=surface->Data.B+1;surface->Data.R=surface->Data.B+2;surface->Data.A=surface->Data.B+3;}
            }
            surface->Data.TimeStamp=(mfxU64)frame*90000/row.fps;surface->Data.FrameOrder=frame;
        }
        mfxBitstream bs{};bs.Data=bits.data();bs.MaxLength=(mfxU32)bits.size();mfxSyncPoint sync=nullptr;
        double begin=tick();mfxStatus status;
        do {status=MFXVideoENCODE_EncodeFrameAsync(session.s,nullptr,surface,&bs,&sync);
            if(status==MFX_WRN_DEVICE_BUSY) {++busy;Sleep(1);}
            if(tick()-begin>10000)throw std::runtime_error("EncodeFrameAsync busy timeout");
        } while(status==MFX_WRN_DEVICE_BUSY);
        if(surface && status>=0)++submitted;
        if(status==MFX_ERR_MORE_DATA) {if(surface){++submitted;++moredata;continue;}break;}
        if(status<0) {std::cout<<"ENCODE_ERROR frame="<<frame<<" status="<<status<<"\n";okay=false;break;}
        if(!sync) {std::cout<<"ENCODE_ERROR no sync point\n";okay=false;break;}
        mfxStatus synced=MFXVideoCORE_SyncOperation(session.s,sync,10000);double elapsed=tick()-begin;
        if(synced!=MFX_ERR_NONE) {std::cout<<"SYNC_ERROR "<<synced<<"\n";okay=false;break;}
        times.push_back(elapsed);sizes.push_back(bs.DataLength);total+=bs.DataLength;++outputs;
        Nals nal=parse(bits.data()+bs.DataOffset,bs.DataLength);all.au+=nal.au;all.idr+=nal.idr;all.sps+=nal.sps;all.pps+=nal.pps;all.invalid+=nal.invalid;
        if(nal.idr)idrs.push_back(bs.DataLength);
        bitout.write((char*)bits.data()+bs.DataOffset,bs.DataLength);
        frameout<<frame<<","<<elapsed<<","<<bs.DataLength<<","<<bs.FrameType<<","<<bs.TimeStamp<<","<<status<<","<<synced<<"\n";
    }
    MFXVideoENCODE_Close(session.s);if(response.mids)Allocator::freeframes(&session.allocator,&response);
    bool valid=okay && submitted==frames && outputs==frames && all.au==frames && all.idr>=1 && all.sps>=1 && all.pps>=1 && all.invalid==0;
    double avg=outputs?double(total)/outputs:0,mbps=double(total)*8*row.fps/(frames*1000000.0);
    summary<<row.name<<","<<(result.system?"system":"video")<<","<<init<<","<<submitted<<","<<outputs<<","<<pct(times,.5)<<","<<pct(times,.95)<<","<<pct(times,.99)<<","<<pct(times,1)
           <<","<<avg<<","<<pct(sizes,.99)<<","<<pct(sizes,1)<<","<<pct(idrs,1)<<","<<mbps<<","<<all.au<<","<<all.idr<<","<<all.sps<<","<<all.pps<<","<<all.invalid<<","<<busy<<","<<moredata<<","<<valid<<"\n";summary.flush();
    std::cout<<"LATENCY_END "<<row.name<<" outputs="<<outputs<<" p50="<<pct(times,.5)<<" p95="<<pct(times,.95)<<" p99="<<pct(times,.99)<<" max="<<pct(times,1)<<" avg_bytes="<<avg<<" p99_bytes="<<pct(sizes,.99)<<" max_bytes="<<pct(sizes,1)<<" IDR_bytes="<<pct(idrs,1)<<" Mbps="<<mbps<<" AU="<<all.au<<" structural_valid="<<valid<<"\n";
    return valid;
}
int main(int argc,char** argv) {
    std::cout<<std::unitbuf;
    try {
        GPU gpu;Loader loader(gpu);
        {Session session(loader,gpu);mfxVersion v{};check(MFXQueryVersion(session.s,&v),"MFXQueryVersion");std::cout<<"SESSION_API "<<v.Major<<"."<<v.Minor<<"\n";}
        if(argc>1 && std::string(argv[1])=="--enumerate")return 0;
        int frames=600;std::string only;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--row" && i+1<argc)only=argv[++i];
            else if(arg=="--frames" && i+1<argc)frames=std::stoi(argv[++i]);
            else if(arg!="--matrix")throw std::runtime_error("Usage: s7probe [--enumerate | --matrix | --row NAME] [--frames N]");
        }
        if(frames<=0)throw std::runtime_error("frames must be positive");
        std::ofstream matrix(root+(only.empty()?"matrix.csv":only+"-matrix.csv"));matrix<<"row,input,query,init,get,query_diffs,readback_diffs,requested,query_result,readback\n";
        std::vector<Result> accepted;
        for(auto& row:rows()) {
            if(!only.empty() && row.name!=only)continue;
            auto r=probe(row,loader,gpu,false,matrix);
            if(r.init<0) {auto fallback=probe(row,loader,gpu,true,matrix);if(fallback.init>=0 && fallback.get>=0)accepted.push_back(fallback);}
            else if(r.get>=0)accepted.push_back(r);
        }
        if(argc>1 && std::string(argv[1])=="--matrix")return 0;
        std::ofstream summary(root+(only.empty()?"latency.csv":only+"-latency.csv"));summary<<"row,input,init,submitted,outputs,p50_ms,p95_ms,p99_ms,max_ms,avg_bytes,p99_bytes,max_bytes,idr_max_bytes,mbps,access_units,idr,sps,pps,invalid,busy,more_data,structural_valid\n";
        std::vector<std::string> desired={"60_cbr12_ldb","30_vbr12_ldb_maxframe","60_intra_vertical","60_vme_cbr12","30_intra_vertical","60_vme_intra_vertical","60_base_lp_cbr12","30_base_lp_cbr12"};
        if(!only.empty())desired={only};
        int done=0;
        for(auto& name:desired) {
            auto it=std::find_if(accepted.begin(),accepted.end(),[&](const Result& r){return r.row.name==name;});
            if(it==accepted.end()) {std::cout<<"LATENCY_UNSUPPORTED "<<name<<"\n";continue;}
            if(done>=4 && name!="60_vme_cbr12")continue;
            try {if(latency(*it,loader,gpu,frames,summary))++done;}catch(const std::exception& e){std::cout<<"LATENCY_FAILED "<<name<<" "<<e.what()<<"\n";}
        }
        std::cout<<"COMPLETE validated_combinations="<<done<<"\n";return done>=(only.empty()?4:1)?0:2;
    } catch(const std::exception& e) {std::cerr<<"FATAL "<<e.what()<<"\n";return 1;}
}
