// realtime_scheduler.cpp
// build: g++ -std=c++14 -pthread … -lsnpe_*
#include <SNPE/SNPE.hpp>
#include <SNPE/SNPEFactory.hpp>

#include "DlContainer/IDlContainer.hpp"
#include "DlSystem/SNPEPerfProfile.h"
#include "LoadContainer.hpp"
#include "LoadInputTensor.hpp"
#include "PreprocessInput.hpp"
#include "SetBuilderOptions.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <random>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

using zdl::DlSystem::Runtime_t;
using Clock = std::chrono::steady_clock;

/* ───────────────────────────────── workload definitions ────────────────────────── */
struct ModelSpec { std::string dlc; double fps, prob; std::string list; };
using Scenario   = std::vector<ModelSpec>;

/* add / edit scenarios here ------------------------------------------------------- */
static const std::unordered_map<std::string, Scenario> kScenarios = {
    { "AR_Assistant", {
        { "models/KD_res8_narrow_quant.dlc",     3.0, 1.00, "input_lists/KD_res8_narrow.txt" },
        { "models/ASR_EM_24L_quant.dlc",        3.0, 0.50, "input_lists/ASR_EM_24L.txt"     },
        { "models/SS_HRViT_b1_quant.dlc",      10.0, 1.00, "input_lists/SS_HRViT_b1_quant.txt" },
        { "models/DE_midas_v21_small_quant.dlc",30.0, 1.00, "input_lists/DE_midas_v21_small.txt" },
        { "models/OD_D2go_FasterRCNN_quant.dlc",10.0, 1.00, "input_lists/OD_D2go_FasterRCNN.txt"}
    }}
};

static const std::vector<double> kScales = {0.5, 1.0, 1.5, 2.0};

/* ───────────────────────────────── scheduling policies ─────────────────────────── */
enum class Policy : int { CPU_ONLY, GPU_ONLY, DSP_ONLY, RANDOM, JSQ, DYNAMIC };
static const char* kPolName[] = { "CPU_ONLY","GPU_ONLY","DSP_ONLY","RANDOM","JSQ","DYNAMIC" };

/* ───────────────────────────────── global containers ───────────────────────────── */
static std::atomic<bool> gStop{false};
static std::atomic<int > gInFlight{0};

struct RtCtx  { std::unique_ptr<zdl::SNPE::SNPE> snpe;
                std::unique_ptr<zdl::DlSystem::ITensor> input; };
struct ModelCtx { std::array<RtCtx,3> rt; };                // 0=CPU 1=GPU 2=DSP

static std::unordered_map<std::string, ModelCtx>               gModelCtx;
static std::unordered_map<std::string, std::vector<Runtime_t>> gAvailRt;

/* ───────────────────────────────── lock‑free queues per runtime ─────────────────── */
struct Request { const ModelSpec* ms; Runtime_t rt; Clock::time_point dl; };

struct TSQueue {
    std::queue<Request> q; std::mutex m; std::condition_variable cv;
    void push(const Request& r){ { std::lock_guard<std::mutex> lk(m); q.push(r);} cv.notify_one(); }
    bool pop(Request& r){
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk,[&]{ return !q.empty() || gStop.load(); });
        if(q.empty()) return false;
        r = q.front(); q.pop(); return true;
    }
    void clear(){ std::lock_guard<std::mutex> lk(m); std::queue<Request>().swap(q); }
    size_t size(){ std::lock_guard<std::mutex> lk(m); return q.size(); }
};
static TSQueue queues[3];

/* ───────────────────────────────── latency tracker for DYNAMIC ─────────────────── */
struct LatRec { double avg = 1.0; void upd(double v){ avg = 0.9*avg + 0.1*v; } };
static std::unordered_map<std::string, std::array<LatRec,3>> gLat;

/* ───────────────────────────────── helpers ─────────────────────────────────────── */
inline const char* rtName(Runtime_t r){ return r==Runtime_t::CPU?"CPU":r==Runtime_t::GPU?"GPU":"DSP"; }
inline void pin(int core){ cpu_set_t s; CPU_ZERO(&s); CPU_SET(core,&s);
                           sched_setaffinity(static_cast<pid_t>(syscall(SYS_gettid)),sizeof(s),&s); }
inline bool exists(const std::string& p){ return access(p.c_str(),F_OK)==0; }

