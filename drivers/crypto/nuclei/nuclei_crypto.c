// SPDX-License-Identifier: GPL-2.0-only
/*
 * Crypto acceleration support for Nuclei HSM crypto
 *
 * Copyright (c) 2024, Nucleisys Co., Ltd
 *
 */

#include "nuclei_crypto.h"
#include <crypto/engine.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <linux/dma-map-ops.h>

static struct nuclei_crypt_list cryptlist = {
	.dev_list = LIST_HEAD_INIT(cryptlist.dev_list),
	.lock = __SPIN_LOCK_UNLOCKED(cryptlist.lock),
};

struct crypto_mailbox_msg {
	u32 type;
	u32 ret;
	void* ctx;
};

struct nuclei_crypto_info *get_nuclei_crypto(void)
{
	struct nuclei_crypto_info *first;

	spin_lock(&cryptlist.lock);
	first = list_first_entry_or_null(&cryptlist.dev_list,
					 struct nuclei_crypto_info, list);
	list_rotate_left(&cryptlist.dev_list);
	spin_unlock(&cryptlist.lock);
	return first;
}

static struct nuclei_crypto_tmp *nuclei_cipher_algs[] = {
	&nuclei_ecb_aes_alg,
	&nuclei_cbc_aes_alg,
	//&nuclei_ctr_aes_alg,
	&nuclei_ecb_sm4_alg,
	&nuclei_cbc_sm4_alg,
	&nuclei_ahash_hash_sha1,
	&nuclei_ahash_hash_sha256,
	&nuclei_ahash_hash_md5,
	&nuclei_ahash_hmac_sha1,
	&nuclei_ahash_hmac_sha256,
	&nuclei_ahash_hmac_md5,	
};

extern void nuclei_skcipher_done(int err, void *_req);
extern void nuclei_ahash_done(int err, void *_req);

static void nuclei_mb_rx_callback(struct mbox_client *cl, void *msg)
{
	struct nuclei_mbox_msg *mssg = msg;
	int ret = *(u32*)mssg->data;

	/* take header reserved field as crypto type */	 
	if ((ret & 0xFF) == CRYPTO_ALG_TYPE_SKCIPHER) {
		nuclei_skcipher_done(ret & BIT(31), mssg);
	} else if ((ret & 0xFF) == CRYPTO_ALG_TYPE_AHASH) {
		nuclei_ahash_done(ret & BIT(31), mssg);
	}
}

static int nuclei_mb_init(struct nuclei_crypto_info *crypto_info)
{
	int err=0, i;

	struct mbox_client *mcl = &crypto_info->mcl;
	struct device *dev = crypto_info->dev;

	crypto_info->mbox = devm_kcalloc(dev, crypto_info->num_chan,
				  sizeof(struct mbox_chan *), GFP_KERNEL);
	if (!crypto_info->mbox)
		return -ENOMEM;

	mcl->dev = dev;
	mcl->tx_block = false;
	mcl->tx_tout = 0;
	mcl->knows_txdone = true;
	mcl->rx_callback = nuclei_mb_rx_callback;
	mcl->tx_done = NULL;

	for (i = 0; i < crypto_info->num_chan; i++) {
		crypto_info->mbox[i] = mbox_request_channel(mcl, i);
		if (IS_ERR(crypto_info->mbox[i])) {
			err = PTR_ERR(crypto_info->mbox[i]);
			dev_err(dev,
				"Mbox channel %d request failed with err %d",
				i, err);
			crypto_info->mbox[i] = NULL;
			goto free_channels;
		}
	}

	return 0;
free_channels:
	for (i = 0; i < crypto_info->num_chan; i++) {
		if (crypto_info->mbox[i])
			mbox_free_channel(crypto_info->mbox[i]);
	}

	return err;
}

static int nuclei_crypto_debugfs_show(struct seq_file *seq, void *v)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(nuclei_cipher_algs); i++) {
		if (!nuclei_cipher_algs[i]->dev)
			continue;
		switch (nuclei_cipher_algs[i]->type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			seq_printf(seq, "%s %s reqs=%lu fallback=%lu\n",
				   nuclei_cipher_algs[i]->alg.skcipher.base.base.cra_driver_name,
				   nuclei_cipher_algs[i]->alg.skcipher.base.base.cra_name,
				   nuclei_cipher_algs[i]->stat_req, nuclei_cipher_algs[i]->stat_fb);
			seq_printf(seq, "\tfallback due to length: %lu\n",
				   nuclei_cipher_algs[i]->stat_fb_len);
			seq_printf(seq, "\tfallback due to alignment: %lu\n",
				   nuclei_cipher_algs[i]->stat_fb_align);
			seq_printf(seq, "\tfallback due to SGs: %lu\n",
				   nuclei_cipher_algs[i]->stat_fb_sgdiff);
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			seq_printf(seq, "%s %s reqs=%lu fallback=%lu\n",
				   nuclei_cipher_algs[i]->alg.hash.base.halg.base.cra_driver_name,
				   nuclei_cipher_algs[i]->alg.hash.base.halg.base.cra_name,
				   nuclei_cipher_algs[i]->stat_req, nuclei_cipher_algs[i]->stat_fb);
			break;
		}
	}
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(nuclei_crypto_debugfs);

