/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright 2024 Nuclei
 */

/* opcode */
#define SECURE_SERVICE_OPCODE_HASH                          1
#define SECURE_SERVICE_OPCODE_CRYP                          2
#define SECURE_SERVICE_OPCODE_ACRYP                         3
#define SECURE_SERVICE_OPCODE_TRNG                          4
#define SECURE_SERVICE_OPCODE_EFUSE                         5
#define SECURE_SERVICE_OPCODE_BOOT                          6 
/* hash algorithm */
#define SECURE_SERVICE_HASH_SHA1                            1
#define SECURE_SERVICE_HASH_MD5                             2
#define SECURE_SERVICE_HASH_SHA224                          3
#define SECURE_SERVICE_HASH_SHA256                          4
#define SECURE_SERVICE_HASH_SHA512                          5
#define SECURE_SERVICE_HASH_SM3                             6
#define SECURE_SERVICE_HASH_SHA384                          7
#define HASH_TABLE_MAX                                      8
#define SECURE_SERVICE_HASH_DIGEST_OFFSET                   8
#define SECURE_SERVICE_HASH_KEY_OFFSET                      24
/* hash mode */
#define SECURE_SERVICE_HASH_MODE                            0
#define SECURE_SERVICE_HMAC_MODE                            1
/* cryp */
/* key source */
#define SECURE_SERVICE_CRYP_KEY_SEL_CFG                     0
#define SECURE_SERVICE_CRYP_KEY_SEL_GRP0                    1
#define SECURE_SERVICE_CRYP_KEY_SEL_GRP1                    2
#define SECURE_SERVICE_CRYP_KEY_SEL_GRP2                    3
#define SECURE_SERVICE_CRYP_KEY_SEL_GRP3                    4
#define SECURE_SERVICE_CRYP_KEY_SEL_GRP4                    5
#define SECURE_SERVICE_CRYP_KEY_SEL_GRP5                    6
/* encrypt or decrypt */
#define SECURE_SERVICE_CRYP_ENCRYPT                         0
#define SECURE_SERVICE_CRYP_DECRYPT                         1
/* nonce length */
#define SECURE_SERVICE_CRYP_NONCELEN_7                      0
#define SECURE_SERVICE_CRYP_NONCELEN_8                      1
#define SECURE_SERVICE_CRYP_NONCELEN_9                      2
#define SECURE_SERVICE_CRYP_NONCELEN_10                     3
#define SECURE_SERVICE_CRYP_NONCELEN_11                     4
#define SECURE_SERVICE_CRYP_NONCELEN_12                     5
#define SECURE_SERVICE_CRYP_NONCELEN_13                     6
/* cryp key size */
#define SECURE_SERVICE_CRYP_KEY_128BITS                     0
#define SECURE_SERVICE_CRYP_KEY_192BITS                     1
#define SECURE_SERVICE_CRYP_KEY_256BITS                     2
/* cryp mode */
#define SECURE_SERVICE_CRYP_ECB                             0
#define SECURE_SERVICE_CRYP_CBC                             1
#define SECURE_SERVICE_CRYP_CTR                             2
#define SECURE_SERVICE_CRYP_CMAC                            3
/* cryp algorithm */ 
#define SECURE_SERVICE_CRYP_AES                             0
#define SECURE_SERVICE_CRYP_SM4                             1
#define CRYP_TABLE_MAX                                      2

#define SECURE_SERVICE_IN_ALL                               0
#define SECURE_SERVICE_IN_INIT                              1
#define SECURE_SERVICE_IN_UPDATE                            2
#define SECURE_SERVICE_IN_END                               3

