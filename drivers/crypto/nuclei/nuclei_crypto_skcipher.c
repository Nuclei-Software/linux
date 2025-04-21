// SPDX-License-Identifier: GPL-2.0-only
/*
 * Crypto acceleration support for Nuclei HSM Crypto
 *
 * Copyright (c) 2024, Nuclei Co., Ltd
 *
 */

#include <crypto/engine.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include "nuclei_crypto.h"

static int nuclei_cipher_need_fallback(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct nuclei_crypto_tmp *algt = container_of(alg, struct nuclei_crypto_tmp, alg.skcipher.base);
	struct scatterlist *sgs, *sgd;
	unsigned int stodo, dtodo, len;
	unsigned int bs = crypto_skcipher_blocksize(tfm);

	if (!req->cryptlen)
		return true;

	len = req->cryptlen;
	sgs = req->src;
	sgd = req->dst;
	while (sgs && sgd) {
		if (!IS_ALIGNED(sgs->offset, sizeof(u32))) {
			algt->stat_fb_align++;
			return true;
		}
		if (!IS_ALIGNED(sgd->offset, sizeof(u32))) {
			algt->stat_fb_align++;
			return true;
		}
		stodo = min(len, sgs->length);
		if (stodo % bs) {
			algt->stat_fb_len++;
			return true;
		}
		dtodo = min(len, sgd->length);
		if (dtodo % bs) {
			algt->stat_fb_len++;
			return true;
		}
		if (stodo != dtodo) {
			algt->stat_fb_sgdiff++;
			return true;
		}
		len -= stodo;
		sgs = sg_next(sgs);
		sgd = sg_next(sgd);
	}
	return false;
}

static int nuclei_cipher_fallback(struct skcipher_request *areq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct nuclei_cipher_ctx *op = crypto_skcipher_ctx(tfm);
	struct nuclei_cipher_rctx *rctx = skcipher_request_ctx(areq);
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct nuclei_crypto_tmp *algt = container_of(alg, struct nuclei_crypto_tmp, alg.skcipher.base);
	int err;

	algt->stat_fb++;

	skcipher_request_set_tfm(&rctx->fallback_req, op->fallback_tfm);
	skcipher_request_set_callback(&rctx->fallback_req, areq->base.flags,
				      areq->base.complete, areq->base.data);
	skcipher_request_set_crypt(&rctx->fallback_req, areq->src, areq->dst,
				   areq->cryptlen, areq->iv);
	if (rctx->cipher_cmd_desc.cryp.cmd_cfg.encryp)
		err = crypto_skcipher_decrypt(&rctx->fallback_req);
	else
		err = crypto_skcipher_encrypt(&rctx->fallback_req);
	return err;
}

static int nuclei_cipher_handle_req(struct skcipher_request *req)
{
	struct nuclei_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct nuclei_crypto_info *rkc;
	struct crypto_engine *engine;

	if (nuclei_cipher_need_fallback(req))
		return nuclei_cipher_fallback(req);

	rkc = get_nuclei_crypto();

	engine = rkc->engine;
	rctx->dev = rkc;

	return crypto_transfer_skcipher_request_to_engine(engine, req);
}

static int nuclei_cipher_setkey(struct crypto_skcipher *cipher,
			 const u8 *key, unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(cipher);
	struct nuclei_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256)
		return -EINVAL;
	ctx->keylen = keylen;
	memcpy(ctx->key, key, keylen);

	return crypto_skcipher_setkey(ctx->fallback_tfm, key, keylen);
}