static void register_debugfs(struct nuclei_crypto_info *crypto_info)
{
	struct dentry *dbgfs_dir __maybe_unused;
	struct dentry *dbgfs_stats __maybe_unused;

	/* Ignore error of debugfs */
	dbgfs_dir = debugfs_create_dir("nuclie_crypto", NULL);
	dbgfs_stats = debugfs_create_file("stats", 0444, dbgfs_dir, &cryptlist,
					  &nuclei_crypto_debugfs_fops);

#ifdef CONFIG_CRYPTO_DEV_ROCKCHIP_DEBUG
	cryptlist.dbgfs_dir = dbgfs_dir;
	cryptlist.dbgfs_stats = dbgfs_stats;
#endif
}

static int nuclei_crypto_register(struct nuclei_crypto_info *crypto_info)
{
	unsigned int i, k;
	int err = 0;

	for (i = 0; i < ARRAY_SIZE(nuclei_cipher_algs); i++) {
		nuclei_cipher_algs[i]->dev = crypto_info;
		switch (nuclei_cipher_algs[i]->type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			dev_info(crypto_info->dev, "Register %s as %s\n",
				 nuclei_cipher_algs[i]->alg.skcipher.base.base.cra_name,
				 nuclei_cipher_algs[i]->alg.skcipher.base.base.cra_driver_name);
			err = crypto_engine_register_skcipher(&nuclei_cipher_algs[i]->alg.skcipher);
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			dev_info(crypto_info->dev, "Register %s as %s\n",
				 nuclei_cipher_algs[i]->alg.hash.base.halg.base.cra_name,
				 nuclei_cipher_algs[i]->alg.hash.base.halg.base.cra_driver_name);
			err = crypto_engine_register_ahash(&nuclei_cipher_algs[i]->alg.hash);
			break;
		default:
			dev_err(crypto_info->dev, "unknown algorithm\n");
		}
		if (err)
			goto err_cipher_algs;
	}
	return 0;

err_cipher_algs:
	for (k = 0; k < i; k++) {
		if (nuclei_cipher_algs[i]->type == CRYPTO_ALG_TYPE_SKCIPHER)
			crypto_engine_unregister_skcipher(&nuclei_cipher_algs[k]->alg.skcipher);
		else
			crypto_engine_unregister_ahash(&nuclei_cipher_algs[i]->alg.hash);
	}
	return err;
}

static void nuclei_crypto_unregister(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(nuclei_cipher_algs); i++) {
		if (nuclei_cipher_algs[i]->type == CRYPTO_ALG_TYPE_SKCIPHER)
			crypto_engine_unregister_skcipher(&nuclei_cipher_algs[i]->alg.skcipher);
		else
			crypto_engine_unregister_ahash(&nuclei_cipher_algs[i]->alg.hash);
	}
}

static const struct of_device_id crypto_of_id_table[] = {
	{ .compatible = "nuclei,nuclei-crypto",	},
	{}
};
MODULE_DEVICE_TABLE(of, crypto_of_id_table);

static int nuclei_crypto_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nuclei_crypto_info *crypto_info, *first;
	int err = 0;

	crypto_info = devm_kzalloc(&pdev->dev,
				   sizeof(*crypto_info), GFP_KERNEL);
	if (!crypto_info) {
		err = -ENOMEM;
		goto err_crypto;
	}

	crypto_info->dev = &pdev->dev;
	crypto_info->num_chan = 1;
	platform_set_drvdata(pdev, crypto_info);
	
	nuclei_mb_init(crypto_info);

	crypto_info->engine = crypto_engine_alloc_init(&pdev->dev, true);
	crypto_engine_start(crypto_info->engine);
	init_completion(&crypto_info->complete);

	spin_lock(&cryptlist.lock);
	first = list_first_entry_or_null(&cryptlist.dev_list,
					 struct nuclei_crypto_info, list);
	list_add_tail(&crypto_info->list, &cryptlist.dev_list);
	spin_unlock(&cryptlist.lock);

	if (!first) {
		err = nuclei_crypto_register(crypto_info);
		if (err) {
			dev_err(dev, "Fail to register crypto algorithms");
			goto err_register_alg;
		}

		register_debugfs(crypto_info);
	}

	return 0;

err_register_alg:
	crypto_engine_exit(crypto_info->engine);
err_crypto:
	dev_err(dev, "Crypto Accelerator not successfully registered\n");
	return err;
}

static int nuclei_crypto_remove(struct platform_device *pdev)
{
	struct nuclei_crypto_info *crypto_tmp = platform_get_drvdata(pdev);
	struct nuclei_crypto_info *first;

	spin_lock_bh(&cryptlist.lock);
	list_del(&crypto_tmp->list);
	first = list_first_entry_or_null(&cryptlist.dev_list,
					 struct nuclei_crypto_info, list);
	spin_unlock_bh(&cryptlist.lock);

	if (!first) {
#ifdef CONFIG_CRYPTO_DEV_ROCKCHIP_DEBUG
	debugfs_remove_recursive(cryptlist.dbgfs_dir);
#endif
		nuclei_crypto_unregister();
	}
	crypto_engine_exit(crypto_tmp->engine);
	return 0;
}

static struct platform_driver crypto_driver = {
	.probe		= nuclei_crypto_probe,
	.remove		= nuclei_crypto_remove,
	.driver		= {
		.name	= "nuclei-crypto",
		.of_match_table	= crypto_of_id_table,
	},
};

module_platform_driver(crypto_driver);
