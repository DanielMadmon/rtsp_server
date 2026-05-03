#pragma once

namespace lf_mpi{
namespace routing{
    
    enum class MpiViBindTo{
        /// osd receiver which uses LuckoxMpi::vi_get_frame
        OSD,
        /// receiver is hardware video encoder.
        VENC
    };
}
}