/*
 * Copyright (c) 2010-2019 Belledonne Communications SARL.
 *
 * This file is part of mediastreamer2.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "mediastreamer-config.h"
#endif
#include "ortp/ortp.h"

#include "mediastreamer2/ms_srtp.h"
#include "mediastreamer2/mediastream.h"

#ifdef HAVE_SRTP

/*srtp defines all this stuff*/
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

#include "srtp_prefix.h"

#include "ortp/b64.h"

typedef struct _MSSrtpStreamContext {
	srtp_t srtp;
	RtpTransportModifier *modifier;
	ms_mutex_t mutex;
	bool_t secured;
	bool_t mandatory_enabled;
	bool_t is_rtp;
	/*fixed: for rtcp-mux*/
	struct _MSSrtpStreamContext *other_rtp;
} MSSrtpStreamContext;

struct _MSSrtpCtx {
	MSSrtpStreamContext send_rtp_context;
	MSSrtpStreamContext send_rtcp_context;
	MSSrtpStreamContext recv_rtp_context;
	MSSrtpStreamContext recv_rtcp_context;
};

MSSrtpCtx* ms_srtp_context_new(void) {
	MSSrtpCtx* ctx = ms_new0(struct _MSSrtpCtx,1);
	ctx->send_rtp_context.is_rtp=TRUE;
	ms_mutex_init(&ctx->send_rtp_context.mutex, NULL);
	ms_mutex_init(&ctx->send_rtcp_context.mutex, NULL);

	ctx->recv_rtp_context.is_rtp=TRUE;
	ms_mutex_init(&ctx->recv_rtp_context.mutex, NULL);
	ms_mutex_init(&ctx->recv_rtcp_context.mutex, NULL);

	/*fixed: for rtcp-mux*/
	ctx->send_rtp_context.other_rtp = &ctx->send_rtcp_context;
	ctx->send_rtcp_context.other_rtp = &ctx->send_rtp_context;
	ctx->recv_rtp_context.other_rtp = &ctx->recv_rtcp_context;
	ctx->recv_rtcp_context.other_rtp = &ctx->recv_rtp_context;

	return ctx;
}
void ms_srtp_context_delete(MSSrtpCtx* session) {
	ms_mutex_destroy(&session->send_rtp_context.mutex);
	ms_mutex_destroy(&session->send_rtcp_context.mutex);
	ms_mutex_destroy(&session->recv_rtp_context.mutex);
	ms_mutex_destroy(&session->recv_rtcp_context.mutex);
	if (session->send_rtp_context.srtp)
		srtp_dealloc(session->send_rtp_context.srtp);
	if (session->send_rtcp_context.srtp)
		srtp_dealloc(session->send_rtcp_context.srtp);
	if (session->recv_rtp_context.srtp)
		srtp_dealloc(session->recv_rtp_context.srtp);
	if (session->recv_rtcp_context.srtp)
		srtp_dealloc(session->recv_rtcp_context.srtp);

	ms_free(session);
}

/***********************************************/
/***** LOCAL FUNCTIONS                     *****/
/***********************************************/

