#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "backend/cpu/Cpu.h"
#include "base/crypto/Algorithm.h"
#include "crypto/cn/CnCtx.h"
#include "crypto/cn/CnHash.h"
#include "crypto/cn/CryptoNight_test.h"
#include "crypto/common/Assembly.h"
#include "crypto/common/VirtualMemory.h"
#include "crypto/flex/flex.h"
#include "crypto/ghostrider/ghostrider.h"
#include "crypto/randomx/intrin_portable.h"
#include "crypto/randomx/randomx.h"
#include "crypto/rx/RxAlgo.h"


namespace {


using namespace xmrig;


using Hash = std::array<uint8_t, 32>;


static std::string hex(const uint8_t *data, size_t size);


class AlignedBuffer
{
public:
    explicit AlignedBuffer(size_t size, size_t align = 64) :
        m_data(static_cast<uint8_t *>(rx_aligned_alloc(size, align)))
    {
        if (m_data == nullptr) {
            throw std::runtime_error("aligned allocation failed");
        }
    }

    ~AlignedBuffer()
    {
        rx_aligned_free(m_data);
    }

    uint8_t *data()
    {
        return m_data;
    }

private:
    AlignedBuffer(const AlignedBuffer &);
    AlignedBuffer &operator=(const AlignedBuffer &);

    uint8_t *m_data;
};


struct TestState
{
    int failed = 0;
    bool verbose = false;

    void expect(const char *name, const uint8_t *actual, const uint8_t *expected, size_t size)
    {
        if (std::memcmp(actual, expected, size) == 0) {
            if (verbose) {
                std::cout << "ok " << name << "\n";
            }
            return;
        }

        ++failed;
        std::cerr << "FAIL " << name << "\n";
        std::cerr << "  actual   " << hex(actual, size) << "\n";
        std::cerr << "  expected " << hex(expected, size) << "\n";
    }
};


static uint8_t *align64(std::vector<uint8_t> &bytes)
{
    const auto p = reinterpret_cast<uintptr_t>(bytes.data());
    return reinterpret_cast<uint8_t *>((p + 63U) & ~uintptr_t(63U));
}


static std::string hex(const uint8_t *data, size_t size)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');

    for (size_t i = 0; i < size; ++i) {
        out << std::setw(2) << static_cast<unsigned>(data[i]);
    }

    return out.str();
}


class CnContext
{
public:
    CnContext(size_t scratchpadSize, size_t count) :
        memory(scratchpadSize * count, false, false, false, 0, VirtualMemory::kDefaultHugePageSize),
        ctx(count, nullptr)
    {
        CnCtx::create(ctx.data(), memory.scratchpad(), scratchpadSize, count);
    }

    ~CnContext()
    {
        CnCtx::release(ctx.data(), ctx.size());
    }

    cryptonight_ctx **data()
    {
        return ctx.data();
    }

private:
    VirtualMemory memory;
    std::vector<cryptonight_ctx *> ctx;
};


static void verifyCn(TestState &state, Algorithm::Id id, const char *name, const uint8_t *expected)
{
    const Algorithm algorithm(id);
    CnContext ctx(algorithm.l3(), 1);
    Hash out{};
    const auto av = Cpu::info()->hasAES() ? CnHash::AV_SINGLE : CnHash::AV_SINGLE_SOFT;
    auto fn = CnHash::fn(algorithm, av, Assembly::NONE);

    if (!fn) {
        ++state.failed;
        std::cerr << "FAIL " << name << " missing hash function\n";
        return;
    }

    fn(test_input, 76, out.data(), ctx.data(), 0);
    state.expect(name, out.data(), expected, out.size());
}


static void verifyCnR(TestState &state)
{
    const Algorithm algorithm(Algorithm::CN_R);
    CnContext ctx(algorithm.l3(), 1);
    Hash out{};
    const auto av = Cpu::info()->hasAES() ? CnHash::AV_SINGLE : CnHash::AV_SINGLE_SOFT;
    auto fn = CnHash::fn(algorithm, av, Assembly::NONE);

    for (size_t i = 0; i < sizeof(cn_r_test_input) / sizeof(cn_r_test_input[0]); ++i) {
        fn(cn_r_test_input[i].data, cn_r_test_input[i].size, out.data(), ctx.data(), cn_r_test_input[i].height);
        state.expect("cn/r", out.data(), test_output_r + i * out.size(), out.size());
    }
}