static inline int nuclei_cipher_crypt(struct skcipher_request *req,
	u32 algo, u32 mode, u32 encrypt)
{
	struct nuclei_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct nuclei_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);

	memzero_explicit(&rctx->cipher_cmd_desc.cryp, sizeof(cryp_in_token_t));

	rctx->cipher_cmd_desc.cryp.cmd_cfg.encryp = encrypt;
	rctx->cipher_cmd_desc.cryp.cmd_cfg.algo = algo;
	rctx->cipher_cmd_desc.cryp.cmd_cfg.mode = mode;
	if (ctx->keylen == AES_KEYSIZE_128)
		rctx->cipher_cmd_desc.cryp.cmd_cfg.key_length =
		SECURE_SERVICE_CRYP_KEY_128BITS;
	else if (ctx->keylen == AES_KEYSIZE_192)
		rctx->cipher_cmd_desc.cryp.cmd_cfg.key_length =
		SECURE_SERVICE_CRYP_KEY_192BITS;
	else //if (ctx->keylen == AES_KEYSIZE_256)
		rctx->cipher_cmd_desc.cryp.cmd_cfg.key_length =
		SECURE_SERVICE_CRYP_KEY_256BITS;

	/* key source sel, hwkey needs to be resolved */
	rctx->cipher_cmd_desc.cryp.cmd_cfg.key_sel =
		SECURE_SERVICE_CRYP_KEY_SEL_CFG;
	rctx->cipher_cmd_desc.cryp.cmd_cfg.in_ctrl = 
		SECURE_SERVICE_IN_ALL;

	return nuclei_cipher_handle_req(req);
}

static int nuclei_aes_ecb_encrypt(struct skcipher_request *req)
{
	return nuclei_cipher_crypt(req, SECURE_SERVICE_CRYP_AES,
		SECURE_SERVICE_CRYP_ECB, SECURE_SERVICE_CRYP_ENCRYPT);
}

static int nuclei_aes_ecb_decrypt(struct skcipher_request *req)
{
	return nuclei_cipher_crypt(req, SECURE_SERVICE_CRYP_AES,
		SECURE_SERVICE_CRYP_ECB, SECURE_SERVICE_CRYP_DECRYPT);
}

static int nuclei_aes_cbc_encrypt(struct skcipher_request *req)
{
	return nuclei_cipher_crypt(req, SECURE_SERVICE_CRYP_AES,
		SECURE_SERVICE_CRYP_CBC, SECURE_SERVICE_CRYP_ENCRYPT);
}

static int nuclei_aes_cbc_decrypt(struct skcipher_request *req)
{
	return nuclei_cipher_crypt(req, SECURE_SERVICE_CRYP_AES,
		SECURE_SERVICE_CRYP_CBC, SECURE_SERVICE_CRYP_DECRYPT);
}

static int nuclei_aes_ctr_encrypt(struct skcipher_request *req)
{
	return nuclei_cipher_crypt(req, SECURE_SERVICE_CRYP_AES,
		SECURE_SERVICE_CRYP_CTR, SECURE_SERVICE_CRYP_ENCRYPT);
}

static int nuclei_aes_ctr_decrypt(struct skcipher_request *req)
{
	return nuclei_cipher_crypt(req, SECURE_SERVICE_CRYP_AES,
		SECURE_SERVICE_CRYP_CTR, SECURE_SERVICE_CRYP_DECRYPT);
}

static int nuclei_sm4_ecb_encrypt(struct skcipher_request *req)
{
	return nuclei_cipher_crypt(req, SECURE_SERVICE_CRYP_SM4,
		SECURE_SERVICE_CRYP_ECB, SECURE_SERVICE_CRYP_ENCRYPT);
}

static int nuclei_sm4_ecb_decrypt(struct skcipher_request *req)
{
	return nuclei_cipher_crypt(req, SECURE_SERVICE_CRYP_SM4,
		SECURE_SERVICE_CRYP_ECB, SECURE_SERVICE_CRYP_DECRYPT);
}

static int nuclei_sm4_cbc_encrypt(struct skcipher_request *req)
{
	return nuclei_cipher_crypt(req, SECURE_SERVICE_CRYP_SM4,
		SECURE_SERVICE_CRYP_CBC, SECURE_SERVICE_CRYP_ENCRYPT);
}

static int nuclei_sm4_cbc_decrypt(struct skcipher_request *req)
{
	return nuclei_cipher_crypt(req, SECURE_SERVICE_CRYP_SM4,
		SECURE_SERVICE_CRYP_CBC, SECURE_SERVICE_CRYP_DECRYPT);
}

