#include "backend/cpu/CpuThreads.h"


namespace xmrig {


CpuThreads::CpuThreads(size_t count, uint32_t intensity)
{
    m_data.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        add(-1, intensity);
    }
}


} // namespace xmrig