static void check_and_create_srtp_context(MSMediaStreamSessions *sessions) {
	if (!sessions->srtp_context)
		sessions->srtp_context=ms_srtp_context_new();
}
/**** Sender functions ****/
static int _process_on_send(RtpSession* session,MSSrtpStreamContext *ctx, mblk_t *m){
	int slen;
	err_status_t err;
	bool_t is_rtp=ctx->is_rtp;
	rtp_header_t *rtp_header=is_rtp?(rtp_header_t*)m->b_rptr:NULL;
	rtcp_common_header_t *rtcp_header=!is_rtp?(rtcp_common_header_t*)m->b_rptr:NULL;
	slen=(int)msgdsize(m);

	/* fixed: when rtcp-mux is on and we need to send RTCP packet, we need to use same srtp context as used for RTP stream */
	if (!is_rtp && session->rtcp_mux) {
		if (ctx->other_rtp)
			ctx = ctx->other_rtp;
	}

	if (rtp_header && (slen>RTP_FIXED_HEADER_SIZE && rtp_header->version==2)) {
		ms_mutex_lock(&ctx->mutex);
		if (!ctx->secured) {
			// No need to protect RTP packets
			err=err_status_ok;
			if (ctx->mandatory_enabled) {
				slen = 0; /*droping packets*/
			}
		} else {
			/* defragment incoming message and enlarge the buffer for srtp to write its data */
			msgpullup(m,slen+SRTP_MAX_TRAILER_LEN+4 /*for 32 bits alignment*/);
			err=srtp_protect(ctx->srtp,m->b_rptr,&slen);
		}
		ms_mutex_unlock(&ctx->mutex);
	} else if (rtcp_header && (slen>RTP_FIXED_HEADER_SIZE && rtcp_header->version==2)) {
		ms_mutex_lock(&ctx->mutex);
		if (!ctx->secured) {
			// No need to protect RTCP packets
			err=err_status_ok;
			if (ctx->mandatory_enabled) {
				slen = 0; /*droping packets*/
			}
		} else {
		/* defragment incoming message and enlarge the buffer for srtp to write its data */
			msgpullup(m,slen+SRTP_MAX_TRAILER_LEN+4 /*for 32 bits alignment*/ + 4 /*required by srtp_protect_rtcp*/);
			err=srtp_protect_rtcp(ctx->srtp,m->b_rptr,&slen);
		}
		ms_mutex_unlock(&ctx->mutex);
	} else {
		/*ignoring non rtp/rtcp packets*/
 		return slen;
 	}

	/* check return code from srtp_protect */
	if (err==err_status_ok){
		return slen;
	}
	ortp_error("srtp_protect%s() failed (%d) for stream ctx [%p]", is_rtp?"":"_rtcp", err,ctx);
	return -1;
}

static int ms_srtp_process_on_send(RtpTransportModifier *t, mblk_t *m){
	return _process_on_send(t->session,(MSSrtpStreamContext*)t->data, m);
}

static int ms_srtp_process_dummy(RtpTransportModifier *t, mblk_t *m) {
	return (int)msgdsize(m);
}
static int _process_on_receive(RtpSession* session,MSSrtpStreamContext *ctx, mblk_t *m, int err){
	int slen;
	err_status_t srtp_err=err_status_ok;
	bool_t is_rtp=ctx->is_rtp;

	/* fixed: when rtcp-mux is on and we receive RTCP packet, we need to use same srtp context as used for RTP stream */
	if (!is_rtp && session->rtcp_mux) {
		if (ctx->other_rtp)
			ctx = ctx->other_rtp;
	}

	/* keep NON-RTP data unencrypted */
	if (is_rtp){
		rtp_header_t *rtp=(rtp_header_t*)m->b_rptr;
		if (err<RTP_FIXED_HEADER_SIZE || rtp->version!=2 ) {
			return err;
		}
	}else{
		rtcp_common_header_t *rtcp=(rtcp_common_header_t*)m->b_rptr;
		if (err<(int)(sizeof(rtcp_common_header_t)+4) || rtcp->version!=2 ) {
			return err;
		}
	}

	slen=err;
	if (ctx->secured) {
		srtp_err = is_rtp?srtp_unprotect(ctx->srtp,m->b_rptr,&slen):srtp_unprotect_rtcp(ctx->srtp,m->b_rptr,&slen);
		if (srtp_err==err_status_ok) {
			return slen;
		} else {
			ms_error("srtp_unprotect%s() failed (%d) on stream ctx [%p]", is_rtp?"":"_rtcp", srtp_err,ctx);
			return -1;
		}
	}else{
		if (ctx->mandatory_enabled) {
			return 0;
		} else {
			return slen;
		}
	}
	return 0;
}
static int ms_srtp_process_on_receive(RtpTransportModifier *t, mblk_t *m){
	return _process_on_receive(t->session,(MSSrtpStreamContext*)t->data, m,(int)msgdsize(m));
}

/**** Session management functions ****/

/**
 * deallocate transport modifier ressources
 * @param[in/out] tp	The transport modifier to be deallocated
 */
static void ms_srtp_transport_modifier_destroy(RtpTransportModifier *tp){
	ms_free(tp);
}