static void verifyGhostRider(TestState &state)
{
    const Algorithm algorithm(Algorithm::GHOSTRIDER_RTM);
    CnContext ctx(algorithm.l3(), 8);
    uint8_t blob[8 * 80] = {};

    for (size_t i = 0; i < 8; ++i) {
        blob[i * 80 + 0] = static_cast<uint8_t>(i);
        blob[i * 80 + 4] = 0x10;
        blob[i * 80 + 5] = 0x02;
    }

    uint8_t hash1[8 * 32] = {};
    xmrig::ghostrider::hash_octa(blob, 80, hash1, ctx.data(), nullptr, false);

    for (size_t i = 0; i < 8; ++i) {
        blob[i * 80 + 0] = static_cast<uint8_t>(i);
        blob[i * 80 + 4] = 0x43;
        blob[i * 80 + 5] = 0x05;
    }

    uint8_t hash2[8 * 32] = {};
    xmrig::ghostrider::hash_octa(blob, 80, hash2, ctx.data(), nullptr, false);

    for (size_t i = 0; i < sizeof(hash1); ++i) {
        hash1[i] ^= hash2[i];
    }

    state.expect("ghostrider", hash1, test_output_gr, sizeof(hash1));
}


static Hash flexHash()
{
    static const uint8_t header[80] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1c, 0xcc, 0xa6, 0x6a,
        0x44, 0xf8, 0xbd, 0x55, 0x45, 0xc3, 0x16, 0x4a, 0x3a, 0x76,
        0xda, 0x50, 0x39, 0x53, 0x28, 0xc9, 0x07, 0x56, 0x33, 0x77,
        0x5b, 0xc4, 0xc8, 0x79, 0x8f, 0xd6, 0x77, 0x2b, 0x70, 0x0d,
        0x21, 0x5c, 0xf0, 0xff, 0x0f, 0x1e, 0x00, 0x00, 0x00, 0x00
    };

    const Algorithm algorithm(Algorithm::FLEX_KCN);
    CnContext ctx(algorithm.l3(), 1);
    Hash out{};
    flex_hash(reinterpret_cast<const char *>(header), reinterpret_cast<char *>(out.data()), ctx.data());

    return out;
}


class RxRunner
{
public:
    static const std::string seed;

    explicit RxRunner(Algorithm::Id id) :
        algorithm(id)
    {
        RxAlgo::apply(algorithm);

        cacheBytes.resize(RandomX_CurrentConfig.ArgonMemory * 1024 + 64);
        scratchpadBytes.resize(RandomX_CurrentConfig.ScratchpadL3_Size + 64);

        cache = randomx_create_cache(RANDOMX_FLAG_DEFAULT, align64(cacheBytes));
        randomx_init_cache(cache, seed.data(), seed.size());
        vm = randomx_create_vm(RANDOMX_FLAG_DEFAULT, cache, nullptr, align64(scratchpadBytes), 0);

        if (vm == nullptr) {
            throw std::runtime_error("randomx_create_vm failed");
        }
    }

    ~RxRunner()
    {
        randomx_destroy_vm(vm);
        randomx_release_cache(cache);
    }

    Hash hash(const std::string &input)
    {
        Hash out{};
        randomx_calculate_hash(vm, input.data(), input.size(), out.data(), algorithm);

        return out;
    }

    void hashFirst(uint64_t (&temp)[8], const std::string &input)
    {
        randomx_calculate_hash_first(vm, temp, input.data(), input.size(), algorithm);
    }

    Hash hashNext(uint64_t (&temp)[8], const std::string &input)
    {
        Hash out{};
        randomx_calculate_hash_next(vm, temp, input.data(), input.size(), out.data(), algorithm);

        return out;
    }

private:
    Algorithm algorithm;
    std::vector<uint8_t> cacheBytes;
    std::vector<uint8_t> scratchpadBytes;
    randomx_cache *cache = nullptr;
    randomx_vm *vm       = nullptr;
};


