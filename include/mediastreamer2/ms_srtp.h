/*
  mediastreamer2 library - modular sound and video processing and streaming
  Copyright (C) 2006-2014 Belledonne Communications, Grenoble

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef ms_srtp_h
#define ms_srtp_h

#include <ortp/rtpsession.h>
#include "mediastreamer2/mscommon.h"

#ifdef __cplusplus
extern "C"{
#endif
/* defined in mediastream.h */
struct _MediaStream;

/*
 * Crypto suite used configure encrypted stream*/
typedef enum _MSCryptoSuite{
        MS_CRYPTO_SUITE_INVALID=0,
        MS_AES_128_SHA1_80,
        MS_AES_128_SHA1_32,
        MS_AES_128_NO_AUTH,
        MS_NO_CIPHER_SHA1_80,
        MS_AES_256_SHA1_80,
        MS_AES_256_SHA1_32
} MSCryptoSuite;

typedef struct _MSCryptoSuiteNameParams{
        const char *name;
        const char *params;
}MSCryptoSuiteNameParams;

MS2_PUBLIC MSCryptoSuite ms_crypto_suite_build_from_name_params(const MSCryptoSuiteNameParams *nameparams);
MS2_PUBLIC int ms_crypto_suite_to_name_params(MSCryptoSuite cs, MSCryptoSuiteNameParams *nameparams);


/* defined in srtp.h*/
typedef struct srtp_ctx_t *MSSrtpCtx;

typedef enum {
	MSSRTP_RTP_STREAM,
	MSSRTP_RTCP_STREAM,
	MSSRTP_ALL_STREAMS
} MSSrtpStreamType;
/**
 * Check if SRTP is supported
 * @return true if SRTP is supported
 */
MS2_PUBLIC bool_t ms_srtp_supported(void);

/**
 * Set srtp receiver key for the given media stream.
 * If no srtp session exists on the stream it is created, if it already exists srtp policy is created/modified for the receiver side of the stream.
 *
 * @param[in/out]	stream	The mediastream to operate on
 * @param[in]		suite	The srtp crypto suite to use
 * @param[in]		key	Srtp master key and master salt in a base 64 NULL terminated string
 * @return	0 on success, error code otherwise
 */
MS2_PUBLIC int media_stream_set_srtp_recv_key_b64(struct _MediaStream *stream, MSCryptoSuite suite, const char* key);

/**
 * Set srtp receiver key for the given media stream.
 * If no srtp session exists on the stream it is created, if it already exists srtp policy is created/modified for the receiver side of the stream.
 *
 * @param[in/out]	stream		The mediastream to operate on
 * @param[in]		suite		The srtp crypto suite to use
 * @param[in]		key		Srtp master key and master salt
 * @param[in]		key_length	key buffer length
 * @param[in]		stream_type	Srtp suite is applied to RTP stream, RTCP stream or both
 * @return	0 on success, error code otherwise
 */
MS2_PUBLIC int media_stream_set_srtp_recv_key(struct _MediaStream *stream, MSCryptoSuite suite, const char* key, size_t key_length, MSSrtpStreamType stream_type);

/**
 * Set srtp sender key for the given media stream.
 * If no srtp session exists on the stream it is created, if it already exists srtp policy is created/modified for the sender side of the stream.
 *
 * @param[in/out]	stream	The mediastream to operate on
 * @param[in]		suite	The srtp crypto suite to use
 * @param[in]		key	Srtp master key and master salt in a base 64 NULL terminated string
 * @return	0 on success, error code otherwise
 */
MS2_PUBLIC int media_stream_set_srtp_send_key_b64(struct _MediaStream *stream, MSCryptoSuite suite, const char* key);

/**
 * Set srtp sender key for the given media stream.
 * If no srtp session exists on the stream it is created, if it already exists srtp policy is created/modified for the sender side of the stream.
 *
 * @param[in/out]	stream		The mediastream to operate on
 * @param[in]		suite		The srtp crypto suite to use
 * @param[in]		key		Srtp master key and master salt
 * @param[in]		key_length	key buffer length
 * @param[in]		stream_type	Srtp suite is applied to RTP stream, RTCP stream or both
 * @return	0 on success, error code otherwise
 */
MS2_PUBLIC int media_stream_set_srtp_send_key(struct _MediaStream *stream, MSCryptoSuite suite, const char* key, size_t key_length, MSSrtpStreamType stream_type);

/**
 * Deallocate ressources for a srtp session
 *
 * @param[in/out] session	SRTP session to be terminated
 * @return 0 on success
 */
MS2_PUBLIC int ms_srtp_dealloc(MSSrtpCtx session);
#ifdef __cplusplus
}
#endif

#endif /* ms_srtp_h */