void nuclei_skcipher_done(int err, void *_req)
{
	struct nuclei_mbox_msg *mssg = (struct nuclei_mbox_msg *)_req;
	struct skcipher_request *req = (struct skcipher_request *)mssg->ctx;
	struct nuclei_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	u32 ivsize = crypto_skcipher_ivsize(tfm);
	struct nuclei_crypto_info *rkc = rctx->dev;
	u32 nbytes;

	dma_unmap_single(rkc->dev, rctx->buf_dma, rctx->buflen,
			 DMA_FROM_DEVICE);

	if (unlikely(err)) {
		dev_dbg(rkc->dev, "%s: %s request failed: %d\n", __func__,
			crypto_tfm_alg_name(crypto_skcipher_tfm(tfm)), err);
		goto out_free_buf;
	}
	/* copy result from linear buffer */
	nbytes = sg_copy_from_buffer(req->dst, sg_nents(req->dst), rctx->buf,
				     req->cryptlen);
	if (unlikely(nbytes != req->cryptlen)) {
		err = -ENODATA;
		goto out_free_buf;
	}
	memcpy(req->iv, ((mailbox_cryp_cmd_in_token *)mssg->data)->iv, ivsize);

out_free_buf:
	kfree(rctx->buf);

	crypto_finalize_skcipher_request(rkc->engine, req, err);
}

static int nuclei_cipher_run(struct crypto_engine *engine, void *async_req)
{
	struct skcipher_request *areq = container_of(async_req, struct skcipher_request, base);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct nuclei_cipher_rctx *rctx = skcipher_request_ctx(areq);
	struct nuclei_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err = 0,nbytes;
	int ivsize;
	struct nuclei_mbox_msg mb_msg;

	struct nuclei_crypto_info *rkc = rctx->dev;
	unsigned int blocksize = crypto_skcipher_blocksize(tfm);

	ivsize = crypto_skcipher_ivsize(tfm);

	if (!areq->cryptlen)
		return 0;

	rctx->buflen = roundup(areq->cryptlen, blocksize);
	rctx->buf = kzalloc(rctx->buflen, GFP_KERNEL);
	if (IS_ERR_OR_NULL(rctx->buf)) {
		rctx->buflen = 0;
		return -ENOMEM;
	}
	/* copy source to linear buffer */
	nbytes = sg_copy_to_buffer(areq->src, sg_nents(areq->src),
				   rctx->buf, areq->cryptlen);
	if (nbytes != areq->cryptlen) {
		err = -ENODATA;
		goto err_free_buf;
	}

	rctx->buf_dma = dma_map_single(rkc->dev, rctx->buf, rctx->buflen,
				       DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(rkc->dev, rctx->buf_dma))) {
		err = -ENOMEM;
		goto err_free_buf;
	}

	rctx->cipher_cmd_desc.cryp.header.opcode = SECURE_SERVICE_OPCODE_CRYP;
	/* save crypto type in reserved field for respone */
	rctx->cipher_cmd_desc.cryp.header.TokenID = CRYPTO_ALG_TYPE_SKCIPHER;
	rctx->cipher_cmd_desc.cryp.input_data_addr_low = rctx->buf_dma & 0xFFFFFFFF;
	rctx->cipher_cmd_desc.cryp.input_data_addr_hig = (rctx->buf_dma >> 32) & 0xFFFFFFFF;
	rctx->cipher_cmd_desc.cryp.length = rctx->buflen;
	rctx->cipher_cmd_desc.cryp.input_data_length = rctx->buflen;
	rctx->cipher_cmd_desc.cryp.output_data_addr_low = rctx->buf_dma & 0xFFFFFFFF;
	rctx->cipher_cmd_desc.cryp.output_data_addr_hig = (rctx->buf_dma >> 32) & 0xFFFFFFFF;
	rctx->cipher_cmd_desc.cryp.output_data_length = rctx->buflen;

	memcpy(rctx->cipher_cmd_desc.iv, areq->iv, ivsize);
	memcpy(rctx->cipher_cmd_desc.key, ctx->key, ctx->keylen);

	mb_msg.data = &rctx->cipher_cmd_desc;
	mb_msg.ctx = areq;
	err = mbox_send_message(rkc->mbox[0], &mb_msg);
	if (err < 0)
		goto err_unmap_buf;

	return 0;

err_unmap_buf:
	dma_unmap_single(rkc->dev, rctx->buf_dma, rctx->buflen,
			 DMA_BIDIRECTIONAL);
err_free_buf:
	kfree(rctx->buf);
	rctx->buflen = 0;
	return err;
}