static MSSrtpStreamContext* get_stream_context(MSMediaStreamSessions *sessions, bool_t is_send, bool_t is_rtp) {
	if (is_send && is_rtp)
		return &sessions->srtp_context->send_rtp_context;
	else if (is_send && !is_rtp)
		return &sessions->srtp_context->send_rtcp_context;
	else if (!is_send && is_rtp)
		return &sessions->srtp_context->recv_rtp_context;
	else
		return &sessions->srtp_context->recv_rtcp_context;
}


static int ms_media_stream_session_fill_srtp_context(MSMediaStreamSessions *sessions, bool_t is_send, bool_t is_rtp) {
	err_status_t err=0;
	RtpTransport *transport=NULL;
	MSSrtpStreamContext* stream_ctx = get_stream_context(sessions,is_send,is_rtp);

	if (is_rtp) {
		rtp_session_get_transports(sessions->rtp_session,&transport,NULL);
	} else {
		rtp_session_get_transports(sessions->rtp_session,NULL,&transport);
	}

	ms_mutex_lock(&stream_ctx->mutex);

	if (stream_ctx->srtp && stream_ctx->secured) {
		/*we cannot reuse srtp context, so freeing first*/
		srtp_dealloc(stream_ctx->srtp);
		stream_ctx->srtp=NULL;
	}

	if (!stream_ctx->srtp) {
		err = srtp_create(&stream_ctx->srtp, NULL);
		if (err != 0) {
			ms_error("Failed to create srtp session (%d) for stream sessions [%p]", err,sessions);
			goto end;
		}
	}

	if (!stream_ctx->modifier) {
		stream_ctx->modifier=ms_new0(RtpTransportModifier,1);
		stream_ctx->modifier->data=stream_ctx;
		stream_ctx->modifier->t_process_on_send=is_send?ms_srtp_process_on_send:ms_srtp_process_dummy;
		stream_ctx->modifier->t_process_on_receive=is_send?ms_srtp_process_dummy:ms_srtp_process_on_receive;
		stream_ctx->modifier->t_destroy=ms_srtp_transport_modifier_destroy;
		meta_rtp_transport_append_modifier(transport, stream_ctx->modifier);
	}
end:
	ms_mutex_unlock(&stream_ctx->mutex);
	return err;

}
int ms_media_stream_sessions_fill_srtp_context_all_stream(struct _MSMediaStreamSessions *sessions) {
	int  err = -1;
	/*check if exist before filling*/

	if (!(get_stream_context(sessions, TRUE,TRUE)->srtp) && (err = ms_media_stream_session_fill_srtp_context(sessions, TRUE, TRUE)))
		 return err;
	if (!(get_stream_context(sessions, TRUE,FALSE)->srtp) && (err = ms_media_stream_session_fill_srtp_context(sessions, TRUE, FALSE)))
		 return err;
	if (!(get_stream_context(sessions, FALSE,TRUE)->srtp) && (err = ms_media_stream_session_fill_srtp_context(sessions, FALSE, TRUE)))
		 return err;

	if (!get_stream_context(sessions, FALSE,FALSE)->srtp)
		err = ms_media_stream_session_fill_srtp_context(sessions,FALSE,FALSE);

	return err;
}


static int ms_set_srtp_crypto_policy(MSCryptoSuite suite, crypto_policy_t *policy, bool_t is_rtp) {
	switch(suite){
		case MS_AES_128_SHA1_32:
			// srtp doc says: not adapted to rtcp...
			crypto_policy_set_aes_cm_128_hmac_sha1_32(policy);
			break;
		case MS_AES_128_SHA1_80_NO_AUTH:
		case MS_AES_128_SHA1_32_NO_AUTH:
			// srtp doc says: not adapted to rtcp...
			crypto_policy_set_aes_cm_128_null_auth(policy);
			break;
		case MS_AES_128_SHA1_80_SRTP_NO_CIPHER:
			// SRTP is not encrypted
			// SRTCP is encrypted
			(is_rtp) ? crypto_policy_set_null_cipher_hmac_sha1_80(policy) : crypto_policy_set_aes_cm_128_hmac_sha1_80(policy);
			break;
		case MS_AES_128_SHA1_80_SRTCP_NO_CIPHER:
			// SRTP is encrypted
			// SRTCP is not encrypted
			(is_rtp) ? crypto_policy_set_aes_cm_128_hmac_sha1_80(policy) : crypto_policy_set_null_cipher_hmac_sha1_80(policy);
			break;
		case MS_AES_128_SHA1_80_NO_CIPHER:
			// SRTP is not encrypted
			// SRTCP is not encrypted
			crypto_policy_set_null_cipher_hmac_sha1_80(policy);
			break;
		case MS_AES_128_SHA1_80: /*default mode*/
			crypto_policy_set_aes_cm_128_hmac_sha1_80(policy);
			break;
		case MS_AES_256_SHA1_80: // For backward compatibility
		case MS_AES_CM_256_SHA1_80:
			crypto_policy_set_aes_cm_256_hmac_sha1_80(policy);
			break;
		case MS_AES_256_SHA1_32:
			crypto_policy_set_aes_cm_256_hmac_sha1_32(policy);
			break;
		case MS_CRYPTO_SUITE_INVALID:
			return -1;
			break;
	}
	return 0;
}

