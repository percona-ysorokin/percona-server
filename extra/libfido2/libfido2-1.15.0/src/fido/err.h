/*
 * Copyright (c) 2018 Yubico AB. All rights reserved.
 * SPDX-License-Identifier: BSD-2-Clause
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _FIDO_ERR_H
#define _FIDO_ERR_H

#define FIDO_ERR_SUCCESS		0x00
#define FIDO_ERR_INVALID_COMMAND	0x01
#define FIDO_ERR_INVALID_PARAMETER	0x02
#define FIDO_ERR_INVALID_LENGTH		0x03
#define FIDO_ERR_INVALID_SEQ		0x04
#define FIDO_ERR_TIMEOUT		0x05
#define FIDO_ERR_CHANNEL_BUSY		0x06
#define FIDO_ERR_LOCK_REQUIRED		0x0a
#define FIDO_ERR_INVALID_CHANNEL	0x0b
#define FIDO_ERR_CBOR_UNEXPECTED_TYPE	0x11
#define FIDO_ERR_INVALID_CBOR		0x12
#define FIDO_ERR_MISSING_PARAMETER	0x14
#define FIDO_ERR_LIMIT_EXCEEDED		0x15
#define FIDO_ERR_UNSUPPORTED_EXTENSION	0x16
#define FIDO_ERR_FP_DATABASE_FULL	0x17
#define FIDO_ERR_LARGEBLOB_STORAGE_FULL	0x18
#define FIDO_ERR_CREDENTIAL_EXCLUDED	0x19
#define FIDO_ERR_PROCESSING		0x21
#define FIDO_ERR_INVALID_CREDENTIAL	0x22
#define FIDO_ERR_USER_ACTION_PENDING	0x23
#define FIDO_ERR_OPERATION_PENDING	0x24
#define FIDO_ERR_NO_OPERATIONS		0x25
#define FIDO_ERR_UNSUPPORTED_ALGORITHM	0x26
#define FIDO_ERR_OPERATION_DENIED	0x27
#define FIDO_ERR_KEY_STORE_FULL		0x28
#define FIDO_ERR_NOT_BUSY		0x29
#define FIDO_ERR_NO_OPERATION_PENDING	0x2a
#define FIDO_ERR_UNSUPPORTED_OPTION	0x2b
#define FIDO_ERR_INVALID_OPTION		0x2c
#define FIDO_ERR_KEEPALIVE_CANCEL	0x2d
#define FIDO_ERR_NO_CREDENTIALS		0x2e
#define FIDO_ERR_USER_ACTION_TIMEOUT	0x2f
#define FIDO_ERR_NOT_ALLOWED		0x30
#define FIDO_ERR_PIN_INVALID		0x31
#define FIDO_ERR_PIN_BLOCKED		0x32
#define FIDO_ERR_PIN_AUTH_INVALID	0x33
#define FIDO_ERR_PIN_AUTH_BLOCKED	0x34
#define FIDO_ERR_PIN_NOT_SET		0x35
#define FIDO_ERR_PIN_REQUIRED		0x36
#define FIDO_ERR_PIN_POLICY_VIOLATION	0x37
#define FIDO_ERR_PIN_TOKEN_EXPIRED	0x38
#define FIDO_ERR_REQUEST_TOO_LARGE	0x39
#define FIDO_ERR_ACTION_TIMEOUT		0x3a
#define FIDO_ERR_UP_REQUIRED		0x3b
#define FIDO_ERR_UV_BLOCKED		0x3c
#define FIDO_ERR_UV_INVALID		0x3f
#define FIDO_ERR_UNAUTHORIZED_PERM	0x40
#define FIDO_ERR_ERR_OTHER		0x7f
#define FIDO_ERR_SPEC_LAST		0xdf

/* defined internally */
#define FIDO_OK				FIDO_ERR_SUCCESS
#define FIDO_ERR_TX			-1
#define FIDO_ERR_RX			-2
#define FIDO_ERR_RX_NOT_CBOR		-3
#define FIDO_ERR_RX_INVALID_CBOR	-4
#define FIDO_ERR_INVALID_PARAM		-5
#define FIDO_ERR_INVALID_SIG		-6
#define FIDO_ERR_INVALID_ARGUMENT	-7
#define FIDO_ERR_USER_PRESENCE_REQUIRED	-8
#define FIDO_ERR_INTERNAL		-9
#define FIDO_ERR_NOTFOUND		-10
#define FIDO_ERR_COMPRESS		-11

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

const char *fido_strerr(int);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* _FIDO_ERR_H */