const std::string RxRunner::seed = "xmrig-mo-check-hashes seed";


class RxFullDataset
{
public:
    explicit RxFullDataset(Algorithm::Id id) :
        algorithm(id)
    {
        RxAlgo::apply(algorithm);

        cacheMemory.reset(new AlignedBuffer(RandomX_CurrentConfig.ArgonMemory * 1024));
        cache = randomx_create_cache(RANDOMX_FLAG_JIT, cacheMemory->data());
        if (cache == nullptr) {
            cache = randomx_create_cache(RANDOMX_FLAG_DEFAULT, cacheMemory->data());
        }

        if (cache == nullptr) {
            throw std::runtime_error("randomx_create_cache failed");
        }

        randomx_init_cache(cache, RxRunner::seed.data(), RxRunner::seed.size());

        const size_t datasetSize = randomx_dataset_item_count() * RANDOMX_DATASET_ITEM_SIZE;
        datasetMemory.reset(new AlignedBuffer(datasetSize));
        dataset = randomx_create_dataset(datasetMemory->data());

        if (dataset == nullptr) {
            throw std::runtime_error("randomx_create_dataset failed");
        }

        randomx_init_dataset(dataset, cache, 0, randomx_dataset_item_count());
    }

    ~RxFullDataset()
    {
        if (dataset != nullptr) {
            randomx_release_dataset(dataset);
        }

        if (cache != nullptr) {
            randomx_release_cache(cache);
        }
    }

    Hash hash(randomx_flags flags, const std::string &input)
    {
        AlignedBuffer scratchpad(RandomX_CurrentConfig.ScratchpadL3_Size);
        randomx_vm *vm = randomx_create_vm(flags, nullptr, dataset, scratchpad.data(), 0);

        if (vm == nullptr) {
            throw std::runtime_error("randomx_create_vm failed");
        }

        Hash out{};
        randomx_calculate_hash(vm, input.data(), input.size(), out.data(), algorithm);
        randomx_destroy_vm(vm);

        return out;
    }

    void hashFirstNext(randomx_flags flags, const std::string &inputA, const std::string &inputB, Hash &outA, Hash &outB)
    {
        AlignedBuffer scratchpad(RandomX_CurrentConfig.ScratchpadL3_Size);
        randomx_vm *vm = randomx_create_vm(flags, nullptr, dataset, scratchpad.data(), 0);

        if (vm == nullptr) {
            throw std::runtime_error("randomx_create_vm failed");
        }

        uint64_t temp[8] = {};
        randomx_calculate_hash_first(vm, temp, inputA.data(), inputA.size(), algorithm);
        randomx_calculate_hash_next(vm, temp, inputB.data(), inputB.size(), outA.data(), algorithm);
        randomx_calculate_hash_next(vm, temp, inputA.data(), inputA.size(), outB.data(), algorithm);
        randomx_destroy_vm(vm);
    }

private:
    RxFullDataset(const RxFullDataset &);
    RxFullDataset &operator=(const RxFullDataset &);

    Algorithm algorithm;
    std::unique_ptr<AlignedBuffer> cacheMemory;
    std::unique_ptr<AlignedBuffer> datasetMemory;
    randomx_cache *cache     = nullptr;
    randomx_dataset *dataset = nullptr;
};


struct RxVector
{
    Algorithm::Id id;
    const char *name;
    Hash expected;
};


