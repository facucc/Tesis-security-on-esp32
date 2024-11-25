#include "mbedtls/build_info.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform.h"
#include "mbedtls/rsa.h"
#include "mbedtls/x509_csr.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen_csr.h"

static const char* TAG = "GEN_CSR";

static int xWriteCertificateRequestPEM(mbedtls_x509write_csr* req, int (*f_rng)(void*, unsigned char*, size_t), void* p_rng, char* out_csr_pem);
static int xGenerateKey(mbedtls_pk_context* key, mbedtls_ctr_drbg_context* ctr_drbg, mbedtls_entropy_context* entropy, char* key_pem);
static int xGenerateCSR(mbedtls_pk_context* key, mbedtls_ctr_drbg_context* ctr_drbg, char* out_csr_pem);

static int xWritePrivateKey(mbedtls_pk_context* key, char* key_pem)
{
    int xRet;

    memset(key_pem, 0, PRIVATE_KEY_BUFFER_SIZE);

    if ((xRet = mbedtls_pk_write_key_pem(key, (unsigned char*)key_pem, PRIVATE_KEY_BUFFER_SIZE)) != 0) {
        ESP_LOGE(TAG, "Failed write key pem%d", xRet);
        return xRet;
    }
    return 0;
}

static int xWritePublicKey(mbedtls_pk_context* key)
{
    int xRet;
    unsigned char output_buf[2048];

    memset(output_buf, 0, sizeof(output_buf));

    if ((xRet = mbedtls_pk_write_pubkey_pem(key, output_buf, sizeof(output_buf))) != 0) {
        return xRet;
    }
    ESP_LOGI(TAG, "Public key: \n\t%s", output_buf);
    return 0;
}

bool GenerateCSR(char* key_pem, char* csr_pem)
{
    int xRet;
    mbedtls_pk_context* key = (mbedtls_pk_context*)calloc(1, sizeof(mbedtls_pk_context));
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context* ctr_drbg = (mbedtls_ctr_drbg_context*)calloc(1, sizeof(mbedtls_ctr_drbg_context));

    if (key == NULL || ctr_drbg == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed");
        if (key != NULL) {
            free(key);
        }
        if (ctr_drbg != NULL) {
            free(ctr_drbg);
        }

        return false;
    }

    mbedtls_pk_init(key);
    mbedtls_ctr_drbg_init(ctr_drbg);
    mbedtls_entropy_init(&entropy);

    if ((xRet = xGenerateKey(key, ctr_drbg, &entropy, key_pem)) != 0) {
        ESP_LOGE(TAG, "Key generation failed");
        goto exit;
    }

    if ((xRet = xGenerateCSR(key, ctr_drbg, csr_pem)) != 0) {
        ESP_LOGE(TAG, "CSR generation failed");
        goto exit;
    }

exit:
    mbedtls_pk_free(key);
    mbedtls_ctr_drbg_free(ctr_drbg);
    mbedtls_entropy_free(&entropy);
    free(key);
    free(ctr_drbg);

    return (xRet == 0);
}
static int xWriteCertificateRequestPEM(mbedtls_x509write_csr* req,
                                       int (*f_rng)(void*, unsigned char*, size_t),
                                       void* p_rng, char* out_csr_pem)
{
    int xRet;

    memset(out_csr_pem, 0, CSR_BUFFER_SIZE);

    if ((xRet = mbedtls_x509write_csr_pem(req, (unsigned char*)out_csr_pem, CSR_BUFFER_SIZE, f_rng, p_rng)) < 0) {
        return xRet;
    }

    return 0;
}

static int xGenerateKey(mbedtls_pk_context* key, mbedtls_ctr_drbg_context* ctr_drbg, mbedtls_entropy_context* entropy, char* key_pem)
{
    int xRet;
    const char* pers = "gen_key";

    if ((xRet = mbedtls_ctr_drbg_seed(ctr_drbg, mbedtls_entropy_func, entropy, (const unsigned char*)pers, strlen(pers))) != 0) {
        mbedtls_strerror(xRet, key_pem, sizeof(key_pem));
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed failed: -0x%04x - %s", (unsigned int) - xRet, key_pem);
        return xRet;
    }

    if ((xRet = mbedtls_pk_setup(key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA))) != 0) {
        mbedtls_strerror(xRet, key_pem, sizeof(key_pem));
        ESP_LOGE(TAG, "mbedtls_pk_setup failed: -0x%04x - %s", (unsigned int) - xRet, key_pem);
        return xRet;
    }
    if ((xRet = mbedtls_rsa_gen_key(mbedtls_pk_rsa(*key), mbedtls_ctr_drbg_random, ctr_drbg, KEY_SIZE, EXPONENT)) != 0) {
        mbedtls_strerror(xRet, key_pem, sizeof(key_pem));
        ESP_LOGE(TAG, "mbedtls_rsa_gen_key failed: -0x%04x - %s", (unsigned int) - xRet, key_pem);
        return xRet;
    }
    if ((xRet = xWritePrivateKey(key, key_pem)) != 0) {
        ESP_LOGE(TAG, "write_private_key failed");
        return xRet;
    }

    if ((xRet = xWritePublicKey(key)) != 0) {
        ESP_LOGE(TAG, "write_public_key failed");
        return xRet;
    }

    return 0;
}

static int xGenerateCSR(mbedtls_pk_context* key,
                        mbedtls_ctr_drbg_context* ctr_drbg,
                        char* out_csr_pem)
{
    int xRet;
    mbedtls_x509write_csr csr;

    mbedtls_x509write_csr_init(&csr);

    xRet = mbedtls_x509write_csr_set_subject_name(&csr, DFL_SUBJECT_NAME);
    if (xRet != 0) {
        ESP_LOGE(TAG, "Failed to set subject name: %d", xRet);
        goto cleanup;
    }

    mbedtls_x509write_csr_set_key(&csr, key);
    mbedtls_x509write_csr_set_md_alg(&csr, MBEDTLS_MD_SHA256);

    xRet = mbedtls_x509write_csr_set_key_usage(&csr, MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
    if (xRet != 0) {
        ESP_LOGE(TAG, "mbedtls_x509write_csr_set_key_usage failed: %d", xRet);
        goto cleanup;
    }

    xRet = xWriteCertificateRequestPEM(&csr, mbedtls_ctr_drbg_random, ctr_drbg, out_csr_pem);
    if (xRet != 0) {
        ESP_LOGE(TAG, "write_certificate_request failed: %d", xRet);
        goto cleanup;
    }

    mbedtls_x509write_csr_free(&csr);
    return 0;

cleanup:
    mbedtls_x509write_csr_free(&csr);
    return xRet;
}
