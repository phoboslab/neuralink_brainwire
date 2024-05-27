/*

Copyright (c) 2024, Dominic Szablewski - https://phoboslab.org
SPDX-License-Identifier: MIT

Command line tool to compress neuralink samples

Compile with: 
	gcc bwenc.c -std=c99 -lm -O3 -o bwenc

Usage:
	./bwenc in.wav comp.bw
	./bwenc comp.bw decomp.wav

*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define ABORT(...) \
	printf("Abort at line " TOSTRING(__LINE__) ": " __VA_ARGS__); \
	printf("\n"); \
	exit(1)
#define ASSERT(TEST, ...) \
	if (!(TEST)) { \
		ABORT(__VA_ARGS__); \
	}

#define STR_ENDS_WITH(S, E) (strcmp(S + strlen(S) - (sizeof(E)-1), E) == 0)

typedef struct {
	uint32_t channels;
	uint32_t samplerate;
	uint32_t samples;
} samples_t;


/* -----------------------------------------------------------------------------
	WAV reader / writer */

#define WAV_CHUNK_ID(S) \
	(((uint32_t)(S[3])) << 24 | ((uint32_t)(S[2])) << 16 | \
	 ((uint32_t)(S[1])) <<  8 | ((uint32_t)(S[0])))

void fwrite_u32_le(uint32_t v, FILE *fh) {
	uint8_t buf[sizeof(uint32_t)];
	buf[0] = 0xff & (v      );
	buf[1] = 0xff & (v >>  8);
	buf[2] = 0xff & (v >> 16);
	buf[3] = 0xff & (v >> 24);
	int wrote = fwrite(buf, sizeof(uint32_t), 1, fh);
	ASSERT(wrote, "Write error");
}

void fwrite_u16_le(unsigned short v, FILE *fh) {
	uint8_t buf[sizeof(unsigned short)];
	buf[0] = 0xff & (v      );
	buf[1] = 0xff & (v >>  8);
	int wrote = fwrite(buf, sizeof(unsigned short), 1, fh);
	ASSERT(wrote, "Write error");
}