static const RxVector rxVectors[] = {
    { Algorithm::RX_0,     "rx/0",     {{ 0x18, 0x33, 0xff, 0x32, 0x2e, 0x60, 0xda, 0xd7, 0xd6, 0x88, 0x5b, 0xcd, 0x5e, 0x08, 0x44, 0xec,
                                           0x8e, 0x48, 0x08, 0xa4, 0x7d, 0x8d, 0x35, 0x48, 0x06, 0x66, 0x8f, 0x0c, 0x24, 0x1a, 0x6a, 0xf5 }} },
    { Algorithm::RX_V2,    "rx/2",     {{ 0x7a, 0x42, 0x79, 0x43, 0x5b, 0x2d, 0x80, 0x0e, 0x7e, 0x63, 0x8f, 0x1a, 0xa2, 0x10, 0x2d, 0x8a,
                                           0x3a, 0x01, 0x84, 0x3a, 0xb8, 0xd6, 0x6c, 0x26, 0xe9, 0x39, 0x63, 0x26, 0x6e, 0x42, 0x47, 0x68 }} },
    { Algorithm::RX_WOW,   "rx/wow",   {{ 0x45, 0x89, 0x62, 0x10, 0x53, 0x9d, 0x9b, 0xa3, 0x73, 0x77, 0xb9, 0xc1, 0x86, 0xb7, 0x34, 0x33,
                                           0x04, 0xcd, 0x6b, 0xf7, 0x97, 0xc7, 0x5f, 0x3d, 0x10, 0x87, 0xee, 0x63, 0xf2, 0x2a, 0x73, 0xe4 }} },
    { Algorithm::RX_ARQ,   "rx/arq",   {{ 0xca, 0x2b, 0x7a, 0xfe, 0x5c, 0x50, 0xab, 0x45, 0x29, 0x6c, 0x54, 0x57, 0x33, 0x7f, 0xf7, 0xcf,
                                           0x27, 0x62, 0x41, 0x39, 0xf0, 0x5e, 0xc5, 0xd4, 0x4c, 0x1e, 0x4a, 0xe8, 0xbf, 0xe9, 0x5b, 0xb6 }} },
    { Algorithm::RX_GRAFT, "rx/graft", {{ 0xc4, 0xa9, 0xcf, 0xd2, 0xf0, 0xa9, 0x35, 0x9c, 0x78, 0xf0, 0x89, 0x90, 0x2b, 0x44, 0xb4, 0x26,
                                           0x23, 0xfb, 0xf1, 0x8a, 0x1a, 0x01, 0x78, 0x68, 0x79, 0xbf, 0xed, 0x3f, 0x0b, 0xe2, 0x55, 0xf7 }} },
    { Algorithm::RX_SFX,   "rx/sfx",   {{ 0x3b, 0x88, 0x93, 0x6e, 0x28, 0x9f, 0xf3, 0x24, 0x0d, 0xc0, 0x74, 0xb2, 0x89, 0xfe, 0x64, 0x64,
                                           0x19, 0x4b, 0xdf, 0xe3, 0x1a, 0x80, 0xc9, 0xdc, 0x10, 0xc9, 0xc1, 0xea, 0xa1, 0x97, 0x55, 0x13 }} },
    { Algorithm::RX_YADA,  "rx/yada",  {{ 0x04, 0xd6, 0x8f, 0xb9, 0xc4, 0x45, 0xf4, 0x43, 0x7d, 0xfc, 0xe6, 0xeb, 0xb8, 0x02, 0x83, 0x67,
                                           0xb0, 0x60, 0x14, 0x00, 0x66, 0xeb, 0xaa, 0x41, 0x6a, 0x4c, 0xd2, 0xca, 0x63, 0xcd, 0xb7, 0x23 }} },
    { Algorithm::RX_XLA,   "panthera", {{ 0x18, 0x05, 0xaa, 0x4f, 0xd2, 0x6f, 0x56, 0x86, 0xda, 0xda, 0x26, 0xd9, 0xe1, 0x6c, 0xcb, 0x29,
                                           0x08, 0x11, 0xb2, 0xa7, 0x94, 0x04, 0xfd, 0xd1, 0x06, 0x4f, 0x1c, 0xa0, 0x92, 0x90, 0x22, 0x12 }} },
};


static void dumpVectors()
{
    const auto flex = flexHash();
    std::cout << "flex " << hex(flex.data(), flex.size()) << "\n";

    for (const auto &item : rxVectors) {
        RxRunner rx(item.id);
        const auto out = rx.hash("xmrig-mo-check-hashes input A");
        std::cout << item.name << " " << hex(out.data(), out.size()) << "\n";
    }
}


