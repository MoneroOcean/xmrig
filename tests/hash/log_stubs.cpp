#include "base/io/log/Log.h"
#include "base/io/log/Tags.h"

#include <cstddef>

#include "crypto/randomx/blake2/blake2.h"

#include <cstdarg>
#include <cstdint>


void (*rx_blake2b_compress)(blake2b_state *S, const uint8_t *block) = rx_blake2b_compress_integer;
int (*rx_blake2b)(void *out, size_t outlen, const void *in, size_t inlen) = rx_blake2b_default;


extern "C" uint64_t uv_hrtime()
{
    return 0;
}


namespace xmrig {


bool Log::m_background = false;
bool Log::m_colors     = false;
LogPrivate *Log::d     = nullptr;
uint32_t Log::m_verbose = 0;


void Log::add(ILogBackend *) {}
void Log::destroy() {}
void Log::init() {}
void Log::print(const char *, ...) {}
void Log::print(Level, const char *, ...) {}


const char *Tags::cpu()
{
    return "cpu";
}


} // namespace xmrig
