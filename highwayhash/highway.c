#include "highway.h"

#include <stdint.h>
#include <string.h>

/*
This code is compatible with C90 with the additional requirement of
supporting uint64_t.
*/

/*////////////////////////////////////////////////////////////////////////////*/
/* Internal implementation                                                    */
/*////////////////////////////////////////////////////////////////////////////*/

void HighwayHashReset(const uint64_t key[4], HighwayHashState* state) {
	state->mul0[0] = 0xdbe6d5d5fe4cce2full;
	state->mul0[1] = 0xa4093822299f31d0ull;
	state->mul0[2] = 0x13198a2e03707344ull;
	state->mul0[3] = 0x243f6a8885a308d3ull;
	state->mul1[0] = 0x3bd39e10cb0ef593ull;
	state->mul1[1] = 0xc0acf169b5f18a8cull;
	state->mul1[2] = 0xbe5466cf34e90c6cull;
	state->mul1[3] = 0x452821e638d01377ull;
	state->v0[0] = state->mul0[0] ^ key[0];
	state->v0[1] = state->mul0[1] ^ key[1];
	state->v0[2] = state->mul0[2] ^ key[2];
	state->v0[3] = state->mul0[3] ^ key[3];
	state->v1[0] = state->mul1[0] ^ ((key[0] >> 32u) | (key[0] << 32u));
	state->v1[1] = state->mul1[1] ^ ((key[1] >> 32u) | (key[1] << 32u));
	state->v1[2] = state->mul1[2] ^ ((key[2] >> 32u) | (key[2] << 32u));
	state->v1[3] = state->mul1[3] ^ ((key[3] >> 32u) | (key[3] << 32u));
}

static void ZipperMergeAndAdd(const uint64_t v1, const uint64_t v0,
							  uint64_t* add1, uint64_t* add0) {
	*add0 += (((v0 & 0xff000000ull) | (v1 & 0xff00000000ull)) >> 24u) |
			 (((v0 & 0xff0000000000ull) | (v1 & 0xff000000000000ull)) >> 16u) |
			 (v0 & 0xff0000ull) | ((v0 & 0xff00ull) << 32u) |
			 ((v1 & 0xff00000000000000ull) >> 8u) | (v0 << 56u);
	*add1 += (((v1 & 0xff000000ull) | (v0 & 0xff00000000ull)) >> 24u) |
			 (v1 & 0xff0000ull) | ((v1 & 0xff0000000000ull) >> 16u) |
			 ((v1 & 0xff00ull) << 24u) | ((v0 & 0xff000000000000ull) >> 8u) |
			 ((v1 & 0xffull) << 48u) | (v0 & 0xff00000000000000ull);
}

static void Update(const uint64_t lanes[4], HighwayHashState* state) {
	int i;
	for (i = 0; i < 4; ++i) {
		state->v1[i] += state->mul0[i] + lanes[i];
		state->mul0[i] ^= (state->v1[i] & 0xffffffff) * (state->v0[i] >> 32u);
		state->v0[i] += state->mul1[i];
		state->mul1[i] ^= (state->v0[i] & 0xffffffff) * (state->v1[i] >> 32u);
	}
	ZipperMergeAndAdd(state->v1[1], state->v1[0], &state->v0[1], &state->v0[0]);
	ZipperMergeAndAdd(state->v1[3], state->v1[2], &state->v0[3], &state->v0[2]);
	ZipperMergeAndAdd(state->v0[1], state->v0[0], &state->v1[1], &state->v1[0]);
	ZipperMergeAndAdd(state->v0[3], state->v0[2], &state->v1[3], &state->v1[2]);
}

static uint64_t Read64(const uint8_t* src) {
	return (uint64_t)src[0] | ((uint64_t)src[1] << 8u) |
		   ((uint64_t)src[2] << 16u) | ((uint64_t)src[3] << 24u) |
		   ((uint64_t)src[4] << 32u) | ((uint64_t)src[5] << 40u) |
		   ((uint64_t)src[6] << 48u) | ((uint64_t)src[7] << 56u);
}