static void verifyRandomX(TestState &state, const RxVector &item)
{
    static const std::string inputA = "xmrig-mo-check-hashes input A";
    static const std::string inputB = "xmrig-mo-check-hashes input B";
    static const std::string inputC = "xmrig-mo-check-hashes input C";

    RxRunner rx(item.id);
    const auto directA = rx.hash(inputA);
    const auto directB = rx.hash(inputB);

    state.expect(item.name, directA.data(), item.expected.data(), directA.size());

    uint64_t temp[8] = {};
    rx.hashFirst(temp, inputA);
    const auto nextA = rx.hashNext(temp, inputB);
    const auto nextB = rx.hashNext(temp, inputC);

    state.expect((std::string(item.name) + " first/next A").c_str(), nextA.data(), directA.data(), nextA.size());
    state.expect((std::string(item.name) + " first/next B").c_str(), nextB.data(), directB.data(), nextB.size());
}


static void verifyRandomXFull(TestState &state, const RxVector &item)
{
    static const std::string inputA = "xmrig-mo-check-hashes input A";
    static const std::string inputB = "xmrig-mo-check-hashes input B";

    try {
        RxFullDataset full(item.id);
        const auto fullA = full.hash(RANDOMX_FLAG_FULL_MEM, inputA);
        const auto fullB = full.hash(RANDOMX_FLAG_FULL_MEM, inputB);

        Hash nextA{};
        Hash nextB{};
        full.hashFirstNext(RANDOMX_FLAG_FULL_MEM, inputA, inputB, nextA, nextB);
        state.expect((std::string(item.name) + " full first/next A").c_str(), nextA.data(), fullA.data(), nextA.size());
        state.expect((std::string(item.name) + " full first/next B").c_str(), nextB.data(), fullB.data(), nextB.size());

        // rx/2 VM JIT is left as upstream v6.26 behavior, which does not match the interpreter.
        if (item.id != Algorithm::RX_V2) {
            const auto fullJitA = full.hash(static_cast<randomx_flags>(RANDOMX_FLAG_FULL_MEM | RANDOMX_FLAG_JIT), inputA);
            state.expect((std::string(item.name) + " full jit").c_str(), fullJitA.data(), fullA.data(), fullJitA.size());
        }

        if (Cpu::info()->hasAES()) {
            const auto fullHardAesA = full.hash(static_cast<randomx_flags>(RANDOMX_FLAG_FULL_MEM | RANDOMX_FLAG_HARD_AES), inputA);
            state.expect((std::string(item.name) + " full hard-aes").c_str(), fullHardAesA.data(), fullA.data(), fullHardAesA.size());

            if (item.id != Algorithm::RX_V2) {
                const auto fullJitHardAesA = full.hash(static_cast<randomx_flags>(RANDOMX_FLAG_FULL_MEM | RANDOMX_FLAG_JIT | RANDOMX_FLAG_HARD_AES), inputA);
                state.expect((std::string(item.name) + " full jit hard-aes").c_str(), fullJitHardAesA.data(), fullA.data(), fullJitHardAesA.size());
            }
        }
    }
    catch (const std::exception &ex) {
        ++state.failed;
        std::cerr << "FAIL " << item.name << " full setup: " << ex.what() << "\n";
    }
}


struct TestCase
{
    std::string name;
    std::function<void(TestState &)> run;
};


