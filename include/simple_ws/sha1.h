/*
 *  sha1.h
 *
 *  Description:
 *      This is the header file for code which implements the Secure
 *      Hashing Algorithm 1 as defined in FIPS PUB 180-1 published
 *      April 17, 1995.
 *
 *      Many of the variable names in this code, especially the
 *      single character names, were used because those were the names
 *      used in the publication.
 *
 *      Please read the file sha1.c for more information.
 *
 */

#ifndef __SIMPLE_WS_SHA1_H
#define __SIMPLE_WS_SHA1_H

#include <stdint.h>
/*
 * If you do not have the ISO standard stdint.h header file, then you
 * must typdef the following:
 *    name              meaning
 *  uint32_t         unsigned 32 bit integer
 *  uint8_t          unsigned 8 bit integer (i.e., unsigned char)
 *  int_least16_t    integer of >= 16 bits
 *
 */

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef _SHA_enum_
#define _SHA_enum_
enum
{
	shaSuccess = 0,
	shaNull,				 /* Null pointer parameter */
	shaInputTooLong, /* input data too long */
	shaStateError		 /* called Input after Result */
};
#endif
#define SHA1HashSize 20

/*
	*  This structure will hold context information for the SHA-1
	*  hashing operation
	*/
typedef struct SHA1Context
{
	uint32_t Intermediate_Hash[SHA1HashSize / 4]; /* Message Digest  */

	uint32_t Length_Low;	/* Message length in bits      */
	uint32_t Length_High; /* Message length in bits      */

	/* Index into message block array   */
	int_least16_t Message_Block_Index;
	uint8_t Message_Block[64]; /* 512-bit message blocks      */

	int Computed;	 /* Is the digest computed?         */
	int Corrupted; /* Is the message digest corrupted? */
} SHA1Context;

/*
*  Function Prototypes
*/

/**
 * @brief Initialize a SHA-1 context for a new hash computation.
 * @param context SHA-1 context to initialize.
 * @return shaSuccess on success, otherwise an error code.
 */
int SHA1Reset(SHA1Context *);

/**
 * @brief Feed input bytes into an active SHA-1 context.
 * @param context SHA-1 context to update.
 * @param message_array Input bytes to hash.
 * @param length Number of bytes in message_array.
 * @return shaSuccess on success, otherwise an error code.
 */
int SHA1Input(SHA1Context *, const uint8_t *, unsigned int);

/**
 * @brief Finalize hashing and write the SHA-1 digest.
 * @param context SHA-1 context to finalize.
 * @param Message_Digest Output buffer for the 20-byte digest.
 * @return shaSuccess on success, otherwise an error code.
 */
int SHA1Result(SHA1Context *,
								uint8_t Message_Digest[SHA1HashSize]);

/**
 * @brief Convenience helper that computes SHA-1 for a byte sequence.
 * @param hash_out Output buffer for the 20-byte digest.
 * @param str Input byte sequence to hash.
 * @param len Number of bytes in str.
 * @return No return value.
 */
void SHA1(uint8_t *hash_out, const char *str, int len);

#ifdef __cplusplus
}
#endif

#endif /* __SIMPLE_WS_SHA1_H */