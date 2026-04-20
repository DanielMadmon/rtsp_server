// RTSP Server


#include <memory>
#include <chrono>
#include <csignal>
#include "lf_mpi_svc.hpp"

using namespace lf_mpi;
using namespace std::chrono;



/// TODO: create an rtsp daemon that use eventfd for gracefull shutdown
int main(int argc, char **argv)
{	
    log_level_set(LOG_DBG);
    
    flag stop_flag{false};
    LOGD("RK_MPI_SYS_Init done.");
    lf_mpi::luckfox_mpi_config config{};
    config.stop_flag = &stop_flag;
    lf_mpi_svc& mpi_svc = lf_mpi_svc::create_new(config);
    mpi_svc.init();
    LOGI("mpi_svc init done");
    /*
    while(!stop_flag.load(std::memory_order_acquire)){
        std::this_thread::sleep_for(milliseconds(10));
    }
    */
    std::this_thread::sleep_for(seconds(20));
    stop_flag.store(true,std::memory_order_seq_cst);
    mpi_svc.exit_svc();
	return 0;
}