static const std::vector<TestCase> &smallTests()
{
    static const std::vector<TestCase> tests = {
        { "cn/0",              [](TestState &state) { verifyCn(state, Algorithm::CN_0, "cn/0", test_output_v0); } },
        { "cn/1",              [](TestState &state) { verifyCn(state, Algorithm::CN_1, "cn/1", test_output_v1); } },
        { "cn/2",              [](TestState &state) { verifyCn(state, Algorithm::CN_2, "cn/2", test_output_v2); } },
        { "cn/fast",           [](TestState &state) { verifyCn(state, Algorithm::CN_FAST, "cn/fast", test_output_msr); } },
        { "cn/xao",            [](TestState &state) { verifyCn(state, Algorithm::CN_XAO, "cn/xao", test_output_xao); } },
        { "cn/rto",            [](TestState &state) { verifyCn(state, Algorithm::CN_RTO, "cn/rto", test_output_rto); } },
        { "cn/half",           [](TestState &state) { verifyCn(state, Algorithm::CN_HALF, "cn/half", test_output_half); } },
        { "cn/r",              [](TestState &state) { verifyCnR(state); } },
        { "cn/rwz",            [](TestState &state) { verifyCn(state, Algorithm::CN_RWZ, "cn/rwz", test_output_rwz); } },
        { "cn/zls",            [](TestState &state) { verifyCn(state, Algorithm::CN_ZLS, "cn/zls", test_output_zls); } },
        { "cn/ccx",            [](TestState &state) { verifyCn(state, Algorithm::CN_CCX, "cn/ccx", test_output_ccx); } },
        { "cn/double",         [](TestState &state) { verifyCn(state, Algorithm::CN_DOUBLE, "cn/double", test_output_double); } },
        { "cn/gpu",            [](TestState &state) { verifyCn(state, Algorithm::CN_GPU, "cn/gpu", test_output_gpu); } },
        { "cn-lite/0",         [](TestState &state) { verifyCn(state, Algorithm::CN_LITE_0, "cn-lite/0", test_output_v0_lite); } },
        { "cn-lite/1",         [](TestState &state) { verifyCn(state, Algorithm::CN_LITE_1, "cn-lite/1", test_output_v1_lite); } },
        { "cn-heavy/0",        [](TestState &state) { verifyCn(state, Algorithm::CN_HEAVY_0, "cn-heavy/0", test_output_v0_heavy); } },
        { "cn-heavy/xhv",      [](TestState &state) { verifyCn(state, Algorithm::CN_HEAVY_XHV, "cn-heavy/xhv", test_output_xhv_heavy); } },
        { "cn-heavy/tube",     [](TestState &state) { verifyCn(state, Algorithm::CN_HEAVY_TUBE, "cn-heavy/tube", test_output_tube_heavy); } },
        { "cn-pico",           [](TestState &state) { verifyCn(state, Algorithm::CN_PICO_0, "cn-pico", test_output_pico_trtl); } },
        { "cn-pico/tlo",       [](TestState &state) { verifyCn(state, Algorithm::CN_PICO_TLO, "cn-pico/tlo", test_output_pico_tlo); } },
        { "cn/upx2",           [](TestState &state) { verifyCn(state, Algorithm::CN_UPX2, "cn/upx2", test_output_femto_upx2); } },
        { "argon2/chukwa",     [](TestState &state) { verifyCn(state, Algorithm::AR2_CHUKWA, "argon2/chukwa", argon2_chukwa_test_out); } },
        { "argon2/chukwav2",   [](TestState &state) { verifyCn(state, Algorithm::AR2_CHUKWA_V2, "argon2/chukwav2", argon2_chukwa_v2_test_out); } },
        { "argon2/ninja",      [](TestState &state) { verifyCn(state, Algorithm::AR2_WRKZ, "argon2/wrkz", argon2_wrkz_test_out); } },
        { "ghostrider",        [](TestState &state) { verifyGhostRider(state); } },
        { "flex",              [](TestState &state) {
                                      const auto flex = flexHash();
                                      state.expect("flex", flex.data(), test_output_flex, flex.size());
                                  } },
        { "rx/0",              [](TestState &state) { verifyRandomX(state, rxVectors[0]); } },
        { "rx/2",              [](TestState &state) { verifyRandomX(state, rxVectors[1]); } },
        { "rx/wow",            [](TestState &state) { verifyRandomX(state, rxVectors[2]); } },
        { "rx/arq",            [](TestState &state) { verifyRandomX(state, rxVectors[3]); } },
        { "rx/graft",          [](TestState &state) { verifyRandomX(state, rxVectors[4]); } },
        { "rx/sfx",            [](TestState &state) { verifyRandomX(state, rxVectors[5]); } },
        { "rx/yada",           [](TestState &state) { verifyRandomX(state, rxVectors[6]); } },
        { "panthera",          [](TestState &state) { verifyRandomX(state, rxVectors[7]); } },
    };

    return tests;
}