void HighwayHashUpdatePacket(const uint8_t* packet, HighwayHashState* state) {
	uint64_t lanes[4];
	lanes[0] = Read64(packet + 0);
	lanes[1] = Read64(packet + 8);
	lanes[2] = Read64(packet + 16);
	lanes[3] = Read64(packet + 24);
	Update(lanes, state);
}

static void Rotate32By(uint64_t count, uint64_t lanes[4]) {
	int i;
	for (i = 0; i < 4; ++i) {
		uint32_t half0 = lanes[i] & 0xffffffff;
		uint32_t half1 = (lanes[i] >> 32u);
		lanes[i] = (half0 << count) | (half0 >> (32 - count));
		lanes[i] |= (uint64_t)((half1 << count) | (half1 >> (32 - count))) << 32u;
	}
}

void HighwayHashUpdateRemainder(const uint8_t* bytes, const size_t size_mod32,
								HighwayHashState* state) {
	int i;
	const size_t size_mod4 = size_mod32 & 3u;
	const uint8_t* remainder = bytes + (size_mod32 & ~3u);
	uint8_t packet[32] = {0};
	for (i = 0; i < 4; ++i) {
		state->v0[i] += ((uint64_t)size_mod32 << 32u) + size_mod32;
	}
	Rotate32By(size_mod32, state->v1);
	for (i = 0; i < remainder - bytes; i++) {
		packet[i] = bytes[i];
	}
	if (size_mod32 & 16u) {
		for (i = 0; i < 4; i++) {
			packet[28 + i] = remainder[i + size_mod4 - 4];
		}
	} else {
		if (size_mod4) {
			packet[16 + 0] = remainder[0];
			packet[16 + 1] = remainder[size_mod4 >> 1u];
			packet[16 + 2] = remainder[size_mod4 - 1];
		}
	}
	HighwayHashUpdatePacket(packet, state);
}

static void Permute(const uint64_t v[4], uint64_t* permuted) {
	permuted[0] = (v[2] >> 32u) | (v[2] << 32u);
	permuted[1] = (v[3] >> 32u) | (v[3] << 32u);
	permuted[2] = (v[0] >> 32u) | (v[0] << 32u);
	permuted[3] = (v[1] >> 32u) | (v[1] << 32u);
}

void PermuteAndUpdate(HighwayHashState* state) {
	uint64_t permuted[4];
	Permute(state->v0, permuted);
	Update(permuted, state);
}

static void ModularReduction(uint64_t a3_unmasked, uint64_t a2, uint64_t a1,
							 uint64_t a0, uint64_t* m1, uint64_t* m0) {
	uint64_t a3 = a3_unmasked & 0x3FFFFFFFFFFFFFFFull;
	*m1 = a1 ^ ((a3 << 1u) | (a2 >> 63u)) ^ ((a3 << 2u) | (a2 >> 62u));
	*m0 = a0 ^ (a2 << 1u) ^ (a2 << 2u);
}

uint64_t HighwayHashFinalize64(HighwayHashState* state) {
	int i;
	for (i = 0; i < 4; i++) {
		PermuteAndUpdate(state);
	}
	return state->v0[0] + state->v1[0] + state->mul0[0] + state->mul1[0];
}

void HighwayHashFinalize128(HighwayHashState* state, uint64_t hash[2]) {
	int i;
	for (i = 0; i < 6; i++) {
		PermuteAndUpdate(state);
	}
	hash[0] = state->v0[0] + state->mul0[0] + state->v1[2] + state->mul1[2];
	hash[1] = state->v0[1] + state->mul0[1] + state->v1[3] + state->mul1[3];
}