/* acryp */
/* acryp algorithm */
#define SECURE_SERVICE_ACRYP_ED25519                         1 
#define SECURE_SERVICE_ACRYP_SM2                             2
#define SECURE_SERVICE_ACRYP_RSA_PSS_2048                         3
// #define SECURE_SERVICE_ACRYP_RSA3072                         4
#define SECURE_SERVICE_ACRYP_RSA_PSS_4096                         5
#define SECURE_SERVICE_ACRYP_MOD_EXP                         6
#define ACRYP_SUPPORT_SIGN_ALGO_MAX_NUM                      6
/* acryp mode when acryp algorithm is 1 ~ 5 */
#define SECURE_SERVICE_ACRYP_VERIFY                          0
#define SECURE_SERVICE_ACRYP_SIGNATURE                       1
/* acryp mode when acryp algorithm is 6 */
#define SECURE_SERVICE_ACRYP_MOD_EXP_RSA2048                 0
#define SECURE_SERVICE_ACRYP_MOD_EXP_RSA4096                 1
#define SECURE_SERVICE_ACRYP_MOD_EXP_NORMAL                  2

/* efuse */
/* efuse mode */
#define SECURE_SERVICE_EFUSE_BIT                             0         /* efuse signle bit write/read */
#define SECURE_SERVICE_EFUSE_WORD                            1         /* efuse signle word write/read */ 
#define SECURE_SERVICE_EFUSE_WORD_CONTINUS                   2         /* efuse multiple words write/read */
/* efuse write or read */ 
#define SECURE_SERVICE_EFUSE_WRITE                           0
#define SECURE_SERVICE_EFUSE_READ                            1

/* efuse items select for word continue mode */
#define SECURE_SERVICE_EFUSE_WORD_CONTINUE_ENCRYPT_KEY_GRP0      0x01   /* efuse write symmetirc key to group0 */
#define SECURE_SERVICE_EFUSE_WORD_CONTINUE_ENCRYPT_KEY_GRP1      0x02   /* efuse write symmetirc key to group1 */
#define SECURE_SERVICE_EFUSE_WORD_CONTINUE_ENCRYPT_KEY_GRP2      0x03   /* efuse write symmetirc key to group2 */
#define SECURE_SERVICE_EFUSE_WORD_CONTINUE_ENCRYPT_KEY_GRP3      0x04   /* efuse write symmetirc key to group3 */
#define SECURE_SERVICE_EFUSE_WORD_CONTINUE_ENCRYPT_KEY_GRP4      0x05   /* efuse write symmetirc key to group4 */
#define SECURE_SERVICE_EFUSE_WORD_CONTINUE_ENCRYPT_KEY_GRP5      0x06   /* efuse write symmetirc key to group5 */
#define SECURE_SERVICE_EFUSE_WORD_CONTINUE_PUB_KEY0              0x10   /* efuse write public key0 */
#define SECURE_SERVICE_EFUSE_WORD_CONTINUE_PUB_KEY1              0x11   /* efuse write public key1 */
#define SECURE_SERVICE_EFUSE_WORD_CONTINUE_PUB_KEY2              0x12   /* efuse write public key2 */
#define SECURE_SERVICE_EFUSE_WORD_CONTINUE_PUB_KEY3              0x13   /* efuse write public key3 */

#define SECURE_SERVICE_ERR_LINK_FAIL                         -1
#define SECURE_SERVICE_ERR_HOST_READ_TIMEOUT                 -2
#define SECURE_SERVICE_ERR_HSM_READ_TIMEOUT                  -3
#define SECURE_SERVICE_ERR_HOST_WRITE_TIMEOUT                -4
#define SECURE_SERVICE_ERR_HSM_WRITE_TIMEOUT                 -5
#define SECURE_SERVICE_ERR_MAILBOX_IS_EMPTY                  -6
#define SECURE_SERVICE_ERR_UNLINK_FAIL                       -7
#define SECURE_SERVICE_ERR_OPCODE_NO_SUPPORT                 -8
#define SECURE_SERVICE_ERR_ALGO_NO_SUPPORT                   -9
#define SECURE_SERVICE_ERR_MODE_NO_SUPPORT                   -10
#define SECURE_SERVICE_ERR_KEY_SIZE                          -11
#define SECURE_SERVICE_ERR_KEY_SLT                           -12