static const std::vector<TestCase> &fullTests()
{
    static const std::vector<TestCase> tests = {
        { "rx/0",      [](TestState &state) { verifyRandomXFull(state, rxVectors[0]); } },
        { "rx/2",      [](TestState &state) { verifyRandomXFull(state, rxVectors[1]); } },
        { "rx/wow",    [](TestState &state) { verifyRandomXFull(state, rxVectors[2]); } },
        { "rx/arq",    [](TestState &state) { verifyRandomXFull(state, rxVectors[3]); } },
        { "rx/graft",  [](TestState &state) { verifyRandomXFull(state, rxVectors[4]); } },
        { "rx/sfx",    [](TestState &state) { verifyRandomXFull(state, rxVectors[5]); } },
        { "rx/yada",   [](TestState &state) { verifyRandomXFull(state, rxVectors[6]); } },
        { "panthera",  [](TestState &state) { verifyRandomXFull(state, rxVectors[7]); } },
    };

    return tests;
}


static const std::vector<TestCase> &suiteTests(const std::string &suite)
{
    if (suite == "small" || suite == "hash-small") {
        return smallTests();
    }

    if (suite == "full" || suite == "hash-full") {
        return fullTests();
    }

    throw std::runtime_error("unknown suite: " + suite);
}


static int listTests(const std::vector<TestCase> &tests)
{
    for (const auto &test : tests) {
        std::cout << test.name << "\n";
    }

    return 0;
}


static int runTests(const std::vector<TestCase> &tests, const std::string &caseName, bool verbose)
{
    int failed = 0;
    bool found = caseName.empty();

    for (const auto &test : tests) {
        if (!caseName.empty() && caseName != test.name) {
            continue;
        }

        found = true;
        TestState state;
        state.verbose = verbose;

        try {
            test.run(state);
        }
        catch (const std::exception &ex) {
            ++state.failed;
            std::cerr << "FAIL " << test.name << ": " << ex.what() << "\n";
        }

        if (state.failed > 0) {
            failed += state.failed;
        }
        else if (verbose) {
            std::cout << "ok " << test.name << "\n";
        }
    }

    if (!found) {
        std::cerr << "unknown test case: " << caseName << "\n";
        return 1;
    }

    return failed == 0 ? 0 : 1;
}


static void printHelp()
{
    std::cout << "Usage: hash-tests [options]\n\n"
              << "Options:\n"
              << "  --suite NAME       test suite to run: small or full (default: small)\n"
              << "  --case NAME        run one named test case from the selected suite\n"
              << "  --list             list test cases from the selected suite\n"
              << "  --full-randomx     compatibility alias for --suite full\n"
              << "  --dump-vectors     print generated vector hashes\n"
              << "  --verbose          print successful sub-checks\n"
              << "  --help             show this help\n";
}


} // namespace


int main(int argc, char **argv)
{
    std::string suite = "small";
    std::string caseName;
    bool list = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);

        if (arg == "--dump-vectors") {
            dumpVectors();
            return 0;
        }

        if (arg == "--full-randomx") {
            suite = "full";
        }
        else if (arg == "--suite") {
            if (++i >= argc) {
                std::cerr << "missing value for --suite\n";
                return 1;
            }

            suite = argv[i];
        }
        else if (arg.find("--suite=") == 0) {
            suite = arg.substr(8);
        }
        else if (arg == "--case") {
            if (++i >= argc) {
                std::cerr << "missing value for --case\n";
                return 1;
            }

            caseName = argv[i];
        }
        else if (arg.find("--case=") == 0) {
            caseName = arg.substr(7);
        }
        else if (arg == "--list") {
            list = true;
        }
        else if (arg == "--verbose") {
            verbose = true;
        }
        else if (arg == "--help" || arg == "-h") {
            printHelp();
            return 0;
        }
        else {
            std::cerr << "unknown argument: " << arg << "\n";
            return 1;
        }
    }

    try {
        const auto &tests = suiteTests(suite);
        return list ? listTests(tests) : runTests(tests, caseName, verbose);
    }
    catch (const std::exception &ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
