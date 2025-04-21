// SPDX-License-Identifier: GPL-2.0-only
/*
 * Crypto acceleration support by Nuclei HSM
 *
 * Copyright (c) 2024, Nuclei Co., Ltd
 *
 */

#include <asm/unaligned.h>
#include <crypto/internal/hash.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include "nuclei_crypto.h"

/*
 * NUCLEI HSM Crypto currenly not support misaligned buffer,
 * but one day in future, maybe misaligned buffer is supported,
 * so NUCLEI_HSM_HASH_SUPPORT_MISALIGNED used to indicate this.
 */
#define HASH_MODE_INIT      0
#define HASH_MODE_UPDATE    1
#define HASH_MODE_FINAL     2
#define HASH_MODE_FINUP     3
#define HASH_MODE_DIGEST    4
/*
 * IC can not process zero message hash,
 * so we put the fixed hash out when met zero message.
 */

static bool nuclei_ahash_need_fallback(struct ahash_request *req)
{
	struct scatterlist *sg;

	sg = req->src;
	while (sg) {
		if (!IS_ALIGNED(sg->offset, sizeof(u32))) {
			return true;
		}
		sg = sg_next(sg);
	}
	return false;
}

static int nuclei_ahash_digest_fb(struct ahash_request *areq)
{
	struct nuclei_ahash_rctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct nuclei_ahash_ctx *tfmctx = crypto_ahash_ctx(tfm);
	struct ahash_alg *alg = crypto_ahash_alg(tfm);
	struct nuclei_crypto_tmp *algt = container_of(alg, struct nuclei_crypto_tmp, alg.hash.base);

	algt->stat_fb++;

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	rctx->fallback_req.nbytes = areq->nbytes;
	rctx->fallback_req.src = areq->src;
	rctx->fallback_req.result = areq->result;

	return crypto_ahash_digest(&rctx->fallback_req);
}