const char * ms_crypto_suite_to_string(MSCryptoSuite suite) {
	switch(suite) {
		case MS_CRYPTO_SUITE_INVALID:
			return "<invalid-or-unsupported-suite>";
			break;
		case MS_AES_128_SHA1_80:
			return "AES_CM_128_HMAC_SHA1_80";
			break;
		case MS_AES_128_SHA1_32:
			return "AES_CM_128_HMAC_SHA1_32";
			break;
		case MS_AES_128_SHA1_80_NO_AUTH:
			return "AES_CM_128_HMAC_SHA1_80 UNAUTHENTICATED_SRTP";
			break;
		case MS_AES_128_SHA1_32_NO_AUTH:
			return "AES_CM_128_HMAC_SHA1_32 UNAUTHENTICATED_SRTP";
			break;
		case MS_AES_128_SHA1_80_SRTP_NO_CIPHER:
			return "AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP";
			break;
		case MS_AES_128_SHA1_80_SRTCP_NO_CIPHER:
			return "AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTCP";
			break;
		case MS_AES_128_SHA1_80_NO_CIPHER:
			return "AES_CM_128_HMAC_SHA1_80 UNENCRYPTED_SRTP UNENCRYPTED_SRTCP";
			break;
		case MS_AES_256_SHA1_80:
			return "AES_256_CM_HMAC_SHA1_80";
			break;
		case MS_AES_CM_256_SHA1_80:
			return "AES_CM_256_HMAC_SHA1_80";
			break;
		case MS_AES_256_SHA1_32:
			return "AES_256_CM_HMAC_SHA1_32";
			break;
	}
	return "<invalid-or-unsupported-suite>";
}

static int ms_srtp_add_or_update_stream(srtp_t session, const srtp_policy_t *policy)
{

	err_status_t status = srtp_update_stream(session, policy);
	if (status != 0) {
		status = srtp_add_stream(session, policy);
	}

	return status;
}

static int ms_add_srtp_stream(srtp_t srtp, MSCryptoSuite suite, uint32_t ssrc, const char *key, size_t key_length, bool_t is_send, bool_t is_rtp) {
	srtp_policy_t policy;
	err_status_t err;
	ssrc_t ssrc_conf;

	memset(&policy,0,sizeof(policy));

	/* Init both RTP and RTCP policies, even if this srtp_t is used for one of them.
	 * Indeed the key derivation algorithm that computes the SRTCP auth key depends on parameters 
	 * of the SRTP stream.*/
	if (ms_set_srtp_crypto_policy(suite, &policy.rtp, is_rtp) != 0) {
		return -1;
	}
	if ((int)key_length != policy.rtp.cipher_key_len) {
		ms_error("Key size (%i) doesn't match the selected srtp profile (required %d)", (int)key_length,
					policy.rtp.cipher_key_len);
		return -1;
	}
	// and RTCP stream
	if (ms_set_srtp_crypto_policy(suite, &policy.rtcp, is_rtp) != 0) {
		return -1;
	}
	if ((int)key_length != policy.rtcp.cipher_key_len) {
		ms_error("Key size (%i) doesn't match the selected srtcp profile (required %d)", (int)key_length,
					policy.rtcp.cipher_key_len);
		return -1;
	}


	if (is_send)
		policy.allow_repeat_tx=1; /*necessary for telephone-events*/

	/* When RTP bundle mode is used, the srtp_t is used to encrypt or decrypt several RTP streams (SSRC) at the same time.
	 * This is why we use the "template" mode of libsrtp, using ssrc_any_inbound and ssrc_any_outbound */
	ssrc_conf.type=is_send ? ssrc_any_outbound : ssrc_any_inbound;
	ssrc_conf.value=0;

	policy.ssrc = ssrc_conf;
	policy.key = (uint8_t *)key;
	policy.next = NULL;

	err = ms_srtp_add_or_update_stream(srtp, &policy);
	if (err != err_status_ok) {
		ms_error("Failed to add stream to srtp session (%d)", err);
		return -1;
	}

	return 0;
}

