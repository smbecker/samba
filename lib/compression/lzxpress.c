/*
 * Copyright (C) Matthieu Suiche 2008
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "replace.h"
#include "lzxpress.h"
#include "../lib/util/byteorder.h"


#define __CHECK_BYTES(__size, __index, __needed) do { \
	if (unlikely(__index >= __size)) { \
		return -1; \
	} else { \
		uint32_t __avail = __size - __index; \
		if (unlikely(__needed > __avail)) { \
			return -1; \
		} \
	} \
} while(0)

#define CHECK_INPUT_BYTES(__needed) \
	__CHECK_BYTES(uncompressed_size, uncompressed_pos, __needed)
#define CHECK_OUTPUT_BYTES(__needed) \
	__CHECK_BYTES(max_compressed_size, compressed_pos, __needed)

ssize_t lzxpress_compress(const uint8_t *uncompressed,
			  uint32_t uncompressed_size,
			  uint8_t *compressed,
			  uint32_t max_compressed_size)
{
	/*
	 * This is the algorithm in [MS-XCA] 2.3 "Plain LZ77 Compression".
	 *
	 * It avoids Huffman encoding by including literal bytes inline when a
	 * match is not found. Every so often it includes a uint32 bit map
	 * flagging which positions contain matches and which contain
	 * literals. The encoding of matches is of variable size, depending on
	 * the match length; they are always at least 16 bits long, and can
	 * implicitly use unused half-bytes from earlier in the stream.
	 */
	uint32_t uncompressed_pos, compressed_pos;
	uint32_t indic;
	uint32_t indic_pos;
	uint32_t indic_bit, nibble_index;

	if (!uncompressed_size) {
		return 0;
	}

	uncompressed_pos = 0;
	compressed_pos = 0;
	indic = 0;
	CHECK_OUTPUT_BYTES(sizeof(uint32_t));
	PUSH_LE_U32(compressed, compressed_pos, 0);
	compressed_pos += sizeof(uint32_t);
	indic_pos = 0;

	indic_bit = 0;
	nibble_index = 0;

	while ((uncompressed_pos < uncompressed_size) &&
	       (compressed_pos < max_compressed_size)) {
		bool found = false;

		uint32_t best_len = 2;
		uint32_t best_offset = 0;

		int32_t offset;

		const uint32_t max_offset = MIN(0x2000, uncompressed_pos);
		/* maximum len we can encode into metadata */
		const uint32_t max_len = MIN(0xFFFF + 3, uncompressed_size - uncompressed_pos);

		/* search for the longest match in the window for the lookahead buffer */
		for (offset = 1; (uint32_t)offset <= max_offset; offset++) {
			uint32_t len;

			for (len = 0;
			     (len < max_len) && (uncompressed[uncompressed_pos + len] ==
						 uncompressed[uncompressed_pos + len - offset]);
			     len++);

			/*
			 * We check if len is better than the value found before, including the
			 * sequence of identical bytes
			 */
			if (len > best_len) {
				found = true;
				best_len = len;
				best_offset = offset;
				if (best_len == max_len) {
					/* We're not going to do better than this */
					break;
				}
			}
		}

		if (!found) {
			/*
			 * This is going to literal byte, which we flag by
			 * setting a bit in an indicator field somewhere
			 * earlier in the stream.
			 */
			CHECK_INPUT_BYTES(sizeof(uint8_t));
			CHECK_OUTPUT_BYTES(sizeof(uint8_t));
			compressed[compressed_pos++] = uncompressed[uncompressed_pos++];

			indic <<= 1;
			indic_bit += 1;

			if (indic_bit == 32) {
				PUSH_LE_U32(compressed, indic_pos, indic);
				indic_bit = 0;
				CHECK_OUTPUT_BYTES(sizeof(uint32_t));
				indic_pos = compressed_pos;
				compressed_pos += sizeof(uint32_t);
			}
		} else {
			uint32_t match_len = best_len;

			uint16_t metadata;

			match_len -= 3;
			best_offset -= 1;

			/* Classical meta-data */
			CHECK_OUTPUT_BYTES(sizeof(uint16_t));
			metadata = (uint16_t)((best_offset << 3) | MIN(match_len, 7));
			PUSH_LE_U16(compressed, compressed_pos, metadata);
			compressed_pos += sizeof(uint16_t);

			if (match_len >= 7) {
				match_len -= 7;

				if (!nibble_index) {
					nibble_index = compressed_pos;

					CHECK_OUTPUT_BYTES(sizeof(uint8_t));
					compressed[nibble_index] = MIN(match_len, 15);
					compressed_pos += sizeof(uint8_t);
				} else {
					compressed[nibble_index] |= MIN(match_len, 15) << 4;
					nibble_index = 0;
				}

				if (match_len >= 15) {
					match_len -= 15;

					CHECK_OUTPUT_BYTES(sizeof(uint8_t));
					compressed[compressed_pos] = MIN(match_len, 255);
					compressed_pos += sizeof(uint8_t);

					if (match_len >= 255) {
						/* Additional match_len */

						match_len += 7 + 15;

						if (match_len < (1 << 16)) {
							CHECK_OUTPUT_BYTES(sizeof(uint16_t));
							PUSH_LE_U16(compressed, compressed_pos, match_len);
							compressed_pos += sizeof(uint16_t);
						} else {
							CHECK_OUTPUT_BYTES(sizeof(uint16_t) + sizeof(uint32_t));
							PUSH_LE_U16(compressed, compressed_pos, 0);
							compressed_pos += sizeof(uint16_t);

							PUSH_LE_U32(compressed, compressed_pos, match_len);
							compressed_pos += sizeof(uint32_t);
						}
					}
				}
			}

			indic = (indic << 1) | 1;
			indic_bit += 1;

			if (indic_bit == 32) {
				PUSH_LE_U32(compressed, indic_pos, indic);
				indic_bit = 0;
				CHECK_OUTPUT_BYTES(sizeof(uint32_t));
				indic_pos = compressed_pos;
				compressed_pos += sizeof(uint32_t);
			}

			uncompressed_pos += best_len;
		}
	}

	if (indic_bit != 0) {
		indic <<= 32 - indic_bit;
	}
	indic |= UINT32_MAX >> indic_bit;
	PUSH_LE_U32(compressed, indic_pos, indic);

	return compressed_pos;
}

