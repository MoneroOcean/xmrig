#include "../cn/algorithm.cl"

#if ((ALGO == ALGO_RX_0) || (ALGO == ALGO_RX_YADA))
#include "randomx_constants_monero.h"
#elif (ALGO == ALGO_RX_WOW)
#include "randomx_constants_wow.h"
#elif (ALGO == ALGO_RX_ARQ)
#include "randomx_constants_arqma.h"
#elif (ALGO == ALGO_RX_XEQ)
#include "randomx_constants_equilibria.h"
#elif (ALGO == ALGO_RX_KEVA)
#include "randomx_constants_keva.h"
#elif (ALGO == ALGO_RX_GRAFT)
#include "randomx_constants_graft.h"
#endif

#include "aes.cl"
#include "blake2b.cl"
#include "randomx_vm.cl"
#include "randomx_jit.cl"