/* prepare one ITensor (robust) ---------------------------------------------------- */
static std::unique_ptr<zdl::DlSystem::ITensor>
prepInput(std::unique_ptr<zdl::SNPE::SNPE>& snpe, const std::string& list)
{
    auto batches = preprocessInput(list.c_str(),1);
    if(batches.empty()) throw std::runtime_error("empty input list "+list);

    const auto& namesOpt = snpe->getInputTensorNames();
    if(!namesOpt) throw std::runtime_error("null input‑name ptr");
    const auto& names = *namesOpt;
    if(names.size()!=1)
        throw std::runtime_error("model declares "+std::to_string(names.size())+" inputs");

    return loadInputTensor(snpe, batches[0], names);
}

/* preload DLCs once (with Init‑Caching) ------------------------------------------ */
static void preload()
{
    std::set<std::string> dlcs;
    for(auto& kv:kScenarios) for(auto& m:kv.second) dlcs.insert(m.dlc);

    std::cout<<"\n=== Pre‑loading "<<dlcs.size()<<" unique DLCs ===\n";
    for(const auto& dlc:dlcs)
    {
        std::cout<<"• "<<dlc<<": ";
        const ModelSpec* anySpec=nullptr;
        for(auto& kv:kScenarios) for(const auto& m:kv.second)
            if(m.dlc==dlc){ anySpec=&m; break; }
        if(!anySpec || !exists(dlc)){ std::cout<<"<file missing>\n"; continue; }

        auto& mc = gModelCtx[dlc];
        std::vector<const char*> ok;

        for(Runtime_t rt:{Runtime_t::CPU,Runtime_t::GPU,Runtime_t::DSP}){
            if(!zdl::SNPE::SNPEFactory::isRuntimeAvailable(rt)) continue;

            RtCtx ctx;
            if(auto cont = loadContainerFromFile(dlc)){
                zdl::DlSystem::PlatformConfig pc;
                ctx.snpe = setBuilderOptions(cont, rt, {}, false, pc,
                                             /*InitCache*/true,false,
                                             zdl::DlSystem::PerformanceProfile_t::HIGH_PERFORMANCE);
                if(ctx.snpe){
                    cont->save(dlc.c_str());               // create / update cache
                    try{ ctx.input = prepInput(ctx.snpe, anySpec->list); }
                    catch(...){ ctx.snpe.reset(); }
                }
            }
            if(ctx.snpe){
                mc.rt[int(rt)] = std::move(ctx);
                gAvailRt[dlc].push_back(rt);
                ok.push_back(rtName(rt));
            }
        }
        if(ok.empty()) std::cout<<"<no runtime>";
        else           for(size_t i=0;i<ok.size();++i) std::cout<<ok[i]<<(i+1==ok.size()?"":",");
        std::cout<<"\n";
    }
    std::cout<<"===========================================\n";
}

/* worker thread ------------------------------------------------------------------ */
static void worker(Runtime_t rt,int core)
{
    pin(core);
    for(Request rq; !gStop && queues[int(rt)].pop(rq); ){
        const RtCtx& ctx = gModelCtx[rq.ms->dlc].rt[int(rt)];
        if(!ctx.snpe){                            // runtime not available after all
            if(Clock::now()>rq.dl) ;              // miss counted in drain
            continue;
        }
        gInFlight++;
        auto t0 = Clock::now();
        zdl::DlSystem::TensorMap om;
        ctx.snpe->execute(ctx.input.get(), om);
        auto t1 = Clock::now();
        gInFlight--;
        gLat[rq.ms->dlc][int(rt)].upd(
            std::chrono::duration<double,std::milli>(t1-t0).count());
    }
}

/* selectors ---------------------------------------------------------------------- */
static Runtime_t pickJSQ(const std::vector<Runtime_t>& rts)
{
    Runtime_t best=rts[0]; size_t bq=queues[int(best)].size();
    auto pref = {Runtime_t::DSP,Runtime_t::GPU,Runtime_t::CPU};
    for(Runtime_t rt:rts){
        size_t q=queues[int(rt)].size();
        if(q<bq || (q==bq &&
           std::find(pref.begin(),pref.end(),rt)<std::find(pref.begin(),pref.end(),best)))
        { best=rt; bq=q; }
    }
    return best;
}
static Runtime_t pickDyn(const ModelSpec& m,const std::vector<Runtime_t>&rts,double slack)
{
    for(Runtime_t pref:{Runtime_t::DSP,Runtime_t::GPU,Runtime_t::CPU}){
        if(std::find(rts.begin(),rts.end(),pref)==rts.end()) continue;
        double qLat = queues[int(pref)].size()*gLat[m.dlc][int(pref)].avg;
        if(qLat <= slack) return pref;
    }
    return pickJSQ(rts);
}