void HighwayHashFinalize256(HighwayHashState* state, uint64_t hash[4]) {
	int i;
	/* We anticipate that 256-bit hashing will be mostly used with long messages
	   because storing and using the 256-bit hash (in contrast to 128-bit)
	   carries a larger additional constant cost by itself. Doing extra rounds
	   here hardly increases the per-byte cost of long messages. */
	for (i = 0; i < 10; i++) {
		PermuteAndUpdate(state);
	}
	ModularReduction(state->v1[1] + state->mul1[1], state->v1[0] + state->mul1[0],
					 state->v0[1] + state->mul0[1], state->v0[0] + state->mul0[0],
					 &hash[1], &hash[0]);
	ModularReduction(state->v1[3] + state->mul1[3], state->v1[2] + state->mul1[2],
					 state->v0[3] + state->mul0[3], state->v0[2] + state->mul0[2],
					 &hash[3], &hash[2]);
}

/*////////////////////////////////////////////////////////////////////////////*/
/* Non-cat API: single call on full data                                      */
/*////////////////////////////////////////////////////////////////////////////*/

static void ProcessAll(const uint8_t* data, size_t size, const uint64_t key[4],
					   HighwayHashState* state) {
	size_t i;
	HighwayHashReset(key, state);
	for (i = 0; i + 32 <= size; i += 32) {
		HighwayHashUpdatePacket(data + i, state);
	}
	if ((size & 31u) != 0) HighwayHashUpdateRemainder(data + i, size & 31u, state);
}

uint64_t HighwayHash64(const uint8_t* data, size_t size,
					   const uint64_t key[4]) {
	HighwayHashState state;
	ProcessAll(data, size, key, &state);
	return HighwayHashFinalize64(&state);
}

void HighwayHash128(const uint8_t* data, size_t size,
					const uint64_t key[4], uint64_t hash[2]) {
	HighwayHashState state;
	ProcessAll(data, size, key, &state);
	HighwayHashFinalize128(&state, hash);
}

void HighwayHash256(const uint8_t* data, size_t size,
					const uint64_t key[4], uint64_t hash[4]) {
	HighwayHashState state;
	ProcessAll(data, size, key, &state);
	HighwayHashFinalize256(&state, hash);
}

/*////////////////////////////////////////////////////////////////////////////*/
/* Cat API: allows appending with multiple calls                              */
/*////////////////////////////////////////////////////////////////////////////*/

void HighwayHashCatStart(const uint64_t key[4], HighwayHashCat* state) {
	HighwayHashReset(key, &state->state);
	state->num = 0;
}

void HighwayHashCatAppend(const uint8_t* bytes, size_t num,
						  HighwayHashCat* state) {
	size_t i;
	if (state->num != 0) {
		size_t num_add = num > (32u - state->num) ? (32u - state->num) : num;
		for (i = 0; i < num_add; i++) {
			state->packet[state->num + i] = bytes[i];
		}
		state->num += num_add;
		num -= num_add;
		bytes += num_add;
		if (state->num == 32) {
			HighwayHashUpdatePacket(state->packet, &state->state);
			state->num = 0;
		}
	}
	while (num >= 32) {
		HighwayHashUpdatePacket(bytes, &state->state);
		num -= 32;
		bytes += 32;
	}
	for (i = 0; i < num; i++) {
		state->packet[state->num] = bytes[i];
		state->num++;
	}
}

uint64_t HighwayHashCatFinish64(const HighwayHashCat* state) {
	HighwayHashState copy = state->state;
	if (state->num) {
		HighwayHashUpdateRemainder(state->packet, state->num, &copy);
	}
	return HighwayHashFinalize64(&copy);
}

void HighwayHashCatFinish128(const HighwayHashCat* state, uint64_t hash[2]) {
	HighwayHashState copy = state->state;
	if (state->num) {
		HighwayHashUpdateRemainder(state->packet, state->num, &copy);
	}
	HighwayHashFinalize128(&copy, hash);
}

void HighwayHashCatFinish256(const HighwayHashCat* state, uint64_t hash[4]) {
	HighwayHashState copy = state->state;
	if (state->num) {
		HighwayHashUpdateRemainder(state->packet, state->num, &copy);
	}
	HighwayHashFinalize256(&copy, hash);
}