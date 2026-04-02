#pragma once
#include <cstdint>
#include "rk_mpi_mmz.h"

template<typename T>class mmz_alloc{
    public:
        using is_always_equal = std::true_type;
        typedef T value_type;
        mmz_alloc() noexcept {}
        T* allocate(std::size_t nb){
            T* ptr = static_cast<T*>(mmz_allocate(nb));
            if(ptr){
                return into_virtual_address(ptr);
            }
            return ptr;
        }
        void deallocate(T* tp,std::size_t nb) noexcept{
            (void)nb;
            MB_BLK handle = vir_to_handle(tp);
            if(!handle){
                return;
            }
            mmz_deallocate(handle);
        }
        T* into_virtual_address(T* tp){
            return static_cast<T*>(
                mmz_to_vir_addr(tp)
            );
        }
        static std::uint64_t virtual_to_physical_address(T* tp){
            return into_physical_address(vir_to_handle(tp));
        }
        static std::uint64_t into_physical_address(MB_BLK handle){
            return mmz_to_phy_addr(handle);
        }
        static int32_t into_fd(T* tp){
            return mmz_to_fd(tp);
        }
        static MB_BLK vir_to_handle(T* tp){
            return RK_MPI_MMZ_VirAddr2Handle(tp);
        }
        template<typename U>
        struct rebind {
            using other = mmz_alloc<U>;
        };
        bool operator==(const mmz_alloc&) const { return true; }
        bool operator!=(const mmz_alloc&) const { return false; }
    private:
        void* mmz_allocate(std::size_t nb){
            MB_BLK ret = nullptr;
            int32_t res = RK_MPI_MMZ_Alloc(&ret,nb,RK_MMZ_ALLOC_TYPE_CMA|RK_MMZ_SYNC_RW);
            if(res != RK_SUCCESS || !ret){
                return nullptr;
            }
            else{
                return ret;
            }
        }
        void mmz_deallocate(MB_BLK mb){
            RK_MPI_MMZ_Free(mb);
        }
        void* mmz_to_vir_addr(T* tp){
            return RK_MPI_MMZ_Handle2VirAddr(tp);
        }
        static std::uint64_t mmz_to_phy_addr(MB_BLK handle){
            return RK_MPI_MMZ_Handle2PhysAddr(handle);
        }
        static std::int32_t mmz_to_fd(T* tp){
            return RK_MPI_MMZ_Handle2Fd(tp);
        }
        
};

