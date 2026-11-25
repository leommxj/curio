#include "curio_tls.h"
#include "esp_netif.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include <stdio.h>
#include <string.h>

static int generate(curio_config_t *config)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    mbedtls_svc_key_id_t id = 0;
    mbedtls_pk_context key;
    mbedtls_x509write_cert cert;
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&cert);
    int result = psa_generate_key(&attributes, &id);
    if (!result) result = mbedtls_pk_copy_from_psa(id, &key);
    psa_destroy_key(id);
    psa_reset_key_attributes(&attributes);
    if (result) goto done;
    mbedtls_x509write_crt_set_version(&cert, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&cert, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&cert, &key);
    mbedtls_x509write_crt_set_issuer_key(&cert, &key);
    unsigned char serial[16] = {0};
    result = psa_generate_random(serial, sizeof(serial));
    serial[0] = (serial[0] & 0x7f) | 1;
    char name[48], dns[40];
    snprintf(dns, sizeof(dns), "%s.local", config->network.hostname);
    snprintf(name, sizeof(name), "CN=%s", dns);
    mbedtls_x509_san_list short_name = {.node = {.type = MBEDTLS_X509_SAN_DNS_NAME,
        .san.unstructured_name = {.p = (unsigned char *)config->network.hostname, .len = strlen(config->network.hostname)}}};
    mbedtls_x509_san_list local_name = {.node = {.type = MBEDTLS_X509_SAN_DNS_NAME,
        .san.unstructured_name = {.p = (unsigned char *)dns, .len = strlen(dns)}}, .next = &short_name};
    uint32_t addresses[CURIO_WIFI_MAX_NETWORKS];
    mbedtls_x509_san_list ip_names[CURIO_WIFI_MAX_NETWORKS];
    for (size_t i = 0; i < config->network.wifi_count; i++) {
        const char *address = config->network.wifi[i].ipv4.address;
        if (!*address) continue;
        addresses[i] = esp_ip4addr_aton(address);
        ip_names[i] = (mbedtls_x509_san_list){.node = {.type = MBEDTLS_X509_SAN_IP_ADDRESS,
            .san.unstructured_name = {.p = (unsigned char *)&addresses[i], .len = 4}}, .next = short_name.next};
        short_name.next = &ip_names[i];
    }
    if (!result) result = mbedtls_x509write_crt_set_serial_raw(&cert, serial, sizeof(serial));
    if (!result) result = mbedtls_x509write_crt_set_subject_name(&cert, name);
    if (!result) result = mbedtls_x509write_crt_set_issuer_name(&cert, name);
    if (!result) result = mbedtls_x509write_crt_set_validity(&cert, "20240101000000", "20491231235959");
    if (!result) result = mbedtls_x509write_crt_set_basic_constraints(&cert, 1, -1);
    if (!result) result = mbedtls_x509write_crt_set_key_usage(&cert, MBEDTLS_X509_KU_DIGITAL_SIGNATURE | MBEDTLS_X509_KU_KEY_CERT_SIGN);
    if (!result) result = mbedtls_x509write_crt_set_subject_alternative_name(&cert, &local_name);
    if (!result) result = mbedtls_x509write_crt_pem(&cert, (unsigned char *)config->tls.certificate, sizeof(config->tls.certificate));
    if (!result) result = mbedtls_pk_write_key_pem(&key, (unsigned char *)config->tls.private_key, sizeof(config->tls.private_key));
done:
    mbedtls_x509write_crt_free(&cert);
    mbedtls_pk_free(&key);
    return result;
}

esp_err_t curio_tls_prepare(curio_config_t *config)
{
    if (!config->network.wifi_count && !config->tls.certificate[0]) return ESP_OK;
    if (psa_crypto_init() != PSA_SUCCESS) return ESP_FAIL;
    if (!config->tls.certificate[0]) return generate(config) == 0 ? ESP_OK : ESP_FAIL;
    mbedtls_x509_crt cert;
    mbedtls_pk_context key;
    mbedtls_x509_crt_init(&cert);
    mbedtls_pk_init(&key);
    int result = mbedtls_x509_crt_parse(&cert, (const unsigned char *)config->tls.certificate, strlen(config->tls.certificate) + 1);
    if (!result) result = mbedtls_pk_parse_key(&key, (const unsigned char *)config->tls.private_key, strlen(config->tls.private_key) + 1, NULL, 0);
    if (!result) result = mbedtls_pk_check_pair(&cert.pk, &key);
    if (!result) {
        char dns[40];
        snprintf(dns, sizeof(dns), "%s.local", config->network.hostname);
        uint32_t flags = 0;
        int verified = mbedtls_x509_crt_verify(&cert, &cert, NULL, dns, &flags, NULL, NULL);
        if (verified) result = !flags || (flags & ~(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE));
    }
    mbedtls_pk_free(&key);
    mbedtls_x509_crt_free(&cert);
    return result == 0 ? ESP_OK : ESP_ERR_INVALID_ARG;
}