static int nuclei_cipher_tfm_init(struct crypto_skcipher *tfm)
{
	struct nuclei_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	const char *name = crypto_tfm_alg_name(&tfm->base);
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct nuclei_crypto_tmp *algt = container_of(alg, struct nuclei_crypto_tmp, alg.skcipher.base);

	ctx->fallback_tfm = crypto_alloc_skcipher(name, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->fallback_tfm)) {
		dev_err(algt->dev->dev, "ERROR: Cannot allocate fallback for %s %ld\n",
			name, PTR_ERR(ctx->fallback_tfm));
		return PTR_ERR(ctx->fallback_tfm);
	}

	tfm->reqsize = sizeof(struct nuclei_cipher_rctx) +
		crypto_skcipher_reqsize(ctx->fallback_tfm);

	return 0;
}

static void nuclei_cipher_tfm_exit(struct crypto_skcipher *tfm)
{
	struct nuclei_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);

	memzero_explicit(ctx->key, ctx->keylen);
	crypto_free_skcipher(ctx->fallback_tfm);
}

#define NUCLEI_CRYPTO_PRIORITY 300

#define NUCLEI_SKCIPHER_ALG(alg_name, set_key_func, min_key_size,\
	max_key_size, encrypt_func, decrypt_func, iv_size) \
{\
	.type = CRYPTO_ALG_TYPE_SKCIPHER,\
	.alg.skcipher.base = { \
		.base.cra_name		= alg_name,\
		.base.cra_driver_name	= alg_name"-nuclei",\
		.base.cra_priority	= NUCLEI_CRYPTO_PRIORITY,\
		.base.cra_flags		= CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK,\
		.base.cra_blocksize	= AES_BLOCK_SIZE,\
		.base.cra_ctxsize	= sizeof(struct nuclei_cipher_ctx),\
		.base.cra_alignmask	= 0x0f,\
		.base.cra_module	= THIS_MODULE,\
		.init			= nuclei_cipher_tfm_init,\
		.exit			= nuclei_cipher_tfm_exit,\
		.min_keysize		= min_key_size,\
		.max_keysize		= max_key_size,\
		.ivsize			= iv_size,\
		.setkey			= set_key_func,\
		.encrypt		= encrypt_func,\
		.decrypt		= decrypt_func,\
     },\
	.alg.skcipher.op = {\
		.do_one_request = nuclei_cipher_run,\
	},\
}

struct nuclei_crypto_tmp nuclei_ecb_aes_alg = \
	NUCLEI_SKCIPHER_ALG("ecb(aes)", nuclei_cipher_setkey, AES_MIN_KEY_SIZE,\
		AES_MAX_KEY_SIZE, nuclei_aes_ecb_encrypt, nuclei_aes_ecb_decrypt, 0);

struct nuclei_crypto_tmp nuclei_cbc_aes_alg = \
	NUCLEI_SKCIPHER_ALG("cbc(aes)", nuclei_cipher_setkey, AES_MIN_KEY_SIZE,\
		AES_MAX_KEY_SIZE, nuclei_aes_cbc_encrypt, nuclei_aes_cbc_decrypt,\
		AES_BLOCK_SIZE);

struct nuclei_crypto_tmp nuclei_ctr_aes_alg = \
	NUCLEI_SKCIPHER_ALG("ctr(aes)", nuclei_cipher_setkey, AES_MIN_KEY_SIZE,\
		AES_MAX_KEY_SIZE, nuclei_aes_ctr_encrypt, nuclei_aes_ctr_decrypt,\
		AES_BLOCK_SIZE);

struct nuclei_crypto_tmp nuclei_ecb_sm4_alg = \
	NUCLEI_SKCIPHER_ALG("ecb(sm4)", nuclei_cipher_setkey, AES_MIN_KEY_SIZE,\
		AES_MAX_KEY_SIZE, nuclei_sm4_ecb_encrypt, nuclei_sm4_ecb_decrypt,\
		0);

struct nuclei_crypto_tmp nuclei_cbc_sm4_alg = \
	NUCLEI_SKCIPHER_ALG("cbc(sm4)", nuclei_cipher_setkey, AES_MIN_KEY_SIZE,\
		AES_MAX_KEY_SIZE, nuclei_sm4_cbc_encrypt, nuclei_sm4_cbc_decrypt,\
		AES_BLOCK_SIZE);

