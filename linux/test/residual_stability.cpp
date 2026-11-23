// Lighting-off oracle for NR cadence/photometry. No learned weights or Relight.
#define NR_DITTO_FIXTURE_LIBRARY
#include "dittosr_stage.cpp"
#include "nr_seamless.hpp"
#include <filesystem>
#include <numeric>

struct ResidualTest : Test {
    nrvk::Context ctx;
    ResidualTest() {
        ctx.instance=host.instance;ctx.physical=host.physical;ctx.device=host.device;
        ctx.queue=host.queue;ctx.family=host.queue_family;ctx.mem=mem;ctx.adopted=true;
    }
    ~ResidualTest(){ctx.destroy();}
    void run(const std::string& shaders,const std::filesystem::path& out,const std::string& kind,unsigned interval) {
        const unsigned W=96,H=64,F=48;
        auto shown=ctx.image(W,H,VK_FORMAT_R32G32B32A32_SFLOAT,true);
        auto answer=ctx.image(W,H,VK_FORMAT_R32G32B32A32_SFLOAT,false);
        auto motion=ctx.image(W,H,VK_FORMAT_R32G32_SFLOAT,false);
        auto depth=ctx.image(W,H,VK_FORMAT_R32_SFLOAT,true);
        nr::SeamlessHistory history;history.create(ctx,shaders,shown,answer,motion,depth);
        nr::TemporalCadence cadence;
        std::vector<float> input(W*H*4),fresh(input.size()),actual(input.size()),mv(W*H*2,0),z(W*H);
        std::vector<double> errors,gains;double mse=0,alpha_error=0,peak_instability=0;unsigned samples=0;
        for(unsigned f=0;f<F;f++) {
            const bool full=cadence.next(interval);
            const bool cut=kind=="cut"&&f==13;
            const float exposure=kind=="exposure"?(f%2?1.08f:1.f):kind=="large_exposure"?(f%2?1.5f:1.f):kind=="step"&&f>=24?1.8f:1.f;
            const float noise=kind=="oscillation"?std::exp2(((f/interval)%2?1.f:-1.f)*.04f):1.f;
            const float true_gain=kind=="step"&&f>=24?1.6f:1.25f;
            const bool invalid=kind=="invalid"&&f==15;
            for(unsigned y=0;y<H;y++) for(unsigned x=0;x<W;x++) {
                const unsigned i=y*W+x,k=4*i;
                const float tex=.25f+.025f*std::sin(float(x)*.7f)*std::cos(float(y)*.6f);
                const float colors[3]={kind=="confidence"?1.3f:true_gain,kind=="confidence"?1.1f:true_gain,kind=="confidence"?.8f:true_gain};
                for(unsigned c=0;c<3;c++) {input[k+c]=tex*exposure;fresh[k+c]=input[k+c]*colors[c]*noise;}
                input[k+3]=fresh[k+3]=.375f;
                z[i]=kind=="confidence"&&(f%2)?.7225f:.7f;
                mv[2*i]=invalid?std::nanf(""):0.f;mv[2*i+1]=0;
            }
            ctx.upload(shown,input.data(),input.size()*4);ctx.upload(motion,mv.data(),mv.size()*4);ctx.upload(depth,z.data(),z.size()*4);
            if(full) ctx.upload(answer,fresh.data(),fresh.size()*4);
            nr::SeamlessHistory::Push p{W,H,0,uint32_t(history.have&&!cut),1,1,.05f,.08f,1,0,1,0,0,cadence.phase};
            ctx.one_shot([&](VkCommandBuffer cmd){history.project(cmd,p);history.finish(cmd,p,full);});
            ctx.download(answer,actual.data(),actual.size()*4);
            double error=0,gain=0;unsigned count=0;
            for(unsigned y=8;y<H-8;y++) for(unsigned x=8;x<W-8;x++) {
                const unsigned k=4*(y*W+x);
                for(unsigned c=0;c<4;c++) assert(std::isfinite(actual[k+c]));
                alpha_error=std::max(alpha_error,double(std::abs(actual[k+3]-input[k+3])));
                const float colors[3]={kind=="confidence"?1.3f:true_gain,kind=="confidence"?1.1f:true_gain,kind=="confidence"?.8f:true_gain};
                if(f>=12&&!(kind=="cut"&&f==13)&&!invalid) for(unsigned c=0;c<3;c++) {
                    double d=actual[k+c]-input[k+c]*colors[c];mse+=d*d;samples++;
                }
                double a=.2126*actual[k]+.7152*actual[k+1]+.0722*actual[k+2];
                double b=.2126*input[k]+.7152*input[k+1]+.0722*input[k+2];
                double expected_y=.2126*input[k]*colors[0]+.7152*input[k+1]*colors[1]+.0722*input[k+2]*colors[2];
                error+=std::log2(std::max(a,1e-8)/expected_y);gain+=std::log2(std::max(a,1e-8)/b);count++;
                if(cut) for(unsigned c=0;c<4;c++) assert(actual[k+c]==input[k+c]);
            }
            errors.push_back(error/count);gains.push_back(gain/count);
#ifndef RESIDUAL_BASELINE
            if(kind=="step"&&f==26) assert(std::abs(errors.back())<.02); // third real frame after a true lighting change
            std::vector<_Float16> stats(W*H*4);
            ctx.download(history.statistics[history.current],stats.data(),stats.size()*2);
            for(unsigned k=3;k<stats.size();k+=4) peak_instability=std::max(peak_instability,double(stats[k]));
#endif
            printf("%s N=%u frame=%u full=%d log_gain=%g error_stops=%g\n",kind.c_str(),interval,f,full,gains.back(),errors.back());
        }
        const auto rms=[](const std::vector<double>& v){double s=0;for(double x:v)s+=x*x;return std::sqrt(s/v.size());};
        std::vector<double> steady(errors.begin()+16,errors.end());
        if(kind=="cut"||kind=="invalid") {steady.clear();for(unsigned f=18;f<F;f++)steady.push_back(errors[f]);}
        if(kind=="step") steady.assign(errors.begin()+32,errors.end());
        const double luma_rms=rms(steady),rgb_rms=std::sqrt(mse/samples);
        printf("RESULT %s N=%u luma_rms_stops=%.8f rgb_rmse=%.8f alpha=%g instability=%g full=%llu reconstructed=%llu\n",kind.c_str(),interval,luma_rms,rgb_rms,alpha_error,peak_instability,(unsigned long long)cadence.full,(unsigned long long)cadence.reconstructed);fflush(stdout);
        assert(alpha_error==0&&cadence.full==F/interval&&cadence.unexpected==0);
#ifndef RESIDUAL_BASELINE
        if(kind=="oscillation") {assert(luma_rms<.015&&peak_instability>.5);}
        else if(kind=="confidence") assert(rgb_rms<.002);
        else assert(luma_rms<.006);
#endif
        std::filesystem::create_directories(out);
        std::ofstream result(out/(kind+"-"+std::to_string(interval)+".txt"));result<<luma_rms<<" "<<rgb_rms<<" "<<peak_instability<<"\n";
        history.release();ctx.destroy(shown);ctx.destroy(answer);ctx.destroy(motion);ctx.destroy(depth);
    }
};
int main(int argc,char** argv) try {
    if(argc!=3){puts("usage: residual-stability <runtime shader directory> <results>");return 2;}
#ifndef RESIDUAL_BASELINE
    nr::SeamlessHistory::check_shader(std::filesystem::path(argv[1])/"runtime_seamless.spv");
#endif
    ResidualTest t;
    for(unsigned n:{2u,3u,4u}) for(const char* s:{"exposure","large_exposure","confidence","oscillation","step","cut","invalid"})t.run(argv[1],argv[2],s,n);
    puts("Residual stability checks passed (Lighting off, analytic network)");return 0;
} catch(const std::exception& e){fprintf(stderr,"%s\n",e.what());return 1;}
