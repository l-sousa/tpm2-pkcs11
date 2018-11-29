/* SPDX-License-Identifier: BSD-2 */
/*
 * Copyright (c) 2018, Intel Corporation
 * All rights reserved.
 */
#include <stdint.h>

#include "checks.h"
#include "digest.h"
#include "session.h"
#include "session_ctx.h"
#include "token.h"
#include "tpm.h"

/*
 * TODO The digest code could be refactored to be shared between this and sign.
 */

typedef struct digest_op_data digest_op_data;
struct digest_op_data {
    tobject *tobj;
    CK_MECHANISM_TYPE mode;
    uint32_t sequence_handle;
};

CK_RV digest_init (CK_SESSION_HANDLE session, CK_MECHANISM *mechanism) {

    check_is_init();
    check_pointer(mechanism);

    CK_RV rv = CKR_GENERAL_ERROR;

    session_ctx *ctx = NULL;
    rv = session_lookup(session, &ctx);
    if (rv != CKR_OK) {
        return rv;
    }

    bool session_state_ok = session_ctx_user_state_ok(ctx);
    if (!session_state_ok) {
        rv = CKR_USER_NOT_LOGGED_IN;
        goto out;
    }

    token *tok = session_ctx_get_tok(ctx);
    bool is_active = token_opdata_is_active(tok);
    if (is_active) {
        rv = CKR_OPERATION_ACTIVE;
        goto out;
    }

    /*
     * Start a hashing sequence with the TPM
     */
    tpm_ctx *tpm = tok->tctx;

    uint32_t sequence_handle;
    rv = tpm_hash_init(tpm, mechanism->mechanism, &sequence_handle);
    if (rv != CKR_OK) {
        goto out;
    }

    digest_op_data *opdata = calloc(1, sizeof(*opdata));
    if (!opdata) {
        rv = CKR_HOST_MEMORY;
        goto out;
    }

    opdata->mode = mechanism->mechanism;
    opdata->sequence_handle = sequence_handle;

    /* Store everything for later */
    token_opdata_set(tok, operation_digest, opdata);

    rv = CKR_OK;

out:
    session_ctx_unlock(ctx);

    return rv;
}

CK_RV digest_update (CK_SESSION_HANDLE session, unsigned char *part, unsigned long part_len) {

    check_is_init();
    check_pointer(part);

    CK_RV rv = CKR_GENERAL_ERROR;

    session_ctx *ctx = NULL;
    rv = session_lookup(session, &ctx);
    if (rv != CKR_OK) {
        return rv;
    }

    bool session_state_ok = session_ctx_user_state_ok(ctx);
    if (!session_state_ok) {
        rv = CKR_USER_NOT_LOGGED_IN;
        goto out;
    }

    digest_op_data *opdata = NULL;
    token *tok = session_ctx_get_tok(ctx);
    rv = token_opdata_get(tok, operation_digest, &opdata);
    if (rv != CKR_OK) {
        goto out;
    }

    tpm_ctx *tpm = tok->tctx;

    rv = tpm_hash_update(tpm, opdata->sequence_handle, part, part_len);
    if (rv != CKR_OK) {
        goto out;
    }

    rv = CKR_OK;

out:
    session_ctx_unlock(ctx);

    return rv;
}

CK_RV digest_final (CK_SESSION_HANDLE session, unsigned char *digest, unsigned long *digest_len) {

    check_is_init();
    check_pointer(digest);
    check_pointer(digest_len);

    CK_RV rv = CKR_GENERAL_ERROR;

    session_ctx *ctx = NULL;
    rv = session_lookup(session, &ctx);
    if (rv != CKR_OK) {
        return rv;
    }

    bool session_state_ok = session_ctx_user_state_ok(ctx);
    if (!session_state_ok) {
        rv = CKR_USER_NOT_LOGGED_IN;
        goto out;
    }

    digest_op_data *opdata = NULL;
    token *tok = session_ctx_get_tok(ctx);
    rv = token_opdata_get(tok, operation_digest, &opdata);
    if (rv != CKR_OK) {
        goto out;
    }

    tpm_ctx *tpm = tok->tctx;

    rv = tpm_hash_final(tpm, opdata->sequence_handle, digest, digest_len);
    if (rv != CKR_OK) {
        goto out;
    }

    token_opdata_clear(tok);

    free(opdata);

out:
    session_ctx_unlock(ctx);

    return rv;
}

CK_RV digest_oneshot (CK_SESSION_HANDLE session, unsigned char *data, unsigned long data_len, unsigned char *digest, unsigned long *digest_len) {

    CK_RV rv = digest_update(session, data, data_len);
    if (rv != CKR_OK) {
        return rv;
    }

    return digest_final(session, digest, digest_len);
}
