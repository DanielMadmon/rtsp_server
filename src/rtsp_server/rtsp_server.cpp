#include <memory>
#include <chrono>
#include <csignal>
#include <atomic>
#include "lf_mpi_svc.hpp"
#include "routing.hpp"
#include "config.h"
#include "generic_log.h"
using namespace lf_mpi;
using namespace std::chrono;

static volatile sig_atomic_t quit_flag = 0;

void signal_handler(int signal){
    quit_flag = 1;
}

int main(int argc, char **argv)
{	
    log_level_set(LOG_DBG);
    if(signal(SIGINT,signal_handler) == SIG_ERR){
        LOGE("failed to set SIGINT handler");
        return -1;
    }
    if(signal(SIGTERM,signal_handler) == SIG_ERR){
        LOGE("failed to set SIGTERM handler");
        return -1;
    }
    flag stop_flag{false};
    LOGD("RK_MPI_SYS_Init done.");
    lf_mpi::LuckfoxMpiConfig config{};
    config.stop_flag = &stop_flag;
    config.resize_vi_frame = false;
    config.rotate_vi_frame = false;
    config.crop_vi_frame   = false;
    config.rotation_opts = ROT_90;
    config.resize_or_crop_width = 1280;
    config.resize_or_crop_height = 720;
    config.vi_binding = routing::MpiViBindTo::OSD;
    MpiSvc& mpi_svc = MpiSvc::create_new(config);
    mpi_svc.init();
    LOGI("mpi_svc init done");
    
    while(!quit_flag){
        std::this_thread::sleep_for(milliseconds(10));
    }
    LOGI("got exit signal");
    stop_flag.store(true);
    mpi_svc.exit_svc();
	return 0;
}