/***********************************************/
/***** EXPORTED FUNCTIONS                  *****/
/***********************************************/
/**** Private to mediastreamer2 functions ****/
/* header declared in voip/private.h */
static int srtp_init_done=0;

int ms_srtp_init(void)
{

	err_status_t st=0;
	ms_message("srtp init");
	if (!srtp_init_done) {
		st=srtp_init();
		if (st==0) {
			srtp_init_done++;
		}else{
			ms_fatal("Couldn't initialize SRTP library: %d.", st);
		}
	}else srtp_init_done++;
	return (int)st;
}

void ms_srtp_shutdown(void){
	srtp_init_done--;
	if (srtp_init_done==0){
		srtp_shutdown();
	}
}

static int ms_media_stream_sessions_set_srtp_key_base(MSMediaStreamSessions *sessions, MSCryptoSuite suite, const char* key, size_t key_length, size_t cipher_key_length, bool_t is_send, bool_t is_rtp){
	MSSrtpStreamContext* stream_ctx;
	uint32_t ssrc;
	int error = -1;

	check_and_create_srtp_context(sessions);
	if (key) {
		ms_message("media_stream_set_%s_%s_key(): key %02x..%02x stream sessions is [%p]",(is_rtp?"srtp":"srtcp"),(is_send?"send":"recv"), (uint8_t)key[0], (uint8_t)key[key_length-1], sessions);
	} else {
		ms_message("media_stream_set_%s_%s_key(): key none stream sessions is [%p]",(is_rtp?"srtp":"srtcp"),(is_send?"send":"recv"), sessions);
	}
	stream_ctx = get_stream_context(sessions,is_send,is_rtp);
	ssrc = is_send? rtp_session_get_send_ssrc(sessions->rtp_session):0/*only relevant for send*/;

	if ((error = ms_media_stream_session_fill_srtp_context(sessions,is_send,is_rtp))) {
		stream_ctx->secured=FALSE;
		return error;
	}

	if ((error = ms_add_srtp_stream(stream_ctx->srtp, suite, ssrc, key, cipher_key_length, is_send, is_rtp))) {
		stream_ctx->secured=FALSE;
		return error;
	}

	stream_ctx->secured=(cipher_key_length!=0);
	return 0;
}

static int ms_media_stream_sessions_set_srtp_key(MSMediaStreamSessions *sessions, MSCryptoSuite suite, const char* key, size_t key_length, bool_t is_send, MSSrtpStreamType stream_type){
	int error = 0;

	// RTP stream
	if (stream_type == MSSRTP_ALL_STREAMS || stream_type == MSSRTP_RTP_STREAM) {
		size_t cipher_key_length = 0;
		if ((suite == MS_AES_128_SHA1_80_NO_CIPHER) || (suite == MS_AES_128_SHA1_80_SRTP_NO_CIPHER)) {
			// SRTP is unencrypted therefore cipher key length is 0
			cipher_key_length = 0;
		} else {
			// SRTP is unencrypted therefore cipher key length is the value provided as argument
			cipher_key_length = key_length;
		}
		error=ms_media_stream_sessions_set_srtp_key_base(sessions,suite,key,key_length,cipher_key_length,is_send,TRUE);
	}

	// RTCP stream
	if (error == 0 && (stream_type == MSSRTP_ALL_STREAMS || stream_type == MSSRTP_RTCP_STREAM)) {
		size_t cipher_key_length = 0;
		if ((suite == MS_AES_128_SHA1_80_NO_CIPHER) || (suite == MS_AES_128_SHA1_80_SRTCP_NO_CIPHER)) {
			// SRTCP is unencrypted therefore cipher key length is 0
			cipher_key_length = 0;
		} else {
			// SRTCP is unencrypted therefore cipher key length is the value provided as argument
			cipher_key_length = key_length;
		}
		error=ms_media_stream_sessions_set_srtp_key_base(sessions,suite,key,key_length,cipher_key_length,is_send,FALSE);
	}
	return error;
}


