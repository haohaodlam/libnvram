#ifndef _AESENDE_H
#define _AESENDE_H

#if defined (__cplusplus)
extern   "C" 
{
#endif

#define AES_OPTION_PKCS_PADDING 	0x00000001
#define AES_OPTION_ECB 				0x00010000
#define AES_OPTION_CBC 				0x00020000
#define AES_OPTION_OFB 				0x00040000
#define AES_OPTION_CFB 				0x00080000

#define AES_ERR_SUCCESS				0
#define AES_ERR_INVALID_HANLDE		0x2000
#define AES_ERR_INVALID_USERKEYLEN	0x2001
#define AES_ERR_INVALID_OPTION		0x2003
#define AES_ERR_NO_MEM				0x2004
#define AES_ERR_UNKOWN				0x2fff

int AESEncode (unsigned int options,const void* key, size_t keyLength, const void *iv,const void* dataIn, size_t dataInLength, void* dataOut, int dataOutAvailable, int* dataOutMoved);
int AESDecode (unsigned int options,const char* key, size_t keyLength, const void *iv,const void* dataIn, size_t dataInLength, void* dataOut, int dataOutAvailable, int* dataOutMoved);

#if defined (__cplusplus)
}
#endif

#endif
