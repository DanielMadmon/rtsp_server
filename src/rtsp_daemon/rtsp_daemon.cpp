#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <thread>
#include <chrono>
#include <memory>
#include <unistd.h>
using namespace std::chrono;

volatile sig_atomic_t quit_flag = 0;

void signal_handler(int signum){
    quit_flag = 1;
}


int main(int argc, char **argv){
    if (argc < 2) return 1;
    int efd = atoi(argv[1]);
    if(efd < 0) return 1;

    struct sigaction sa;
    memset(&sa,0,sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,&sa,nullptr);
    sigaction(SIGTERM,&sa,nullptr);

    const uint64_t event_flag = 1;
    while(!quit_flag){
        std::this_thread::sleep_for(milliseconds(100));
    }
    write(efd,&event_flag,sizeof(event_flag));
    return 0;
}