/* run one scenario / scale / policy ---------------------------------------------- */
static double runOne(const Scenario& S, Policy pol, double scale,
                     std::chrono::seconds dur, std::mt19937& rng)
{
    for(auto& q:queues) q.clear();

    struct St{ const ModelSpec* ms; Clock::duration per; Clock::time_point next;
               std::bernoulli_distribution bern; };
    std::vector<St> st;
    auto start = Clock::now();
    for(auto& m:S){
        st.push_back({ &m,
                       std::chrono::duration_cast<Clock::duration>(
                           std::chrono::duration<double>(1.0/(m.fps*scale))),
                       start,
                       std::bernoulli_distribution(m.prob) });
    }
    const auto endTime = start + dur;
    uint64_t total=0, miss=0;

    std::uniform_int_distribution<int> randPick;  // re‑set per use

    while(Clock::now() < endTime){
        auto now = Clock::now();
        for(auto& s : st){
            if(now >= s.next){
                s.next += s.per;
                if(!s.bern(rng)) continue;

                const auto& rts = gAvailRt[s.ms->dlc];
                if(rts.empty()) continue;

                Runtime_t tgt = Runtime_t::CPU;
                switch(pol){
                    case Policy::CPU_ONLY:
                        tgt = (std::find(rts.begin(),rts.end(),Runtime_t::CPU)!=rts.end())
                              ? Runtime_t::CPU : rts[0]; break;
                    case Policy::GPU_ONLY:
                        tgt = (std::find(rts.begin(),rts.end(),Runtime_t::GPU)!=rts.end())
                              ? Runtime_t::GPU : rts[0]; break;
                    case Policy::DSP_ONLY:
                        tgt = (std::find(rts.begin(),rts.end(),Runtime_t::DSP)!=rts.end())
                              ? Runtime_t::DSP : rts[0]; break;
                    case Policy::RANDOM:
                        randPick.param(std::uniform_int_distribution<int>::param_type(0,int(rts.size())-1));
                        tgt = rts[randPick(rng)]; break;
                    case Policy::JSQ:
                        tgt = pickJSQ(rts); break;
                    case Policy::DYNAMIC:{
                        double slack = std::chrono::duration<double,std::milli>(s.per).count();
                        tgt = pickDyn(*s.ms, rts, slack);
                    } break;
                }
                queues[int(tgt)].push({s.ms,tgt,now+s.per});
                ++total;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    /* drain with watchdog (max 3 s) */
    auto wdEnd = Clock::now() + std::chrono::seconds(3);
    while((gInFlight.load()>0 ||
           std::any_of(std::begin(queues),std::end(queues),
                       [](TSQueue&q){return q.size();}))
           && Clock::now() < wdEnd)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    /* anything still sitting in queues is an automatic miss */
    for(auto& q:queues){
        std::lock_guard<std::mutex> lk(q.m);
        while(!q.q.empty()){ if(Clock::now()>q.q.front().dl) ++miss; q.q.pop(); }
    }
    return total ? 100.0*double(miss)/double(total) : 0.0;
}

/* main --------------------------------------------------------------------------- */
int main()
{
    const std::chrono::seconds simDur(15);
    std::mt19937 rng{std::random_device{}()};

    zdl::SNPE::SNPEFactory::initializeLogging(zdl::DlSystem::LogLevel_t::LOG_ERROR);
    preload();

    std::thread cpuT(worker, Runtime_t::CPU, 0);
    std::thread gpuT(worker, Runtime_t::GPU, 1);
    std::thread dspT(worker, Runtime_t::DSP, 2);

    std::ofstream csv("results.csv"); csv<<std::unitbuf;
    csv<<"scenario,scale,policy,miss_rate\n";

    for(const auto& sc : kScenarios){
        for(double scf : kScales){
            std::cout<<"\n>>> Scenario \""<<sc.first<<"\"   scale="<<scf<<"\n";
            for(Policy p : {Policy::CPU_ONLY,Policy::GPU_ONLY,Policy::DSP_ONLY,
                            Policy::RANDOM,Policy::JSQ,Policy::DYNAMIC})
            {
                std::cout<<"   "<<kPolName[int(p)]<<" ... "<<std::flush;
                double miss = runOne(sc.second, p, scf, simDur, rng);
                std::cout<<miss<<"%\n";
                csv<<sc.first<<','<<scf<<','<<kPolName[int(p)]<<','<<miss<<'\n';
            }
        }
    }

    gStop = true; for(auto& q:queues) q.cv.notify_all();
    cpuT.join(); gpuT.join(); dspT.join();

    std::cout<<"\nAll results written to results.csv\n";
    return 0;
}
