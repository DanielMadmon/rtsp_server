// RTSP Server


#include <thread>
#include <memory>
#include <iostream>
#include <time.h>
#include <iomanip>
#include <string>
#include "xop/RtspServer.h"
#include "net/Timer.h"
#include "bs.h"
#include "h265_stream.h"
#include <csignal>
#include "sample_comm.h"
#include "luckfox_mpi.hpp"
#include "generic_log.h"
#include "config.h"
#include "osd.hpp"
#include "mmz_alloc.hpp"
#include "lf_mpi_svc.hpp"

using namespace lf_mpi;
static flag stop_flag{false};

void signal_handler(int sig)
{
    stop_flag.store(true);
}

inline void set_signal_handler(__sighandler_t handler);

int main(int argc, char **argv)
{	
    set_signal_handler(signal_handler);
    log_level_set(LOG_DBG);
    lf_mpi::luckfox_mpi_config config{};
    config.stop_flag = &stop_flag;
    lf_mpi_svc& mpi_svc = lf_mpi_svc::create_new(config);
    LOGD("exiting rtsp server");
    lf_mpi_svc::exit_svc();
	return 0;
}



inline void set_signal_handler(__sighandler_t handler)
{
    signal(SIGINT,handler);
    signal(SIGKILL,handler);
    signal(SIGSTOP,handler);
    signal(SIGABRT,handler);
    signal(SIGSEGV,handler);
    signal(SIGTERM,handler);
    signal(SIGQUIT,handler);
}