/**** Public Functions ****/
/* header declared in include/mediastreamer2/ms_srtp.h */
bool_t ms_srtp_supported(void){
	return TRUE;
}


int ms_media_stream_sessions_set_srtp_recv_key_b64(MSMediaStreamSessions *sessions, MSCryptoSuite suite, const char* b64_key){
	int retval;

	size_t key_length = 0;
	char *key = NULL;

	if (b64_key != NULL) {
		/* decode b64 key */
		size_t b64_key_length = strlen(b64_key);
		size_t max_key_length = b64_decode(b64_key, b64_key_length, 0, 0);
		key = (char *) ms_malloc0(max_key_length+1);
		if ((key_length = b64_decode(b64_key, b64_key_length, key, max_key_length)) == 0) {
			ms_error("Error decoding b64 srtp recv key");
			ms_free(key);
			return -1;
		}
	}

	/* pass decoded key to set_recv_key function */
	retval = ms_media_stream_sessions_set_srtp_recv_key(sessions, suite, key, key_length, MSSRTP_ALL_STREAMS);

	ms_free(key);

	return retval;
}

bool_t ms_media_stream_sessions_secured(const MSMediaStreamSessions *sessions,MediaStreamDir dir) {
	if(!sessions->srtp_context)
		return FALSE;

	switch (dir) {
	case MediaStreamSendRecv: return (sessions->srtp_context->send_rtp_context.secured && (!sessions->rtp_session->rtcp.enabled || sessions->srtp_context->send_rtcp_context.secured || sessions->rtp_session->rtcp_mux))
									 && (sessions->srtp_context->recv_rtp_context.secured && (!sessions->rtp_session->rtcp.enabled || sessions->srtp_context->recv_rtcp_context.secured || sessions->rtp_session->rtcp_mux)) ;
	case MediaStreamSendOnly: return sessions->srtp_context->send_rtp_context.secured && (!sessions->rtp_session->rtcp.enabled || sessions->srtp_context->send_rtcp_context.secured || sessions->rtp_session->rtcp_mux);
	case MediaStreamRecvOnly: return sessions->srtp_context->recv_rtp_context.secured && (!sessions->rtp_session->rtcp.enabled || sessions->srtp_context->recv_rtcp_context.secured || sessions->rtp_session->rtcp_mux);
	}

	return FALSE;
}


int ms_media_stream_sessions_set_srtp_recv_key(MSMediaStreamSessions *sessions, MSCryptoSuite suite, const char* key, size_t key_length, MSSrtpStreamType stream_type) {
	return ms_media_stream_sessions_set_srtp_key(sessions,suite,key,key_length,FALSE,stream_type);
}

int ms_media_stream_sessions_set_srtp_send_key_b64(MSMediaStreamSessions *sessions, MSCryptoSuite suite, const char* b64_key){
	int retval;

	size_t key_length = 0;
	char *key = NULL;

	if (b64_key != NULL) {
		/* decode b64 key */
		size_t b64_key_length = strlen(b64_key);
		size_t max_key_length = b64_decode(b64_key, b64_key_length, 0, 0);
		key = (char *) ms_malloc0(max_key_length+1);
		if ((key_length = b64_decode(b64_key, b64_key_length, key, max_key_length)) == 0) {
			ms_error("Error decoding b64 srtp recv key");
			ms_free(key);
			return -1;
		}
	}

	/* pass decoded key to set_send_key function */
	retval = ms_media_stream_sessions_set_srtp_send_key(sessions, suite, key, key_length, MSSRTP_ALL_STREAMS);

	ms_free(key);

	return retval;
}

int ms_media_stream_sessions_set_srtp_send_key(MSMediaStreamSessions *sessions, MSCryptoSuite suite, const char* key, size_t key_length, MSSrtpStreamType stream_type){
	return ms_media_stream_sessions_set_srtp_key(sessions,suite,key,key_length,TRUE,stream_type);
}

