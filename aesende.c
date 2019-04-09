#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "aes.h"
#include "aesende.h"

#define USER_KEY

int safe_free(char *str)
{
	if(str)
		free(str);

	return 0;
}

int AESEncode (unsigned int options,const void* key, size_t keyLength, const void *iv,const void* dataIn, size_t dataInLength, void* dataOut, int dataOutAvailable, int* dataOutMoved)
{
    unsigned char   UserKey[AES_USER_KEY_LEN]={0};
	DWORD			ModeID;
	DWORD			PadType;
    DWORD   		DstLen;
    RET_VAL 		ret;
    AES_ALG_INFO    AlgInfo;
    char 			*pOut = NULL;
    unsigned int 	len = 16 * (dataInLength/16 + 1);
    int 			eelen = 0;

    if(dataIn == NULL || key == NULL || dataOutMoved == NULL)
    {
        return AES_ERR_INVALID_HANLDE;
   	}

	if(keyLength != 16 && keyLength != 24 && keyLength != 32)
	{
        return AES_ERR_INVALID_USERKEYLEN;
	}

	memcpy(UserKey,key,keyLength);

	if(AES_OPTION_PKCS_PADDING & options)
	{
		PadType = AI_PKCS_PADDING;
	}
	else
	{
		PadType = AI_NO_PADDING;
	}
	if(AES_OPTION_ECB & options)
	{
		ModeID = AI_ECB;
	}
	else if(AES_OPTION_CBC & options)
	{
		ModeID = AI_CBC;
	}
	else if(AES_OPTION_OFB & options)
	{
		ModeID = AI_OFB;
	}
	else if(AES_OPTION_CFB & options)
	{
		ModeID = AI_CFB;
	}
	else
	{
        return AES_ERR_INVALID_OPTION;
	}
    //
    AES_SetAlgInfo(ModeID, PadType, (BYTE *)iv, &AlgInfo);

    pOut = (char*)calloc (1, len + 4);
    if (pOut == NULL)
        return AES_ERR_NO_MEM;

    DstLen = len;

    //Encryption
    ret = AES_EncKeySchedule(UserKey, keyLength, &AlgInfo);
    if( ret!=CTR_SUCCESS )
    {
        safe_free (pOut);
        return ret;
    }

    ret = AES_EncInit(&AlgInfo);
    if( ret!=CTR_SUCCESS )
    {
        safe_free (pOut);
        return ret;
    }

    ret = AES_EncUpdate(&AlgInfo, (unsigned char*)dataIn, dataInLength, (unsigned char*)pOut, &DstLen);
    if( ret!=CTR_SUCCESS )
    {
        safe_free (pOut);
        return ret;
    }
    eelen = DstLen;

    ret = AES_EncFinal(&AlgInfo, (unsigned char*)pOut+eelen, &DstLen);
    if( ret!=CTR_SUCCESS )
    {
        safe_free (pOut);
        return ret;
    }
    eelen += DstLen;

    *dataOutMoved = (dataOutAvailable>eelen)?eelen:dataOutAvailable;
	memcpy(dataOut,pOut,*dataOutMoved);
	safe_free (pOut);
    return AES_ERR_SUCCESS;
}

int AESDecode (unsigned int options,const char* key, size_t keyLength, const void *iv,const void* dataIn, size_t dataInLength, void* dataOut, int dataOutAvailable, int* dataOutMoved)
{
    unsigned char   UserKey[AES_USER_KEY_LEN]={0};
	DWORD			ModeID;
	DWORD			PadType;
    DWORD   		DstLen;
    RET_VAL 		ret;
    AES_ALG_INFO    AlgInfo;
    char 			*pOut = NULL;
    int 			ddlen = 0;

    if(dataIn == NULL || key == NULL || dataOutMoved == NULL)
	{
		return AES_ERR_INVALID_HANLDE;
	}

	if(keyLength != 16 && keyLength != 24 && keyLength != 32)
	{
        return AES_ERR_INVALID_USERKEYLEN;
	}

	memcpy(UserKey,key,keyLength);

	if(AES_OPTION_PKCS_PADDING & options)
	{
		PadType = AI_PKCS_PADDING;
	}
	else
	{
		PadType = AI_NO_PADDING;
	}
	if(AES_OPTION_ECB & options)
	{
		ModeID = AI_ECB;
	}
	else if(AES_OPTION_CBC & options)
	{
		ModeID = AI_CBC;
	}
	else if(AES_OPTION_OFB & options)
	{
		ModeID = AI_OFB;
	}
	else if(AES_OPTION_CFB & options)
	{
		ModeID = AI_CFB;
	}
	else
	{
        return AES_ERR_INVALID_OPTION;
	}
    //
    AES_SetAlgInfo(ModeID, PadType, (BYTE *)iv, &AlgInfo);

    pOut = (char*)calloc(1, dataInLength + 2);
    if (pOut == NULL)
        return AES_ERR_NO_MEM;

    DstLen = dataInLength;

    //Decryption
    ret = AES_DecKeySchedule(UserKey, keyLength, &AlgInfo);
    if( ret!=CTR_SUCCESS )
    {
        safe_free (pOut);
        return ret;
    }

    ret = AES_DecInit(&AlgInfo);
    if( ret!=CTR_SUCCESS )
    {
        safe_free (pOut);
        return ret;
    }

    ret = AES_DecUpdate(&AlgInfo, (unsigned char*)dataIn, dataInLength, (unsigned char*)pOut, &DstLen);
    if( ret!=CTR_SUCCESS )
    {
        safe_free (pOut);
        return ret;
    }
    ddlen = DstLen;

    ret = AES_DecFinal(&AlgInfo, (unsigned char*)pOut+ddlen, &DstLen);
    if( ret!=CTR_SUCCESS )
    {
        safe_free (pOut);
        return ret;
    }
    ddlen += DstLen;

    *dataOutMoved = (dataOutAvailable>ddlen)?ddlen:dataOutAvailable;
	memcpy(dataOut,pOut,*dataOutMoved);
	safe_free (pOut);
    return AES_ERR_SUCCESS;
}