struct common_head_t
{
    uint32_t TokenID:16;         /* inactive temporarily */
    uint32_t reserved:8;
    uint32_t subcode:4;          /* inactive temporarily */
    uint32_t opcode :4;
};

struct cmd_cfg_t
{
    uint32_t algo :4;
    uint32_t mode:4;
    uint32_t w_r:1;
    uint32_t keyLen:8;
    uint32_t in_ctrl :4;
    uint32_t reserved:11;
};

struct cryp_cmd_cfg{
    uint32_t algo :4;
    uint32_t mode :4;
    uint32_t key_length :4;
    uint32_t NonceLength :4;
    uint32_t encryp :1;
    uint32_t key_sel :4;
    uint32_t in_ctrl :4;
    uint32_t reserved :7;
};

typedef struct {
    struct common_head_t header;
    uint32_t identity;                 /* inactive temporarily */
    uint32_t length;                   /* total input length in byte */
    uint32_t input_data_addr_low;       /* input data address low 32bits */
    uint32_t input_data_addr_hig;       /* input data address high 32bits */
    uint32_t input_data_length;         /* input data length in byte */
    uint32_t output_data_addr_low;      /* output data address low 32bits */
    uint32_t output_data_addr_hig;      /* output data address high 32bits */
    uint32_t output_data_length;        /* output data length in byte */
    struct cryp_cmd_cfg cmd_cfg;       
} cryp_in_token_t;

typedef struct {
    struct common_head_t header;
    uint32_t identity;                  /* inactive temporarily */
    uint32_t length;                    /* total input length in byte */
    uint32_t input_data_addr_low;       /* input data address low 32bits */
    uint32_t input_data_addr_hig;       /* input data address high 32bits */
    uint32_t input_data_length;         /* input data length in byte */
    uint32_t input_signdata_addr_low;   /* signature or (M in M^0x10001 mod N) data address low 32bits */
    uint32_t input_signdata_addr_hig;   /* signature or (M in M^0x10001 mod N) data address high 32bits */
    uint32_t input_key_addr_low;        /* public/private key or (N in M^0x10001 mod N) data address low 32bits */
    uint32_t input_key_addr_hig;        /* public/private key or (N in M^0x10001 mod N) data address high 32bits */ 
    struct cmd_cfg_t cmd_cfg;
} acryp_in_token_t;

typedef struct {
    struct common_head_t header;
    uint32_t identity;                  /* inactive temporarily */
    uint32_t rsv1;                    
    uint32_t rsv2;       
    uint32_t rsv3;         
    uint32_t rsv4;         
    uint32_t output_addr_low;   /* random data address low 32bits */
    uint32_t output_addr_hig;   /* random data address high 32bits */
    uint32_t output_len;        /* random data length in byte */
    uint32_t rsv5;              
} trng_in_token_t;

typedef struct {
    struct common_head_t header;
    uint32_t identity;                  /* inactive temporarily */
    uint32_t length;                    /* total input length in byte */
    uint32_t input_data_addr_low;        /* input data address low 32bits */
    uint32_t input_data_addr_hig;        /* input data address high 32bits */
    uint32_t input_data_length;          /* input data length in byte */
    struct cmd_cfg_t cmd_cfg;
} hash_in_token_t;

typedef struct {
    cryp_in_token_t cryp;           /* cryp secure service struct */  
    uint32_t iv[4];                 /* IV data buffer */    
    uint32_t key[8];                /* key data buffer */ 
} mailbox_cryp_cmd_in_token;

typedef struct {
    hash_in_token_t hash;           /* hash secure service struct */
    uint32_t rescv;  
    uint32_t digest[16];            /* digest data buffer */
    uint8_t key[128];            /* digest data buffer */
} mailbox_hash_cmd_in_token;