ssize_t lzxpress_decompress(const uint8_t *input,
			    uint32_t input_size,
			    uint8_t *output,
			    uint32_t max_output_size)
{
	/*
	 * This is the algorithm in [MS-XCA] 2.4 "Plain LZ77 Decompression
	 * Algorithm Details".
	 */
	uint32_t output_index, input_index;
	uint32_t indicator, indicator_bit;
	uint32_t nibble_index;

	if (input_size == 0) {
		return 0;
	}

	output_index = 0;
	input_index = 0;
	indicator = 0;
	indicator_bit = 0;
	nibble_index = 0;

#undef CHECK_INPUT_BYTES
#define CHECK_INPUT_BYTES(__needed) \
	__CHECK_BYTES(input_size, input_index, __needed)
#undef CHECK_OUTPUT_BYTES
#define CHECK_OUTPUT_BYTES(__needed) \
	__CHECK_BYTES(max_output_size, output_index, __needed)

	do {
		if (indicator_bit == 0) {
			CHECK_INPUT_BYTES(sizeof(uint32_t));
			indicator = PULL_LE_U32(input, input_index);
			input_index += sizeof(uint32_t);
			if (input_index == input_size) {
				/*
				 * The compressor left room for indicator
				 * flags for data that doesn't exist.
				 */
				break;
			}
			indicator_bit = 32;
		}
		indicator_bit--;

		/*
		 * check whether the bit specified by indicator_bit is set or not
		 * set in indicator. For example, if indicator_bit has value 4
		 * check whether the 4th bit of the value in indicator is set
		 */
		if (((indicator >> indicator_bit) & 1) == 0) {
			CHECK_INPUT_BYTES(sizeof(uint8_t));
			CHECK_OUTPUT_BYTES(sizeof(uint8_t));
			output[output_index] = input[input_index];
			input_index += sizeof(uint8_t);
			output_index += sizeof(uint8_t);
		} else {
			uint32_t length;
			uint32_t offset;

			CHECK_INPUT_BYTES(sizeof(uint16_t));
			length = PULL_LE_U16(input, input_index);
			input_index += sizeof(uint16_t);
			offset = (length >> 3) + 1;
			length &= 7;

			if (length == 7) {
				if (nibble_index == 0) {
					CHECK_INPUT_BYTES(sizeof(uint8_t));
					nibble_index = input_index;
					length = input[input_index] & 0xf;
					input_index += sizeof(uint8_t);
				} else {
					length = input[nibble_index] >> 4;
					nibble_index = 0;
				}

				if (length == 15) {
					CHECK_INPUT_BYTES(sizeof(uint8_t));
					length = input[input_index];
					input_index += sizeof(uint8_t);
					if (length == 255) {
						CHECK_INPUT_BYTES(sizeof(uint16_t));
						length = PULL_LE_U16(input, input_index);
						input_index += sizeof(uint16_t);
						if (length == 0) {
							CHECK_INPUT_BYTES(sizeof(uint32_t));
							length = PULL_LE_U32(input, input_index);
							input_index += sizeof(uint32_t);
						}

						if (length < (15 + 7)) {
							return -1;
						}
						length -= (15 + 7);
					}
					length += 15;
				}
				length += 7;
			}
			length += 3;

			if (length == 0) {
				return -1;
			}

			for (; length > 0; --length) {
				if (offset > output_index) {
					return -1;
				}
				CHECK_OUTPUT_BYTES(sizeof(uint8_t));
				output[output_index] = output[output_index - offset];
				output_index += sizeof(uint8_t);
			}
		}
	} while ((output_index < max_output_size) && (input_index < (input_size)));

	return output_index;
}
