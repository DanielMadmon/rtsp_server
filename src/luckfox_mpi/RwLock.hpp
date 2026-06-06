#pragma once
#include <atomic>
#include <cstddef>
#include <shared_mutex>
#include <type_traits>
namespace lf_mpi{
namespace rw_lock{

using std::atomic;
using namespace std::chrono;


template <typename T, typename Enable = void>
class RwLock{
    static_assert(sizeof(T) == 0, "Type must be trivially copyable");
};

template<typename T>
class RwLock<T,std::enable_if_t<(atomic<T>::is_always_lock_free &&
                                 sizeof(T) <= 8 &&
                                 std::is_trivially_copyable_v<T>)>>{
    public:
    explicit RwLock(T t):m_data(t)
    {}
    T get() const{
        return m_data.load(std::memory_order_acquire);
    }
    void set(T t){
        m_data.store(t,std::memory_order_release);
    }
    private:
    atomic<T> m_data;
};

template<typename T>
class RwLock<T,std::enable_if_t<(sizeof(T) > 8) &&
             std::is_trivially_copyable_v<T>>>{
    public:
    explicit RwLock(T t):m_data(std::move(t))
    {}
    T get() const{
        std::shared_lock lock(m_mutex);
        return m_data;
    }
    void set(T t){
       std::unique_lock lock(m_mutex);
       m_data = std::move(t);
    }
    private:
    T m_data;
    mutable std::shared_mutex m_mutex;
};


template<typename T>
class RwLock<T,std::enable_if_t<(sizeof(T) > 8) &&
             std::is_trivially_copyable_v<T> && 
             !(atomic<T>::is_always_lock_free)>>{
    public:
    explicit RwLock(T t):m_data(std::move(t))
    {}
    T get() const{
        std::shared_lock lock(m_mutex);
        return m_data;
    }
    void set(T t){
       std::unique_lock lock(m_mutex);
       m_data = std::move(t);
    }
    private:
    T m_data;
    mutable std::shared_mutex m_mutex;
};

}
}//namespace lf_mpi


