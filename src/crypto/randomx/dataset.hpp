/*
Copyright (c) 2018-2019, tevador <tevador@gmail.com>

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
	* Redistributions of source code must retain the above copyright
	  notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright
	  notice, this list of conditions and the following disclaimer in the
	  documentation and/or other materials provided with the distribution.
	* Neither the name of the copyright holder nor the
	  names of its contributors may be used to endorse or promote products
	  derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <cstdint>
#include <vector>
#include <type_traits>
#include "crypto/randomx/common.hpp"
#include "crypto/randomx/superscalar_program.hpp"
#include "crypto/randomx/allocator.hpp"
#include "crypto/randomx/argon2.h"

/* Global scope for C binding */
struct randomx_dataset {
	uint8_t* memory = nullptr;
	randomx::DatasetDeallocFunc* dealloc;
};

/* Global scope for C binding */
struct randomx_cache {
	uint8_t* memory = nullptr;
	randomx::CacheDeallocFunc* dealloc;
	randomx::JitCompiler* jit;
	randomx::CacheInitializeFunc* initialize;
	randomx::DatasetInitFunc* datasetInit;
	randomx::SuperscalarProgram programs[RANDOMX_CACHE_MAX_ACCESSES];
	std::vector<uint64_t> reciprocalCache;
	randomx_argon2_impl* argonImpl;

	bool isInitialized() {
		return programs[0].getSize() != 0;
	}
};

//A pointer to a standard-layout struct object points to its initial member
static_assert(std::is_standard_layout<randomx_dataset>(), "randomx_dataset must be a standard-layout struct");
static_assert(std::is_standard_layout<randomx_cache>(), "randomx_cache must be a standard-layout struct");

namespace randomx {

	using DefaultAllocator = AlignedAllocator<CacheLineSize>;

	template<class Allocator>
	void deallocDataset(randomx_dataset* dataset) {
		if (dataset->memory != nullptr)
			Allocator::freeMemory(dataset->memory, RANDOMX_DATASET_MAX_SIZE);
	}

	template<class Allocator>
	void deallocCache(randomx_cache* cache);

	void initCache(randomx_cache*, const void*, size_t);
	void initCacheCompile(randomx_cache*, const void*, size_t);
	void initDatasetItem(randomx_cache* cache, uint8_t* out, uint64_t blockNumber);
	void initDataset(randomx_cache* cache, uint8_t* dataset, uint32_t startBlock, uint32_t endBlock);
	
	inline randomx_argon2_impl* selectArgonImpl(randomx_flags flags) {
		if ((flags & RANDOMX_FLAG_ARGON2) == 0) {
			return &randomx_argon2_fill_segment_ref;
		}
		randomx_argon2_impl* impl = nullptr;
		if ((flags & RANDOMX_FLAG_ARGON2) == RANDOMX_FLAG_ARGON2_SSSE3) {
			impl = randomx_argon2_impl_ssse3();
		}
		if ((flags & RANDOMX_FLAG_ARGON2) == RANDOMX_FLAG_ARGON2_AVX2) {
			impl = randomx_argon2_impl_avx2();
		}
		if (impl != nullptr) {
			return impl;
		}
		throw std::runtime_error("Unsupported Argon2 implementation");
	}
}