static int zero_message_process(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	int nuclei_digest_size = crypto_ahash_digestsize(tfm);

	switch (nuclei_digest_size) {
	case SHA1_DIGEST_SIZE:
		memcpy(req->result, sha1_zero_message_hash, nuclei_digest_size);
		break;
	case SHA256_DIGEST_SIZE:
		memcpy(req->result, sha256_zero_message_hash, nuclei_digest_size);
		break;
	case MD5_DIGEST_SIZE:
		memcpy(req->result, md5_zero_message_hash, nuclei_digest_size);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int nuclei_ahash_init(struct ahash_request *req)
{
	struct nuclei_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct nuclei_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	rctx->mode = HASH_MODE_INIT;
	rctx->data_times = 0;
	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_init(&rctx->fallback_req);
}

static int nuclei_ahash_update(struct ahash_request *req)
{
#ifndef NUCLEI_HSM_HASH_SUPPORT_MISALIGNED
	struct nuclei_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct nuclei_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.nbytes = req->nbytes;
	rctx->fallback_req.src = req->src;

	return crypto_ahash_update(&rctx->fallback_req);
#else
	struct nuclei_ahash_rctx *rctx = ahash_request_ctx(req);
	struct nuclei_crypto_info *dev;
	struct crypto_engine *engine;

	if (!req->nbytes)
		return 0;

	dev = get_nuclei_crypto();

	rctx->dev = dev;
	rctx->mode = HASH_MODE_UPDATE;
	engine = dev->engine;

	return crypto_transfer_hash_request_to_engine(engine, req);
#endif
}

static int nuclei_ahash_final(struct ahash_request *req)
{
#ifndef NUCLEI_HSM_HASH_SUPPORT_MISALIGNED
	struct nuclei_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct nuclei_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.result = req->result;

	return crypto_ahash_final(&rctx->fallback_req);
#else
	struct nuclei_ahash_rctx *rctx = ahash_request_ctx(req);
	struct nuclei_crypto_info *dev;
	struct crypto_engine *engine;

	if (!req->nbytes)
		return zero_message_process(req);

	dev = get_nuclei_crypto();

	rctx->dev = dev;
	rctx->mode = HASH_MODE_FINAL;
	engine = dev->engine;

	return crypto_transfer_hash_request_to_engine(engine, req);
#endif
}

static int nuclei_ahash_finup(struct ahash_request *req)
{
#ifndef NUCLEI_HSM_HASH_SUPPORT_MISALIGNED
	struct nuclei_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct nuclei_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	rctx->fallback_req.nbytes = req->nbytes;
	rctx->fallback_req.src = req->src;
	rctx->fallback_req.result = req->result;

	return crypto_ahash_finup(&rctx->fallback_req);
#else
	struct nuclei_ahash_rctx *rctx = ahash_request_ctx(req);
	struct nuclei_crypto_info *dev;
	struct crypto_engine *engine;

	if (!req->nbytes)
		return zero_message_process(req);

	dev = get_nuclei_crypto();

	rctx->dev = dev;
	rctx->mode = HASH_MODE_FINUP;
	engine = dev->engine;

	return crypto_transfer_hash_request_to_engine(engine, req);
#endif
}

static int nuclei_ahash_import(struct ahash_request *req, const void *in)
{
	struct nuclei_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct nuclei_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_import(&rctx->fallback_req, in);
}

static int nuclei_ahash_export(struct ahash_request *req, void *out)
{
	struct nuclei_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct nuclei_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_export(&rctx->fallback_req, out);
}

static int nuclei_ahash_digest(struct ahash_request *req)
{
	struct nuclei_ahash_rctx *rctx = ahash_request_ctx(req);
	struct nuclei_crypto_info *dev;
	struct crypto_engine *engine;

	if (nuclei_ahash_need_fallback(req))
		return nuclei_ahash_digest_fb(req);

	if (!req->nbytes)
		return zero_message_process(req);

	dev = get_nuclei_crypto();

	rctx->dev = dev;
	rctx->mode = HASH_MODE_DIGEST;
	engine = dev->engine;

	return crypto_transfer_hash_request_to_engine(engine, req);
}

static u32 nuclei_hsm_hash_algo(struct crypto_ahash *tfm)
{
	u32 digestsize = 0;
	u32 hash_alg = 0;

	digestsize = crypto_ahash_digestsize(tfm);
	switch (digestsize) {
	case SHA1_DIGEST_SIZE:
		hash_alg = SECURE_SERVICE_HASH_SHA1;
		break;
	case SHA224_DIGEST_SIZE:
		hash_alg = SECURE_SERVICE_HASH_SHA224;
		break;
	case SHA256_DIGEST_SIZE:
		hash_alg = SECURE_SERVICE_HASH_SHA256;
		break;
	case SHA384_DIGEST_SIZE:
		hash_alg = SECURE_SERVICE_HASH_SHA384;
		break;
	case SHA512_DIGEST_SIZE:
		hash_alg = SECURE_SERVICE_HASH_SHA512;
		break;
	case MD5_DIGEST_SIZE:
		hash_alg = SECURE_SERVICE_HASH_MD5;
		break;
	}
	return hash_alg;
}



static int nuclei_ahash_prepare(struct crypto_engine *engine, void *breq)
{
	struct ahash_request *areq = container_of(breq, struct ahash_request, base);
	struct nuclei_ahash_rctx *rctx = ahash_request_ctx(areq);
	struct nuclei_crypto_info *rkc = rctx->dev;
	int ret;

	ret = dma_map_sg(rkc->dev, areq->src, sg_nents(areq->src), DMA_TO_DEVICE);
	if (ret <= 0)
		return -EINVAL;

	rctx->nrsg = ret;

	return 0;
}

static inline void nuclei_ahash_cmd_setup(mailbox_hash_cmd_in_token *hash_cmd,
	u32 update_mode, u64 addr, u32 len, u32 alg, u32 mode, u8 *key, u32 keylen)
{
	memset(&hash_cmd->hash, 0, sizeof(hash_in_token_t));
	hash_cmd->hash.header.opcode = SECURE_SERVICE_OPCODE_HASH;
	hash_cmd->hash.header.TokenID = (u8)CRYPTO_ALG_TYPE_AHASH;
	hash_cmd->hash.input_data_addr_low = (u32)(addr & 0xFFFFFFFF);
	hash_cmd->hash.input_data_addr_hig = (u32)(addr >> 32);
	hash_cmd->hash.input_data_length = hash_cmd->hash.length = len;
	hash_cmd->hash.cmd_cfg.algo = alg;
	hash_cmd->hash.cmd_cfg.mode = mode;
	hash_cmd->hash.cmd_cfg.in_ctrl = update_mode;
	if (mode == SECURE_SERVICE_HMAC_MODE){
		hash_cmd->hash.cmd_cfg.keyLen = keylen;
		memcpy(hash_cmd->key, key, keylen);
	}
	hash_cmd->hash.key_data_addr_low = *(uint32_t *)(&hash_cmd->hash.cmd_cfg);
}

static void nuclei_calc_key_hash(const u8 *hashname, const u8 *data, size_t len, u8 *digest) {
    struct crypto_ahash *tfm;
    struct ahash_request *req;
    struct scatterlist sg;
    int ret;

    tfm = crypto_alloc_ahash(hashname, 0, CRYPTO_ALG_ASYNC);
    if (IS_ERR(tfm)) {
        pr_err("Failed to load sha256-Err%d\n",PTR_ERR(tfm));
        return;
    }

    req = ahash_request_alloc(tfm, GFP_KERNEL);
    if (!req) {
        crypto_free_ahash(tfm);
        return;
    }

    sg_init_one(&sg, data, len);
    ahash_request_set_crypt(req, &sg, digest, len);

    ret = crypto_ahash_digest(req);
    if (ret != 0) {
        pr_err("Hash computation failed: %d\n", ret);
    }

    ahash_request_free(req);
    crypto_free_ahash(tfm);
}

static int nuclei_ahash_setkey(struct crypto_ahash *tfm,
			     const u8 *key, unsigned int keylen)
{
	struct nuclei_ahash_ctx *ctx = crypto_ahash_ctx(tfm);
	u32 blocksize = crypto_tfm_alg_blocksize(&tfm->base);
	u32 digestsize = crypto_ahash_digestsize(tfm);
	crypto_ahash_setkey(ctx->fallback_tfm, key, keylen);
	if (keylen > blocksize) {
		/* hash key first */
		u8 *result = kmalloc(digestsize, GFP_KERNEL);

		if (digestsize == MD5_DIGEST_SIZE)
			nuclei_calc_key_hash("md5", key, keylen, result);
		else if (digestsize == SHA1_DIGEST_SIZE)
			nuclei_calc_key_hash("sha1", key, keylen, result);
		else if (digestsize == SHA256_DIGEST_SIZE)
			/*
			 * maybe here need to check again,
			 * because sm3 digest size is also 32byte
			 */
			nuclei_calc_key_hash("sha256", key, keylen, result);
		ctx->keylen = digestsize;
		memcpy(ctx->key, result, digestsize);
		kfree(result);
	} else {
		memcpy(ctx->key, key, keylen);
		ctx->keylen = keylen;
	}

	return 0;
}

void nuclei_ahash_done(int err, void *_req)
{
	#define MBOX_DIGEST_DATA_OFFSET 8
	struct nuclei_mbox_msg *mssg = (struct nuclei_mbox_msg *)_req;
	struct ahash_request *areq = (struct ahash_request *)mssg->ctx;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct nuclei_ahash_rctx *rctx = ahash_request_ctx(areq);	
	struct nuclei_crypto_info *rkc = rctx->dev;

	complete(&rkc->complete);
	if (rctx->hash_cmd_desc.hash.cmd_cfg.in_ctrl == SECURE_SERVICE_IN_ALL ||\
		rctx->hash_cmd_desc.hash.cmd_cfg.in_ctrl == SECURE_SERVICE_IN_END) {
		if (!err) {
			int i;
			int digest_size = crypto_ahash_digestsize(tfm);
			u32 *dst = (u32 *)areq->result;
			u32 *src = (u32 *)((u8 *)mssg->data + MBOX_DIGEST_DATA_OFFSET);

			for(i = 0; i < digest_size/4; i++)
			 	dst[i] = be32_to_cpu(src[i]);
		}
	}

	crypto_finalize_hash_request(rkc->engine, areq, err);
}

static int nuclei_ahash_run(struct crypto_engine *engine, void *breq)
{
	struct ahash_request *areq = container_of(breq, struct ahash_request, base);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct nuclei_ahash_rctx *rctx = ahash_request_ctx(areq);
	struct ahash_alg *alg = crypto_ahash_alg(tfm);
	struct nuclei_crypto_tmp *algt = container_of(alg, struct nuclei_crypto_tmp, alg.hash.base);
	struct scatterlist *sg = areq->src;
	struct nuclei_crypto_info *rkc = rctx->dev;
	struct nuclei_ahash_ctx *ctx = crypto_ahash_ctx(tfm);
	int err;
	int i;
	u32 hash_alg;
	u32 hash_hmac;
	struct nuclei_mbox_msg mb_msg;
	u32 update_mode;

	err = nuclei_ahash_prepare(engine, breq);
	if (err)
		goto theend;

	hash_alg = nuclei_hsm_hash_algo(tfm);
	if (hash_alg == 0) {
		err = -1;
		goto theend;
	}
	hash_hmac = algt->alg.hash.base.setkey ? SECURE_SERVICE_HMAC_MODE:
		SECURE_SERVICE_HASH_MODE;
	if (rctx->mode == HASH_MODE_DIGEST) {
		if (rctx->nrsg == 1)
			update_mode = SECURE_SERVICE_IN_ALL;
		else
			update_mode = SECURE_SERVICE_IN_INIT;
		for (i = 0; i < rctx->nrsg; ) {
			nuclei_ahash_cmd_setup(&rctx->hash_cmd_desc, update_mode,
			sg_dma_address(sg), sg_dma_len(sg), hash_alg, hash_hmac, ctx->key, ctx->keylen);
			reinit_completion(&rkc->complete);
			mb_msg.data = &rctx->hash_cmd_desc;
			mb_msg.ctx = areq;
			err = mbox_send_message(rkc->mbox[0], &mb_msg);
			if (err < 0)
				goto theend;
			wait_for_completion_interruptible_timeout(&rkc->complete,
								  msecs_to_jiffies(100000));
			sg = sg_next(sg);
			i++;
			update_mode = SECURE_SERVICE_IN_UPDATE;
			if (i == (rctx->nrsg -1))
				update_mode = SECURE_SERVICE_IN_END;
		}
	}
#ifdef NUCLEI_HSM_HASH_SUPPORT_MISALIGNED
	else if (rctx->mode == HASH_MODE_UPDATE) {
		if (rctx->data_times==0)
			update_mode = SECURE_SERVICE_IN_INIT;
		else
			update_mode = SECURE_SERVICE_IN_UPDATE;
		nuclei_ahash_cmd_setup(&rctx->hash_cmd_desc, update_mode,
		sg_dma_address(sg), sg_dma_len(sg), hash_alg, hash_hmac, ctx->key, ctx->keylen);

		reinit_completion(&rkc->complete);
		mb_msg.data = &rctx->hash_cmd_desc;
		mb_msg.ctx = areq;
		err = mbox_send_message(rkc->mbox[0], &mb_msg);
		if (err < 0)
			goto theend;
		wait_for_completion_interruptible_timeout(&rkc->complete,
							  msecs_to_jiffies(100000));
		rctx->data_times++;
	} else if (rctx->mode == HASH_MODE_FINAL) {
		nuclei_ahash_cmd_setup(&rctx->hash_cmd_desc, SECURE_SERVICE_IN_END,
		NULL, 0, hash_alg, hash_hmac, ctx->key, ctx->keylen);
		reinit_completion(&rkc->complete);
		mb_msg.data = &rctx->hash_cmd_desc;
		mb_msg.ctx = areq;
		err = mbox_send_message(rkc->mbox[0], &mb_msg);
		if (err < 0)
			goto theend;
		wait_for_completion_interruptible_timeout(&rkc->complete,
							  msecs_to_jiffies(100000));
	} else if (rctx->mode == HASH_MODE_FINUP) {
		if (rctx->data_times == 0) {
			if (rctx->nrsg == 1)
				update_mode = SECURE_SERVICE_IN_ALL;
			else
				update_mode = SECURE_SERVICE_IN_INIT;
		} else {
			if (rctx->nrsg == 1)
				update_mode = SECURE_SERVICE_IN_END;
			else
				update_mode = SECURE_SERVICE_IN_UPDATE;
		}

		for (i = 0; i < rctx->nrsg; ) {
			nuclei_ahash_cmd_setup(&rctx->hash_cmd_desc, update_mode,
			sg_dma_address(sg), sg_dma_len(sg), hash_alg, hash_hmac, ctx->key, ctx->keylen);
			reinit_completion(&rkc->complete);
			mb_msg.data = &rctx->hash_cmd_desc;
			mb_msg.ctx = areq;
			err = mbox_send_message(rkc->mbox[0], &mb_msg);
			if (err < 0)
				goto theend;
			wait_for_completion_interruptible_timeout(&rkc->complete,
								  msecs_to_jiffies(100000));
			rctx->data_times++;
			sg = sg_next(sg);
			i++;
			update_mode = SECURE_SERVICE_IN_UPDATE;
			if (i == (rctx->nrsg -1))
				update_mode = SECURE_SERVICE_IN_END;
		}
	}
#endif

	return 0;

theend:
	crypto_finalize_hash_request(rkc->engine, areq, err);
	
	return err;
}

static int nuclei_ahash_init_tfm(struct crypto_ahash *tfm)
{
	struct nuclei_ahash_ctx *tctx = crypto_ahash_ctx(tfm);
	const char *alg_name = crypto_ahash_alg_name(tfm);
	struct ahash_alg *alg = crypto_ahash_alg(tfm);
	struct nuclei_crypto_tmp *algt = container_of(alg, struct nuclei_crypto_tmp, alg.hash.base);

	/* for fallback */
	tctx->fallback_tfm = crypto_alloc_ahash(alg_name, 0,
						CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(tctx->fallback_tfm)) {
		dev_err(algt->dev->dev, "Could not load fallback driver.\n");
		return PTR_ERR(tctx->fallback_tfm);
	}

	crypto_ahash_set_reqsize(tfm,
				 sizeof(struct nuclei_ahash_rctx) +
				 crypto_ahash_reqsize(tctx->fallback_tfm));

	return 0;
}

static void nuclei_ahash_exit_tfm(struct crypto_ahash *tfm)
{
	struct nuclei_ahash_ctx *tctx = crypto_ahash_ctx(tfm);

	crypto_free_ahash(tctx->fallback_tfm);
}

#define NUCLEI_AHASH_HASH_ALG(alg_name, digest_size, block_size, state_struct) \
struct nuclei_crypto_tmp nuclei_ahash_hash_##alg_name = {\
	.type = CRYPTO_ALG_TYPE_AHASH,\
	.alg.hash.base = {\
		.init = nuclei_ahash_init,\
		.update = nuclei_ahash_update,\
		.final = nuclei_ahash_final,\
		.finup = nuclei_ahash_finup,\
		.export = nuclei_ahash_export,\
		.import = nuclei_ahash_import,\
		.digest = nuclei_ahash_digest,\
		.init_tfm = nuclei_ahash_init_tfm,\
		.exit_tfm = nuclei_ahash_exit_tfm,\
		.halg = {\
			 .digestsize = digest_size,\
			 .statesize = sizeof(struct state_struct),\
			 .base = {\
				  .cra_name = #alg_name,\
				  .cra_driver_name = "nuclei-"#alg_name,\
				  .cra_priority = 300,\
				  .cra_flags = (CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK),\
				  .cra_blocksize = block_size,\
				  .cra_ctxsize = sizeof(struct nuclei_ahash_ctx),\
				  .cra_alignmask = 3,\
				  .cra_module = THIS_MODULE,\
			 }\
		}\
	},\
	.alg.hash.op = {\
		.do_one_request = nuclei_ahash_run,\
	},\
}

#define NUCLEI_AHASH_HMAC_ALG(alg_name, digest_size, block_size, state_struct, setkey_func) \
struct nuclei_crypto_tmp nuclei_ahash_hmac_##alg_name = {\
	.type = CRYPTO_ALG_TYPE_AHASH,\
	.alg.hash.base = {\
		.init = nuclei_ahash_init,\
		.update = nuclei_ahash_update,\
		.final = nuclei_ahash_final,\
		.finup = nuclei_ahash_finup,\
		.export = nuclei_ahash_export,\
		.import = nuclei_ahash_import,\
		.setkey = setkey_func,\
		.digest = nuclei_ahash_digest,\
		.init_tfm = nuclei_ahash_init_tfm,\
		.exit_tfm = nuclei_ahash_exit_tfm,\
		.halg = {\
			 .digestsize = digest_size,\
			 .statesize = sizeof(struct state_struct),\
			 .base = {\
				  .cra_name = "hmac("#alg_name")",\
				  .cra_driver_name = "nuclei-hmac("#alg_name")",\
				  .cra_priority = 300,\
				  .cra_flags = (CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK),\
				  .cra_blocksize = block_size,\
				  .cra_ctxsize = sizeof(struct nuclei_ahash_ctx),\
				  .cra_alignmask = 3,\
				  .cra_module = THIS_MODULE,\
			 }\
		}\
	},\
	.alg.hash.op = {\
		.do_one_request = nuclei_ahash_run,\
	},\
}

NUCLEI_AHASH_HASH_ALG(sha1, SHA1_DIGEST_SIZE, SHA1_BLOCK_SIZE, sha1_state);
NUCLEI_AHASH_HASH_ALG(sha256, SHA256_DIGEST_SIZE, SHA256_BLOCK_SIZE, sha256_state);
NUCLEI_AHASH_HASH_ALG(md5, MD5_DIGEST_SIZE, SHA1_BLOCK_SIZE, md5_state);

NUCLEI_AHASH_HMAC_ALG(sha1, SHA1_DIGEST_SIZE, SHA1_BLOCK_SIZE, sha1_state, nuclei_ahash_setkey);
NUCLEI_AHASH_HMAC_ALG(sha256, SHA256_DIGEST_SIZE, SHA256_BLOCK_SIZE, sha256_state, nuclei_ahash_setkey);
NUCLEI_AHASH_HMAC_ALG(md5, MD5_DIGEST_SIZE, SHA1_BLOCK_SIZE, md5_state, nuclei_ahash_setkey);
