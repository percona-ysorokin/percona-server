/*	@(#)auth_des.h	2.2 88/07/29 4.0 RPCSRC; from 1.3 88/02/08 SMI */
/*	$FreeBSD: src/include/rpc/auth_des.h,v 1.3 2002/03/23 17:24:55 imp Exp $ */
/*
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 *	from: @(#)auth_des.h 2.2 88/07/29 4.0 RPCSRC
 *	from: @(#)auth_des.h 1.14    94/04/25 SMI
 */

/*
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

/*
 * auth_des.h, Protocol for DES style authentication for RPC
 */

#ifndef _TI_AUTH_DES_
#define _TI_AUTH_DES_

#include <rpc/auth.h>

/*
 * There are two kinds of "names": fullnames and nicknames
 */
enum authdes_namekind {
	ADN_FULLNAME, 
	ADN_NICKNAME
};

/*
 * A fullname contains the network name of the client, 
 * a conversation key and the window
 */
struct authdes_fullname {
  char *name;		/* network name of client, up to MAXNETNAMELEN */
  union des_block key;		/* conversation key */
  /* u_long window;	*/ 
  u_int32_t window;	/* associated window */
};


/*
 * A credential 
 */
struct authdes_cred {
	enum authdes_namekind adc_namekind;
  	struct authdes_fullname adc_fullname;
  /*u_long adc_nickname;*/
 u_int32_t adc_nickname;
}; 



/*
 * A des authentication verifier 
 */
struct authdes_verf {
	union {
		struct timeval adv_ctime;	/* clear time */
	  	des_block adv_xtime;		/* crypt time */
	} adv_time_u;
  /*u_long adv_int_u;*/
  u_int32_t adv_int_u;
};

/*
 * des authentication verifier: client variety
 *
 * adv_timestamp is the current time.
 * adv_winverf is the credential window + 1.
 * Both are encrypted using the conversation key.
 */
#define adv_timestamp	adv_time_u.adv_ctime
#define adv_xtimestamp	adv_time_u.adv_xtime
#define adv_winverf	adv_int_u

/*
 * des authentication verifier: server variety
 *
 * adv_timeverf is the client's timestamp + client's window
 * adv_nickname is the server's nickname for the client.
 * adv_timeverf is encrypted using the conversation key.
 */
#define adv_timeverf	adv_time_u.adv_ctime
#define adv_xtimeverf	adv_time_u.adv_xtime
#define adv_nickname	adv_int_u

#ifdef __cplusplus
extern "C" {
#endif
extern int	rtime(struct sockaddr_in *, struct timeval *,
		    struct timeval *);
extern void	kgetnetname(char *);
#ifdef __cplusplus
}
#endif

#endif /* ndef _TI_AUTH_DES_ */
