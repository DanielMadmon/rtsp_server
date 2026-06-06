#pragma once
#include <atomic>
#include <exception>
#include "RwLock.hpp"
namespace lf_mpi{

namespace sig_tx{
using std::atomic;
using rw_lock::RwLock;
using std::exception;

class NullSigPtrException : public exception{
    public:
    NullSigPtrException() = default;
    const char* what() const noexcept{
        return m_msg;
    }

    private:
    static inline const char m_msg[]{"nullptr passed to SigTx constructor"};
};

///@brief atomic<bool> signal propogator 
///with option for setting 
///regardless of source signal
class SigTx{

    public:
    /// @brief construct SigTx will throw if sig_in is null
    /// @param sig_in non-null pointer to upstream signal.
    /// @note - sig_in must stay valid for the entire lifetime
    /// of SigTx
    SigTx(atomic<bool>* sig_in);
    /// @brief get current signal value
    /// @return bool
    bool get() const;
    /// @brief force set signal to true.
    /// next call to get will return true
    void force_set();

    private:
    RwLock<bool> m_sig_force_set;
    const atomic<bool>* m_sig_in;
};

}//namespace sig_tx
}//namespace lf_mpi