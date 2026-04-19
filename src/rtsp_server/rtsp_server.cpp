// RTSP Server


#include <memory>
#include <chrono>
#include <csignal>
#include "lf_mpi_svc.hpp"

using namespace lf_mpi;
using namespace std::chrono;


static volatile std::sig_atomic_t quit_flag = 0;

void signal_handler(int sig)
{
    quit_flag = 1;
}


int main(int argc, char **argv)
{	
    log_level_set(LOG_DBG);
    struct sigaction sa;
    
    // Clear signal mask and set handler
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // No special flags
    
    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        LOGE("sigaction failed");
        return 1;
    }

    flag stop_flag{false};

    int32_t result = RK_MPI_SYS_Init();
    if(result != RK_SUCCESS){
        LOGE("failed to initialize mpi_sys.\
             line: %d,file:%s",__LINE__,__FILE__);
             return -1;
    }
    LOGD("RK_MPI_SYS_Init done.");
    lf_mpi::luckfox_mpi_config config{};
    config.stop_flag = &stop_flag;
    lf_mpi_svc& mpi_svc = lf_mpi_svc::create_new(config);
    mpi_svc.init();
    LOGI("mpi_svc init done");
    while(!quit_flag){
        std::this_thread::sleep_for(milliseconds(10));
    }

    stop_flag.store(true,std::memory_order_seq_cst);
    mpi_svc.exit_svc();
    RK_MPI_SYS_Exit();
	return 0;
}

