/*
 * Copyright (c) 2025 Michael Bishop
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

#ifndef _CHACHA20_H
#define _CHACHA20_H
// from https://en.wikipedia.org/wiki/Salsa20#ChaCha_variant
#define ROTL(a,b) (((a) << (b)) | ((a) >> (32 - (b))))
#define QR(a, b, c, d) (             \
	a += b, d ^= a, d = ROTL(d, 16), \
	c += d, b ^= c, b = ROTL(b, 12), \
	a += b, d ^= a, d = ROTL(d,  8), \
	c += d, b ^= c, b = ROTL(b,  7))
#define ROUNDS 20

void chacha_block(uint32_t out[16], uint32_t const in[16]) {
	int i;
	uint32_t x[16];

	for (i = 0; i < 16; ++i)
		x[i] = in[i];
	// 10 loops × 2 rounds/loop = 20 rounds
	for (i = 0; i < ROUNDS; i += 2) {
		// Odd round
		QR(x[0], x[4], x[ 8], x[12]); // column 1
		QR(x[1], x[5], x[ 9], x[13]); // column 2
		QR(x[2], x[6], x[10], x[14]); // column 3
		QR(x[3], x[7], x[11], x[15]); // column 4
		// Even round
		QR(x[0], x[5], x[10], x[15]); // diagonal 1 (main diagonal)
		QR(x[1], x[6], x[11], x[12]); // diagonal 2
		QR(x[2], x[7], x[ 8], x[13]); // diagonal 3
		QR(x[3], x[4], x[ 9], x[14]); // diagonal 4
	}
	for (i = 0; i < 16; ++i)
		out[i] = x[i] + in[i];
}

void chacha_init(uint32_t state[16], const uint32_t key[8], const uint32_t counter, const uint32_t nonce[3]) {
  state[0] = htonl('expa');
  state[1] = htonl('nd 3');
  state[2] = htonl('2-by');
  state[3] = htonl('te k');

  for (int i=0; i<8; i++) {
    state[4+i] = key[i];
  }

  state[12] = counter;
  state[13] = nonce[0];
  state[14] = nonce[1];
  state[15] = nonce[2];
}
#endif
