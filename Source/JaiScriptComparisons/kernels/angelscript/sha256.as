#include "profile.as"

// 32-bit words as uint: additions wrap and >> is a logical shift, so no masks
const array<uint> K = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void digest_block(const string &in msg, uint base, array<uint>& hash, array<uint>& w) {
	for (uint j = 0; j < 16; ++j) {
		uint p = base + j * 4;
		w[j] = (uint(msg[p]) << 24) | (uint(msg[p + 1]) << 16) | (uint(msg[p + 2]) << 8) | uint(msg[p + 3]);
	}
	for (uint j = 16; j < 64; ++j) {
		uint v0 = w[j - 15];
		uint s0 = ((v0 >> 7) | (v0 << 25)) ^ ((v0 >> 18) | (v0 << 14)) ^ (v0 >> 3);
		uint v1 = w[j - 2];
		uint s1 = ((v1 >> 17) | (v1 << 15)) ^ ((v1 >> 19) | (v1 << 13)) ^ (v1 >> 10);
		w[j] = w[j - 16] + s0 + w[j - 7] + s1;
	}
	uint a = hash[0];
	uint b = hash[1];
	uint c = hash[2];
	uint d = hash[3];
	uint e = hash[4];
	uint f = hash[5];
	uint g = hash[6];
	uint h = hash[7];
	for (uint i = 0; i < 64; ++i) {
		uint s0 = ((a >> 2) | (a << 30)) ^ ((a >> 13) | (a << 19)) ^ ((a >> 22) | (a << 10));
		uint maj = (a & b) ^ (a & c) ^ (b & c);
		uint t2 = s0 + maj;
		uint s1 = ((e >> 6) | (e << 26)) ^ ((e >> 11) | (e << 21)) ^ ((e >> 25) | (e << 7));
		uint ch = (e & f) ^ ((~e) & g);
		uint t1 = h + s1 + ch + K[i] + w[i];
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	hash[0] = hash[0] + a;
	hash[1] = hash[1] + b;
	hash[2] = hash[2] + c;
	hash[3] = hash[3] + d;
	hash[4] = hash[4] + e;
	hash[5] = hash[5] + f;
	hash[6] = hash[6] + g;
	hash[7] = hash[7] + h;
}

array<uint> W(64);

string sha256(const string &in msg) {
	array<uint> hash = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	uint blocks = msg.length() / 64;
	for (uint i = 0; i < blocks; ++i) {
		digest_block(msg, i * 64, hash, W);
	}
	string result;
	for (uint i = 0; i < 8; ++i) {
		result += formatUInt(hash[i], "0h", 8);
	}
	return result;
}

string input;
string result;

void main() {
	for (int i = 0; i < 1024; ++i) {
		input += ".";
	}
	profile_it("sha256", function() {
		for (int i = 0; i < 32; ++i) {
			result = sha256(input);
		}
	});
	print("checksum " + result);
}
