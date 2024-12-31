/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NUCLEI_CRYPTO_H__
#define __NUCLEI_CRYPTO_H__

#include <crypto/aes.h>
#include <crypto/engine.h>
#include <crypto/internal/des.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>
#include <crypto/md5.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>
#include <linux/types.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox/nuclei_mailbox.h>
#include "nuclei_abi.h"

#if 0
/* cipher mode */
#define NUCLEI_CIPHER_DEC	BIT(0)

#define NUCLEI_CIPHER_DEC_SHIFT 	0
#define NUCLEI_CIPHER_DEC_MSK	 	GENMASK(0,0)
#define NUCLEI_CIPHER_ALG_SHIFT 	1
#define NUCLEI_CIPHER_ALG_MSK	 	GENMASK(4,1)
#define NUCLEI_CIPHER_MOD_SHIFT 	5
#define NUCLEI_CIPHER_MOD_MSK		GENMASK(7,5)
#define NUCLEI_CIPHER_KEYLEN_SHIFT 	8
#define NUCLEI_CIPHER_KEYLEN_MSK	GENMASK(9,8)
#define NUCLEI_CIPHER_KEYSEL_SHIFT 	10
#define NUCLEI_CIPHER_KEYSEL_MSK	GENMASK(12,10)
#define NUCLEI_CIPHER_INCTL_SHIFT 	13
#define NUCLEI_CIPHER_INCTL_MSK		GENMASK(14,13)
#endif
/*
 * struct nuclei_crypt_list - struct for managing a list of crypto instance
 * @dev_list:		Used for doing a list of nuclei_crypto_info
 * @lock:		Control access to dev_list
 * @dbgfs_dir:		Debugfs dentry for statistic directory
 * @dbgfs_stats:	Debugfs dentry for statistic counters
 */
struct nuclei_crypt_list {
	struct list_head	dev_list;
	spinlock_t		lock; /* Control access to dev_list */
	struct dentry		*dbgfs_dir;
	struct dentry		*dbgfs_stats;
};

struct nuclei_crypto_info {
	struct list_head		list;
	struct device			*dev;
	struct mbox_client mcl;
	int num_chan;
	/* Array of mailbox channel pointers, one for each channel */
	struct mbox_chan **mbox;
	
	unsigned long nreq;
	struct crypto_engine *engine;
	struct completion complete;
	int status;
};

/* the private variable of hash */
struct nuclei_ahash_ctx {
	unsigned int	keylen;
	u8				key[128];
	/* for fallback */	
	struct crypto_ahash		*fallback_tfm;
};

/* the private variable of hash for fallback */
struct nuclei_ahash_rctx {
	struct nuclei_crypto_info		*dev;
	mailbox_hash_cmd_in_token 	hash_cmd_desc;
	struct ahash_request		fallback_req;
	u32				mode;
	int nrsg;
};

/* the private variable of cipher */
struct nuclei_cipher_ctx {
	unsigned int			keylen;
	u8				key[AES_MAX_KEY_SIZE];
	u8				iv[AES_BLOCK_SIZE];
	struct crypto_skcipher *fallback_tfm;
};

struct nuclei_cipher_rctx {
	struct nuclei_crypto_info		*dev;
	mailbox_cryp_cmd_in_token cipher_cmd_desc;
	void *buf;
	dma_addr_t buf_dma;
	size_t buflen;
	struct skcipher_request fallback_req;   // keep at the end
};

struct nuclei_crypto_tmp {
	u32 type;
	struct nuclei_crypto_info           *dev;
	union {
		struct skcipher_engine_alg skcipher;
		struct ahash_engine_alg hash;
	} alg;
	unsigned long stat_req;
	unsigned long stat_fb;
	unsigned long stat_fb_len;
	unsigned long stat_fb_sglen;
	unsigned long stat_fb_align;
	unsigned long stat_fb_sgdiff;
};

extern struct nuclei_crypto_tmp nuclei_ecb_aes_alg;
extern struct nuclei_crypto_tmp nuclei_cbc_aes_alg;
extern struct nuclei_crypto_tmp nuclei_ctr_aes_alg;
extern struct nuclei_crypto_tmp nuclei_ecb_sm4_alg;
extern struct nuclei_crypto_tmp nuclei_cbc_sm4_alg;

extern struct nuclei_crypto_tmp nuclei_ahash_hash_sha1;
extern struct nuclei_crypto_tmp nuclei_ahash_hash_sha256;
extern struct nuclei_crypto_tmp nuclei_ahash_hash_md5;
extern struct nuclei_crypto_tmp nuclei_ahash_hmac_sha1;
extern struct nuclei_crypto_tmp nuclei_ahash_hmac_sha256;
extern struct nuclei_crypto_tmp nuclei_ahash_hmac_md5;

struct nuclei_crypto_info *get_nuclei_crypto(void);
#endif
