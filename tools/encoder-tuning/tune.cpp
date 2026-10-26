// S7-derived external D3D11 input path; all measurement artifacts are task-local.
#include "s7_support.h"
#include "codec_api.h"
#include <thread>

struct Options {
    std::map<std::string,std::string> a;
    Options(int argc,char** argv) { for(int i=1;i<argc;i+=2) { if(i+1>=argc)throw std::runtime_error("Expected --key value"); a[argv[i]]=argv[i+1]; } }
    std::string str(std::string k,std::string d="")const {auto i=a.find("--"+k);return i==a.end()?d:i->second;}
    int num(std::string k,int d)const {return std::stoi(str(k,std::to_string(d)));}
    double dec(std::string k,double d)const {return std::stod(str(k,std::to_string(d)));}
};
static void extraFields(std::ostream& f,const Params& p) {
    f<<fields(p)<<"ICQQuality="<<p.p.mfx.ICQQuality<<";QVBRQuality="<<p.c3.QVBRQuality
     <<";IntRefCycleDist="<<p.c3.IntRefCycleDist<<";ScenarioInfo="<<p.c3.ScenarioInfo
     <<";BRCPanicMode="<<p.c3.BRCPanicMode<<";WinBRCSize="<<p.c3.WinBRCSize<<";WinBRCMaxAvgKbps="<<p.c3.WinBRCMaxAvgKbps
     <<";MaxFrameSizeI="<<p.c3.MaxFrameSizeI<<";MaxFrameSizeP="<<p.c3.MaxFrameSizeP
     <<";MaxQPI="<<(int)p.c2.MaxQPI<<";MaxQPP="<<(int)p.c2.MaxQPP<<";MBBRC="<<p.c2.MBBRC<<";";
}
static void limitCPU() {
    DWORD_PTR process=0,system=0;GetProcessAffinityMask(GetCurrentProcess(),&process,&system);
    DWORD_PTR selected=0;int n=0;for(unsigned i=0;i<sizeof(DWORD_PTR)*8 && n<8;++i)if(process&(DWORD_PTR(1)<<i)){selected|=DWORD_PTR(1)<<i;++n;}
    SetProcessAffinityMask(GetCurrentProcess(),selected);
    SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);
}
static void readFrame(std::ifstream& in,std::vector<uint8_t>& nv12) {
    in.read((char*)nv12.data(),nv12.size());if(in.gcount()!=(std::streamsize)nv12.size())throw std::runtime_error("Short source frame");
}
static int vpl(const Options& o) {
    GPU gpu;Loader loader(gpu);Session session(loader,gpu);
    Row r;r.fps=o.num("fps",60);r.kbps=o.num("kbps",12000);r.lp=o.num("lp",1)?MFX_CODINGOPTION_ON:MFX_CODINGOPTION_OFF;
    r.tu=o.num("tu",7);r.vbv=o.num("vbv",1);r.slices=o.num("slices",1);r.intra=o.num("intra",0);r.qp=r.intra?o.num("intra-qp",-2):0;
    r.ldb=o.num("ldb",0)?MFX_CODINGOPTION_ON:MFX_CODINGOPTION_OFF;
    std::string mode=o.str("mode","cbr"),profile=o.str("profile","cb");
    r.rc=mode=="vbr"?MFX_RATECONTROL_VBR:mode=="qvbr"?MFX_RATECONTROL_QVBR:mode=="icq"?MFX_RATECONTROL_ICQ:MFX_RATECONTROL_CBR;
    r.profile=profile=="main"?MFX_PROFILE_AVC_MAIN:profile=="high"?MFX_PROFILE_AVC_HIGH:MFX_PROFILE_AVC_CONSTRAINED_BASELINE;
    Params wanted(r);wanted.p.mfx.GopPicSize=(mfxU16)o.num("gop",65535);wanted.p.mfx.NumRefFrame=(mfxU16)o.num("refs",1);
    wanted.p.mfx.BufferSizeInKB=(mfxU16)o.num("buffer",wanted.p.mfx.BufferSizeInKB);
    wanted.p.mfx.InitialDelayInKB=(mfxU16)o.num("initial",wanted.p.mfx.InitialDelayInKB);
    wanted.c2.IntRefCycleSize=r.intra?(mfxU16)o.num("cycle",r.fps):0;
    wanted.c2.MaxFrameSize=(mfxU32)(o.dec("cap",0)*r.kbps*1000/(8*r.fps));
    wanted.c2.AdaptiveI=o.num("adaptive-i",0)?MFX_CODINGOPTION_ON:MFX_CODINGOPTION_OFF;
    wanted.c3.IntRefCycleDist=(mfxU16)o.num("intra-dist",0);
    wanted.c3.ScenarioInfo=(mfxU16)o.num("scenario",0);
    wanted.c3.BRCPanicMode=(mfxU16)o.num("panic",0);
    wanted.c3.WinBRCSize=(mfxU16)o.num("winbrc",0);if(wanted.c3.WinBRCSize)wanted.c3.WinBRCMaxAvgKbps=(mfxU16)r.kbps;
    wanted.c3.MaxFrameSizeI=(mfxU32)(o.dec("cap-i",0)*r.kbps*1000/(8*r.fps));
    wanted.c3.MaxFrameSizeP=(mfxU32)(o.dec("cap-p",0)*r.kbps*1000/(8*r.fps));
    wanted.c2.MaxQPI=(mfxU8)o.num("maxqp",0);wanted.c2.MaxQPP=(mfxU8)o.num("maxqp",0);
    wanted.c2.MBBRC=(mfxU16)o.num("mbbrc",0);
    if(mode=="qvbr")wanted.c3.QVBRQuality=(mfxU16)o.num("quality",26);
    if(mode=="icq")wanted.p.mfx.ICQQuality=(mfxU16)o.num("quality",26);
    Params queried(wanted),actual;int qs=MFXVideoENCODE_Query(session.s,&wanted.p,&queried.p);
    Params init(wanted);int is=MFXVideoENCODE_Init(session.s,&init.p),gs=-999;
    if(is>=0)gs=MFXVideoENCODE_GetVideoParam(session.s,&actual.p);
    std::string base=root+"results/"+o.str("name","trial");
    std::ofstream settings(base+"-params.csv");settings<<"phase,status,fields\nrequested,0,\"";extraFields(settings,wanted);
    settings<<"\"\nquery,"<<qs<<",\"";extraFields(settings,queried);settings<<"\"\ninit,"<<is<<",\"\"\nreadback,"<<gs<<",\"";extraFields(settings,actual);settings<<"\"\n";settings.close();
    std::cout<<"STATUS Query="<<qs<<" Init="<<is<<" Get="<<gs<<" changes="<<diffs(wanted,actual)<<"\n";
    if(is<0 || gs<0)return 2;
    int frames=o.num("frames",600);if(frames==0){MFXVideoENCODE_Close(session.s);return 0;}
    mfxFrameAllocRequest req{};check(MFXVideoENCODE_QueryIOSurf(session.s,&actual.p,&req),"QueryIOSurf");
    mfxFrameAllocResponse res{};check(Allocator::alloc(&session.allocator,&req,&res),"input allocation");
    std::vector<mfxFrameSurface1> surfaces(res.NumFrameActual);
    for(size_t i=0;i<surfaces.size();++i){surfaces[i].Info=actual.p.mfx.FrameInfo;surfaces[i].Data.MemId=res.mids[i];}
    std::ifstream source(o.str("input"),std::ios::binary);if(!source)throw std::runtime_error("Cannot open input");
    std::ofstream bitsout(base+".h264",std::ios::binary),csv(base+"-frames.csv");
    csv<<"input,latency_ms,bytes,frame_type,timestamp,encode_status,sync_status,upload_ms\n";
    std::vector<uint8_t> nv12(1920*1080*3/2),padded(1920*1088*3/2,128),bits(8*1024*1024);
    D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT,0};ComPtr<ID3D11Query> uploaded;hrcheck(gpu.device->CreateQuery(&qd,&uploaded),"upload query");
    int outputs=0,more=0;std::map<mfxU64,double> pending;double start=tick();
    for(int frame=0;frame<frames+100;++frame) {
        mfxFrameSurface1* s=nullptr;double upload=0;
        if(frame<frames) {
            if(o.num("paced",0)) {double due=start+frame*1000.0/r.fps;while(tick()<due)std::this_thread::sleep_for(std::chrono::microseconds(100));}
            readFrame(source,nv12);size_t i=0;while(i<surfaces.size() && surfaces[i].Data.Locked)++i;
            if(i==surfaces.size())throw std::runtime_error("All surfaces locked");s=&surfaces[i];
            memcpy(padded.data(),nv12.data(),1920*1080);memset(padded.data()+1920*1080,16,1920*8);
            memcpy(padded.data()+1920*1088,nv12.data()+1920*1080,1920*540);
            double t=tick();gpu.ctx->UpdateSubresource(((Texture*)s->Data.MemId)->tex.Get(),0,nullptr,padded.data(),1920,0);
            gpu.ctx->End(uploaded.Get());gpu.ctx->Flush();
            while(gpu.ctx->GetData(uploaded.Get(),nullptr,0,0)==S_FALSE){if(tick()-t>5000)throw std::runtime_error("Upload timeout");Sleep(0);}
            upload=tick()-t;s->Data.TimeStamp=(mfxU64)frame*90000/r.fps;s->Data.FrameOrder=frame;
        }
        mfxEncodeCtrl ctrl{};if(frame==o.num("force-idr",-1))ctrl.FrameType=MFX_FRAMETYPE_I|MFX_FRAMETYPE_IDR|MFX_FRAMETYPE_REF;
        mfxBitstream bs{};bs.Data=bits.data();bs.MaxLength=(mfxU32)bits.size();mfxSyncPoint sync=nullptr;
        double t=tick();if(s)pending[s->Data.TimeStamp]=t;mfxStatus status;
        do {status=MFXVideoENCODE_EncodeFrameAsync(session.s,&ctrl,s,&bs,&sync);if(status==MFX_WRN_DEVICE_BUSY)Sleep(1);if(tick()-t>10000)throw std::runtime_error("Busy timeout");}while(status==MFX_WRN_DEVICE_BUSY);
        if(status==MFX_ERR_MORE_DATA){if(s){++more;continue;}break;}
        check(status,"Encode");if(!sync)throw std::runtime_error("No sync point");
        mfxStatus synced=MFXVideoCORE_SyncOperation(session.s,sync,10000);double end=tick();check(synced,"Sync");
        auto pt=pending.find(bs.TimeStamp);if(pt==pending.end())throw std::runtime_error("Unknown output timestamp");
        int input=(int)(bs.TimeStamp*r.fps/90000);double latency=end-pt->second;pending.erase(pt);
        csv<<input<<","<<std::setprecision(9)<<latency<<","<<bs.DataLength<<","<<bs.FrameType<<","<<bs.TimeStamp<<","<<status<<","<<synced<<","<<upload<<"\n";
        bitsout.write((char*)bs.Data+bs.DataOffset,bs.DataLength);++outputs;
    }
    MFXVideoENCODE_Close(session.s);Allocator::freeframes(&session.allocator,&res);
    std::cout<<"DONE frames="<<frames<<" outputs="<<outputs<<" more_data="<<more<<"\n";return outputs==frames && pending.empty()?0:3;
}
static int openh264(const Options& o) {
    HMODULE dll=LoadLibraryW(L"C:/src/tmp/t1-encoder-tuning/bin/openh264.dll");if(!dll)throw std::runtime_error("Cannot load OpenH264 DLL");
    auto create=(int(*)(ISVCEncoder**))GetProcAddress(dll,"WelsCreateSVCEncoder");
    auto destroy=(void(*)(ISVCEncoder*))GetProcAddress(dll,"WelsDestroySVCEncoder");
    if(!create || !destroy)throw std::runtime_error("OpenH264 exports missing");
    ISVCEncoder* encoder=nullptr;if(create(&encoder))throw std::runtime_error("WelsCreateSVCEncoder");
    SEncParamExt p{};encoder->GetDefaultParams(&p);
    int fps=o.num("fps",60),kbps=o.num("kbps",12000),frames=o.num("frames",600);
    p.iUsageType=o.str("usage","screen")=="screen"?SCREEN_CONTENT_REAL_TIME:CAMERA_VIDEO_REAL_TIME;
    p.iPicWidth=1920;p.iPicHeight=1080;p.fMaxFrameRate=(float)fps;p.iTargetBitrate=kbps*1000;p.iMaxBitrate=kbps*1000;
    p.iRCMode=(RC_MODES)o.num("rc",1);p.iMultipleThreadIdc=(unsigned short)o.num("threads",4);
    p.iComplexityMode=(ECOMPLEXITY_MODE)o.num("complexity",0);p.uiIntraPeriod=o.num("gop",0);
    p.iTemporalLayerNum=1;p.iSpatialLayerNum=1;p.iNumRefFrame=o.num("refs",1);
    p.bEnableFrameSkip=o.num("skip",0)!=0;p.bEnableDenoise=false;p.bEnableLongTermReference=false;
    p.bEnableSceneChangeDetect=o.num("scene",0)!=0;p.bEnableBackgroundDetection=o.num("background",1)!=0;
    p.bEnableAdaptiveQuant=o.num("aq",0)!=0;p.bEnableSSEI=false;p.bPrefixNalAddingCtrl=false;
    p.eSpsPpsIdStrategy=CONSTANT_ID;p.iPaddingFlag=0;
    p.iIdrBitrateRatio=o.num("idr-ratio",p.iIdrBitrateRatio);
    p.bFixRCOverShoot=o.num("fix-overshoot",p.bFixRCOverShoot)!=0;
    auto& l=p.sSpatialLayers[0];l.iVideoWidth=1920;l.iVideoHeight=1080;l.fFrameRate=(float)fps;l.iSpatialBitrate=kbps*1000;l.iMaxSpatialBitrate=kbps*1000;
    l.uiProfileIdc=PRO_BASELINE;l.uiLevelIdc=fps==60?LEVEL_4_2:LEVEL_4_0;
    int slices=o.num("slices",1);l.sSliceArgument.uiSliceMode=slices>1?SM_FIXEDSLCNUM_SLICE:SM_SINGLE_SLICE;l.sSliceArgument.uiSliceNum=slices;
    std::string base=root+"results/"+o.str("name","trial");int status=encoder->InitializeExt(&p);
    std::ofstream params(base+"-params.csv");params<<"phase,status,fields\ninit,"<<status<<",\"usage="<<p.iUsageType<<";rc="<<p.iRCMode<<";threads="<<p.iMultipleThreadIdc<<";slices="<<slices<<";complexity="<<p.iComplexityMode<<";skip="<<p.bEnableFrameSkip<<";scene="<<p.bEnableSceneChangeDetect<<";\"\n";
    if(status){destroy(encoder);return 2;}
    SEncParamExt actual{};int get=encoder->GetOption(ENCODER_OPTION_SVC_ENCODE_PARAM_EXT,&actual);
    params<<"readback,"<<get<<",\"usage="<<actual.iUsageType<<";rc="<<actual.iRCMode<<";threads="<<actual.iMultipleThreadIdc<<";slices="<<actual.sSpatialLayers[0].sSliceArgument.uiSliceNum<<";refs="<<actual.iNumRefFrame<<";skip="<<actual.bEnableFrameSkip<<";scene="<<actual.bEnableSceneChangeDetect<<";aq="<<actual.bEnableAdaptiveQuant<<";background="<<actual.bEnableBackgroundDetection<<";idr_ratio="<<actual.iIdrBitrateRatio<<";fix_overshoot="<<actual.bFixRCOverShoot<<";minqp="<<actual.iMinQp<<";maxqp="<<actual.iMaxQp<<";\"\n";
    std::ifstream source(o.str("input"),std::ios::binary);if(!source)throw std::runtime_error("Cannot open source");
    std::vector<uint8_t> nv12(1920*1080*3/2),i420(nv12.size());
    std::ofstream bitout(base+".h264",std::ios::binary),csv(base+"-frames.csv");csv<<"input,latency_ms,bytes,frame_type,timestamp,encode_status,sync_status,upload_ms\n";
    double start=tick();for(int f=0;f<frames;++f){
        if(o.num("paced",0)){double due=start+f*1000.0/fps;while(tick()<due)std::this_thread::sleep_for(std::chrono::microseconds(100));}
        readFrame(source,nv12);memcpy(i420.data(),nv12.data(),1920*1080);
        for(int j=0;j<1920*540/2;++j){i420[1920*1080+j]=nv12[1920*1080+2*j];i420[1920*1080+1920*540/2+j]=nv12[1920*1080+2*j+1];}
        SSourcePicture pic{};pic.iColorFormat=videoFormatI420;pic.iPicWidth=1920;pic.iPicHeight=1080;pic.iStride[0]=1920;pic.iStride[1]=pic.iStride[2]=960;
        pic.pData[0]=i420.data();pic.pData[1]=i420.data()+1920*1080;pic.pData[2]=pic.pData[1]+960*540;pic.uiTimeStamp=(long long)f*1000/fps;
        if(f==o.num("force-idr",-1))encoder->ForceIntraFrame(true);
        SFrameBSInfo bs{};double t=tick();int s=encoder->EncodeFrame(&pic,&bs);double elapsed=tick()-t;if(s)throw std::runtime_error("OpenH264 EncodeFrame="+std::to_string(s));
        int size=0;for(int j=0;j<bs.iLayerNum;++j){auto& layer=bs.sLayerInfo[j];int len=0;for(int k=0;k<layer.iNalCount;++k)len+=layer.pNalLengthInByte[k];bitout.write((char*)layer.pBsBuf,len);size+=len;}
        csv<<f<<","<<std::setprecision(9)<<elapsed<<","<<size<<","<<bs.eFrameType<<","<<pic.uiTimeStamp<<","<<s<<",0,0\n";
    }
    encoder->Uninitialize();destroy(encoder);FreeLibrary(dll);std::cout<<"DONE OpenH264 frames="<<frames<<"\n";return 0;
}
int main(int argc,char** argv){std::cout<<std::unitbuf;try{limitCPU();Options o(argc,argv);return o.str("backend","vpl")=="openh264"?openh264(o):vpl(o);}catch(const std::exception& e){std::cerr<<"ERROR "<<e.what()<<"\n";return 1;}}
