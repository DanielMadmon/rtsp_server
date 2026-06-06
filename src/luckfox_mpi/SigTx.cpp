#include "SigTx.hpp"

namespace lf_mpi{
namespace sig_tx{

SigTx::SigTx(atomic<bool>* sig_in):m_sig_force_set(false),m_sig_in([&]{
    if(!sig_in) throw NullSigPtrException{};
    return sig_in;
}()){}

bool SigTx::get() const
{
    bool current = m_sig_in->load(std::memory_order_acquire);
    if(!m_sig_force_set.get()){
        return current;
    }else{
        return true;
    }
}

void SigTx::force_set()
{
    m_sig_force_set.set(true);
}

}//namespace sig_tx
}//namespace lf_mpi