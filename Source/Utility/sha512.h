#ifndef _MV_SHA512_H_
#define _MV_SHA512_H_
#include <string>

namespace MV {
	class SHA512 {
	protected:
		typedef unsigned char uint8;
		typedef unsigned int uint32;
		typedef unsigned long long uint64;

		const static uint64 sha512_k[];
		static const unsigned int SHA384_512_BLOCK_SIZE = (1024 / 8);

	public:
		void init();
		void update(const unsigned char *message, unsigned int len);
		void final(unsigned char *digest);
		static const unsigned int DIGEST_SIZE = (512 / 8);

	protected:
		void transform(const unsigned char *message, unsigned int block_nb);
		unsigned int m_tot_len;
		unsigned int m_len;
		unsigned char m_block[2 * SHA384_512_BLOCK_SIZE];
		uint64 m_h[8];
	};


	std::string sha512(std::string input);
	//iterations = 2^work
	std::string sha512(std::string input, size_t work);
	std::string sha512(const std::string &input, const std::string &salt, size_t work);
}
#endif