uint32_t fread_u32_le(FILE *fh) {
	uint8_t buf[sizeof(uint32_t)];
	int read = fread(buf, sizeof(uint32_t), 1, fh);
	ASSERT(read, "Read error or unexpected end of file");
	return (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
}

unsigned short fread_u16_le(FILE *fh) {
	uint8_t buf[sizeof(unsigned short)];
	int read = fread(buf, sizeof(unsigned short), 1, fh);
	ASSERT(read, "Read error or unexpected end of file");
	return (buf[1] << 8) | buf[0];
}

int wav_write(const char *path, short *sample_data, samples_t *desc) {
	uint32_t data_size = desc->samples * desc->channels * sizeof(short);
	uint32_t samplerate = desc->samplerate;
	short bits_per_sample = 16;
	short channels = desc->channels;

	// Lifted from https://www.jonolick.com/code.html - public domain
	// Made endian agnostic using fwrite_u*()
	FILE *fh = fopen(path, "wb");
	ASSERT(fh, "Can't open %s for writing", path);
	fwrite("RIFF", 1, 4, fh);
	fwrite_u32_le(data_size + 44 - 8, fh);
	fwrite("WAVEfmt \x10\x00\x00\x00\x01\x00", 1, 14, fh);
	fwrite_u16_le(channels, fh);
	fwrite_u32_le(samplerate, fh);
	fwrite_u32_le(channels * samplerate * bits_per_sample/8, fh);
	fwrite_u16_le(channels * bits_per_sample/8, fh);
	fwrite_u16_le(bits_per_sample, fh);
	fwrite("data", 1, 4, fh);
	fwrite_u32_le(data_size, fh);
	fwrite((void*)sample_data, data_size, 1, fh);
	fclose(fh);
	return data_size  + 44 - 8;
}

short *wav_read(const char *path, samples_t *desc) {
	FILE *fh = fopen(path, "rb");
	ASSERT(fh, "Can't open %s for reading", path);

	uint32_t container_type = fread_u32_le(fh);
	ASSERT(container_type == WAV_CHUNK_ID("RIFF"), "Not a RIFF container");

	uint32_t wav_size = fread_u32_le(fh);
	uint32_t wavid = fread_u32_le(fh);
	ASSERT(wavid == WAV_CHUNK_ID("WAVE"), "No WAVE id found");

	uint32_t data_size = 0;
	uint32_t format_length = 0;
	uint32_t format_type = 0;
	uint32_t channels = 0;
	uint32_t samplerate = 0;
	uint32_t byte_rate = 0;
	uint32_t block_align = 0;
	uint32_t bits_per_sample = 0;

	// Find the fmt and data chunk, skip all others
	while (1) {
		uint32_t chunk_type = fread_u32_le(fh);
		uint32_t chunk_size = fread_u32_le(fh);

		if (chunk_type == WAV_CHUNK_ID("fmt ")) {
			ASSERT(chunk_size == 16 || chunk_size == 18, "WAV fmt chunk size missmatch");

			format_type = fread_u16_le(fh);
			channels = fread_u16_le(fh);
			samplerate = fread_u32_le(fh);
			byte_rate = fread_u32_le(fh);
			block_align = fread_u16_le(fh);
			bits_per_sample = fread_u16_le(fh);

			if (chunk_size == 18) {
				unsigned short extra_params = fread_u16_le(fh);
				ASSERT(extra_params == 0, "WAV fmt extra params not supported");
			}
		}
		else if (chunk_type == WAV_CHUNK_ID("data")) {
			data_size = chunk_size;
			break;
		}
		else {
			int seek_result = fseek(fh, chunk_size, SEEK_CUR);
			ASSERT(seek_result == 0, "Malformed RIFF header");
		}
	}

	ASSERT(format_type == 1, "Type in fmt chunk is not PCM");
	ASSERT(bits_per_sample == 16, "Bits per samples != 16");
	ASSERT(data_size, "No data chunk");

	uint8_t *wav_bytes = malloc(data_size);
	ASSERT(wav_bytes, "Malloc for %d bytes failed", data_size);
	int read = fread(wav_bytes, data_size, 1, fh);
	ASSERT(read, "Read error or unexpected end of file for %d bytes", data_size);
	fclose(fh);

	desc->samplerate = samplerate;
	desc->samples = data_size / (channels * (bits_per_sample/8));
	desc->channels = channels;

	return (short*)wav_bytes;
}



/* -----------------------------------------------------------------------------
	BRAINWIRE reader / writer */

static inline int rice_read(uint8_t *bytes, int *bit_pos, uint32_t k) {
	int msbs = 0;
	int p = *bit_pos;
	while (!(bytes[p >> 3] & (1 << (7-(p & 7))))) {
		p++;
		msbs++;
	}
	p++;

	int count = k;
	int lsbs = 0;
	while (count) {
		int remaining = 8 - (p & 7);
		int read = remaining < count ? remaining : count;
		int shift = remaining - read;
		int mask = (0xff >> (8 - read));
		lsbs = (lsbs << read) | ((bytes[p >> 3] & (mask << shift)) >> shift);
		p += read;
		count -= read;
	}
	*bit_pos = p;

	int val;
	uint32_t uval = (msbs << k) | lsbs;
	if (uval & 1) {
		val = -((int)(uval >> 1)) - 1;
	}
	else {
		val = (int)(uval >> 1);
	}

	return val;
}

static inline int rice_write(uint8_t *bytes, int *bit_pos, int val, uint32_t k) {
	uint32_t uval = val;
	uval <<= 1;
	uval ^= (val >> 31);

	uint32_t msbs = uval >> k;
	uint32_t lsbs = 1 + k;
	uint32_t count = msbs + lsbs;
	uint32_t pattern = 1 << k; // the unary end bit
	pattern |= (uval & ((1 << k)-1)); // the binary LSBs

	int pos = *bit_pos;
	while (count) {
		int occupied = (pos & 7);
		int remaining = 8 - occupied;
		int written = remaining < count ? remaining : count;
		int bits = 0;
		if (count - written < 31) {
			bits = (pattern >> (count - written)) << (remaining - written);
			bits &= (0xff >> occupied);
		}
		bytes[pos >> 3] |= bits;
		pos += written;
		count -= written;
	}
	*bit_pos = pos;
	return msbs + lsbs;
}


static inline int brainwire_dequant(int v) {
	// Not really sure what's goin on here. The original 10bit data was 
	// upscaled to 16 bit somehow. It wasn't a simple bit shift. This thing
	// here was found through a brute force search and just happens to 
	// replicate neuralink's original upscale.
	if (v >= 0) {
		return round(v * 64.061577 + 31.034184);
	}
	else {
		return -round((-v -1) * 64.061577 + 31.034184) - 1;
	}
}

static inline int brainwire_quant(int v) {
	return (int)floor(v/64.0);
}

short *brainwire_read(const char *path, samples_t *desc) {
	FILE *fh = fopen(path, "rb");
	ASSERT(fh, "Couldnt open %s for reading", path);

	fseek(fh, 0, SEEK_END);
	int size = ftell(fh);
	fseek(fh, 0, SEEK_SET);

	uint8_t *bytes = malloc(size);
	int bytes_read = fread(bytes, 1, size, fh);
	ASSERT(size > 0 && bytes_read == size, "Read failed");
	fclose(fh);


	int bit_pos = 0;
	float rice_k = 3;

	int samples = rice_read(bytes, &bit_pos, 16);
	int samplerate = rice_read(bytes, &bit_pos, 16);
	short *sample_data = malloc(samples * sizeof(short));

	int prev_quantized = 0;
	for (int i = 0; i < samples; i++) {
		int temp = bit_pos;

		int residual = rice_read(bytes, &bit_pos, rice_k);
		int quantized = prev_quantized + residual;
		prev_quantized = quantized;
		sample_data[i] = brainwire_dequant(quantized);

		int encoded_len = bit_pos - temp;
		rice_k = rice_k * 0.99 + (encoded_len / 1.55) * 0.01;
	}

	desc->channels = 1;
	desc->samples = samples;
	desc->samplerate = samplerate;
	return sample_data;
}

int brainwire_write(const char *path, short *sample_data, samples_t *desc) {
	uint8_t *bytes = malloc(desc->samples * 2); // just to be sure...
	memset(bytes, 0, desc->samples * 2);

	int bit_pos = 0;
	float rice_k = 3;

	rice_write(bytes, &bit_pos, desc->samples, 16);
	rice_write(bytes, &bit_pos, desc->samplerate, 16);
	
	int prev_quantized = 0;
	for (int i = 0; i < desc->samples; i++) {
		int quantized = brainwire_quant(sample_data[i]);
		int residual = quantized - prev_quantized;
		prev_quantized = quantized;

		int encoded_len = rice_write(bytes, &bit_pos, residual, rice_k);
		rice_k = rice_k * 0.99 + (encoded_len / 1.55) * 0.01;
	}

	int byte_len = (bit_pos + 7) / 8;
	FILE *fh = fopen(path, "wb");
	ASSERT(fh, "Couldnt open %s for writing", path);
	fwrite(bytes, 1, byte_len, fh);
	fclose(fh);

	return byte_len;
}



/* -----------------------------------------------------------------------------
	Main */

int main(int argc, char **argv) {
	ASSERT(argc >= 3, "\nUsage: bwenc in.{wav,bw} out.{wav,bw}")

	samples_t desc;
	short *sample_data = NULL;

	// Decode input

	if (STR_ENDS_WITH(argv[1], ".wav")) {
		sample_data = wav_read(argv[1], &desc);
	}
	else if (STR_ENDS_WITH(argv[1], ".bw")) {
		sample_data = brainwire_read(argv[1], &desc);
	}
	else {
		ABORT("Unknown file type for %s", argv[1]);
	}

	ASSERT(sample_data, "Can't load/decode %s", argv[1]);


	// Encode output
	
	int bytes_written = 0;
	double psnr = 1.0/0.0;
	if (STR_ENDS_WITH(argv[2], ".wav")) {
		bytes_written = wav_write(argv[2], sample_data, &desc);
	}
	else if (STR_ENDS_WITH(argv[2], ".bw")) {
		bytes_written = brainwire_write(argv[2], sample_data, &desc);
	}
	else {
		ABORT("Unknown file type for %s", argv[2]);
	}

	ASSERT(bytes_written, "Can't write/encode %s", argv[2]);
	free(sample_data);

	printf(
		"%s: size: %d kb (%d bytes) = %.2fx compression\n",
		argv[2], bytes_written/1024, bytes_written, 
		(float)(desc.samples * sizeof(short))/(float)bytes_written
	);

	return 0;
}