int ms_media_stream_sessions_set_encryption_mandatory(MSMediaStreamSessions *sessions, bool_t yesno) {
	/*for now, managing all streams in one time*/
	int err;
	check_and_create_srtp_context(sessions);
	if (yesno) {
		if ((err = ms_media_stream_sessions_fill_srtp_context_all_stream(sessions))) {
			return err;
		}
	}
	sessions->srtp_context->send_rtp_context.mandatory_enabled=yesno;
	sessions->srtp_context->send_rtcp_context.mandatory_enabled=yesno;
	sessions->srtp_context->recv_rtp_context.mandatory_enabled=yesno;
	sessions->srtp_context->recv_rtcp_context.mandatory_enabled=yesno;
	return 0;
}

bool_t ms_media_stream_sessions_get_encryption_mandatory(const MSMediaStreamSessions *sessions) {

	if(!sessions->srtp_context)
		return FALSE;

	return 	   sessions->srtp_context->send_rtp_context.mandatory_enabled
			&& sessions->srtp_context->send_rtcp_context.mandatory_enabled
			&& sessions->srtp_context->recv_rtp_context.mandatory_enabled
			&& sessions->srtp_context->recv_rtcp_context.mandatory_enabled;
}

#else /* HAVE_SRTP */

typedef void* srtp_t;
typedef int err_status_t;

bool_t ms_srtp_supported(void){
	return FALSE;
}

int ms_srtp_init(void) {
	return -1;
}

void ms_srtp_shutdown(void){
}

int ms_media_stream_sessions_set_srtp_recv_key_b64(struct _MSMediaStreamSessions *sessions, MSCryptoSuite suite, const char* b64_key){
	ms_error("Unable to set srtp recv key b64: srtp support disabled in mediastreamer2");
	return -1;
}

int ms_media_stream_sessions_set_srtp_recv_key(MSMediaStreamSessions *sessions, MSCryptoSuite suite, const char* key, size_t key_length, MSSrtpStreamType stream_type){
	ms_error("Unable to set srtp recv key: srtp support disabled in mediastreamer2");
	return -1;
}

int ms_media_stream_sessions_set_srtp_send_key_b64(MSMediaStreamSessions *sessions, MSCryptoSuite suite, const char* b64_key){
	ms_error("Unable to set srtp send key b64: srtp support disabled in mediastreamer2");
	return -1;
}

int ms_media_stream_sessions_set_srtp_send_key(MSMediaStreamSessions *sessions, MSCryptoSuite suite, const char* key, size_t key_length, MSSrtpStreamType stream_type){
	ms_error("Unable to set srtp send key: srtp support disabled in mediastreamer2");
	return -1;
}

int ms_media_stream_session_set_encryption_mandatory(MSMediaStreamSessions *sessions, bool_t yesno) {
	ms_error("Unable to set srtp encryption mandatory mode: srtp support disabled in mediastreamer2");
	return -1;
}

bool_t ms_media_stream_sessions_get_encryption_mandatory(const MSMediaStreamSessions *sessions) {
	ms_error("Unable to get srtp encryption mandatory mode: srtp support disabled in mediastreamer2");
	return -1;
}

bool_t ms_media_stream_sessions_secured(const MSMediaStreamSessions *sessions,MediaStreamDir dir) {
	return FALSE;
}

void ms_srtp_context_delete(MSSrtpCtx* session) {
	ms_error("Unable to delete srtp context [%p]: srtp support disabled in mediastreamer2",session);
}

int ms_media_stream_sessions_set_encryption_mandatory(MSMediaStreamSessions *sessions, bool_t yesno) {
	ms_error("Unable to set encryption_mandatory [%p]: srtp support disabled in mediastreamer2",sessions);
	return -1;
}
#endif

const char * ms_srtp_stream_type_to_string(const MSSrtpStreamType type) {
	switch(type) {
	case MSSRTP_RTP_STREAM: return "MSSRTP_RTP_STREAM";
	case MSSRTP_RTCP_STREAM: return "MSSRTP_RTCP_STREAM";
	case MSSRTP_ALL_STREAMS: return "MSSRTP_ALL_STREAMS";
	}
	return "Unkown srtp tream type";
}

