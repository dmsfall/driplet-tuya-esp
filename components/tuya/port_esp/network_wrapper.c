#include <stdbool.h>
#include <string.h>
#include "log.h"
#include "tuya_error_code.h"
#include "network_interface.h"
#include "system_interface.h"
#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "mbedtls/debug.h"
#include "mbedtls/timing.h"
#include "esp_log.h"

struct tls_context
{
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_x509_crt cacert;
	mbedtls_x509_crt clicert;
	mbedtls_pk_context pkey;
	mbedtls_net_context server_fd;
	uint32_t flags;
};

static const char *TAG = "tuya_network_wrapper";

static int mbedtls_random_port(void *p_rng, unsigned char *output, size_t output_len)
{
	int i = 0;

	for (i = 0; i < output_len; i++)
	{
		output[i] = (unsigned char)(0xff & system_random());
	}

	return 0;
}

int network_tls_init(NetworkContext_t *pNetwork, const TLSConnectParams *params)
{
	if (NULL == pNetwork)
	{
		return OPRT_INVALID_PARM;
	}

	pNetwork->connect = network_tls_connect;
	pNetwork->read = network_tls_read;
	pNetwork->write = network_tls_write;
	pNetwork->disconnect = network_tls_disconnect;
	pNetwork->destroy = network_tls_destroy;
	pNetwork->tlsConnectParams = *params;

	tls_context_t *tls_ctx = mbedtls_calloc(1, sizeof(tls_context_t));

	if (NULL == tls_ctx)
	{
		log_error("tls_ctx malloc fail");

		return OPRT_MALLOC_FAILED;
	}

	tls_ctx->flags = 0;
	pNetwork->context = tls_ctx;

	return OPRT_OK;
}

int network_tls_connect(NetworkContext_t *pNetwork, const TLSConnectParams *params)
{
	int ret = 0;
	tls_context_t *tlsDataParams = NULL;
	char portBuffer[6];

	if (NULL == pNetwork)
	{
		return OPRT_INVALID_PARM;
	}

	if (NULL != params)
	{
		pNetwork->tlsConnectParams = *params;
	}

	tlsDataParams = (tls_context_t *)(pNetwork->context);

	mbedtls_ssl_init(&(tlsDataParams->ssl));
	mbedtls_ssl_config_init(&(tlsDataParams->conf));
	mbedtls_x509_crt_init(&(tlsDataParams->cacert));
	mbedtls_x509_crt_init(&(tlsDataParams->clicert));
	mbedtls_pk_init(&(tlsDataParams->pkey));

	ESP_LOGD(TAG, "loading the CA root certificate...");

	ret = mbedtls_x509_crt_parse(&(tlsDataParams->cacert),
								 (const unsigned char *)pNetwork->tlsConnectParams.cacert,
								 pNetwork->tlsConnectParams.cacert_len);
	if (ret < 0)
	{
		ESP_LOGE(TAG, "failed! mbedtls_x509_crt_parse returned -0x%x while parsing root cert", -ret);

		return OPRT_MID_TLS_X509_ROOT_CRT_PARSE_ERROR;
	}

	ESP_LOGD(TAG, "ok (%d skipped)", ret);

	if (pNetwork->tlsConnectParams.client_cert && pNetwork->tlsConnectParams.client_key)
	{
		ESP_LOGD(TAG, "loading the client cert. and key...");

		ret = mbedtls_x509_crt_parse(&(tlsDataParams->clicert),
									 (const unsigned char *)pNetwork->tlsConnectParams.client_cert,
									 pNetwork->tlsConnectParams.client_cert_len);
		if (ret != 0)
		{
			ESP_LOGD(TAG, "failed! mbedtls_x509_crt_parse returned -0x%x while parsing device cert", -ret);
			mbedtls_x509_crt_free(&(tlsDataParams->cacert));

			return OPRT_MID_TLS_X509_DEVICE_CRT_PARSE_ERROR;
		}

		ret = mbedtls_pk_parse_key(&(tlsDataParams->pkey),
								   (const unsigned char *)pNetwork->tlsConnectParams.client_key,
								   pNetwork->tlsConnectParams.client_key_len, NULL, 0, NULL, NULL);
		if (ret != 0)
		{
			ESP_LOGD(TAG, "failed! mbedtls_pk_parse_key returned -0x%x while parsing private key", -ret);
			mbedtls_x509_crt_free(&(tlsDataParams->cacert));

			return OPRT_MID_TLS_PK_PRIVATE_KEY_PARSE_ERROR;
		}

		ESP_LOGD(TAG, "ok");
	}

	snprintf(portBuffer, 6, "%d", pNetwork->tlsConnectParams.port);

	ESP_LOGD(TAG, "connecting to %s/%s...", pNetwork->tlsConnectParams.host, portBuffer);

	if ((ret = mbedtls_net_connect(&(tlsDataParams->server_fd),
								   pNetwork->tlsConnectParams.host,
								   portBuffer, MBEDTLS_NET_PROTO_TCP)) != 0)
	{
		ESP_LOGE(TAG, "failed! mbedtls_net_connect returned -0x%x", -ret);
		mbedtls_x509_crt_free(&(tlsDataParams->cacert));

		switch (ret)
		{
		case MBEDTLS_ERR_NET_SOCKET_FAILED:
			return OPRT_MID_TLS_NET_SOCKET_ERROR;
		case MBEDTLS_ERR_NET_UNKNOWN_HOST:
			return OPRT_MID_TLS_UNKNOWN_HOST_ERROR;
		case MBEDTLS_ERR_NET_CONNECT_FAILED:
		default:
			return OPRT_MID_TLS_NET_CONNECT_ERROR;
		};
	}

	ret = mbedtls_net_set_block(&(tlsDataParams->server_fd));
	if (ret != 0)
	{
		ESP_LOGE(TAG, "failed\n  ! net_set_(non)block() returned -0x%x\n\n", -ret);
		mbedtls_x509_crt_free(&(tlsDataParams->cacert));

		return OPRT_MID_TLS_CONNECTION_ERROR;
	}

	ESP_LOGD(TAG, "ok");

	mbedtls_ssl_set_bio(&(tlsDataParams->ssl), &(tlsDataParams->server_fd), mbedtls_net_send, NULL, mbedtls_net_recv_timeout);
	mbedtls_ssl_conf_read_timeout(&(tlsDataParams->conf), pNetwork->tlsConnectParams.timeout_ms);

	ESP_LOGD(TAG, "setting up the SSL/TLS structure...");

	if ((ret = mbedtls_ssl_config_defaults(&(tlsDataParams->conf), MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
										   MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
	{
		ESP_LOGE(TAG, "failed! mbedtls_ssl_config_defaults returned -0x%x", -ret);
		mbedtls_x509_crt_free(&(tlsDataParams->cacert));

		return OPRT_MID_TLS_CONNECTION_ERROR;
	}

	if (pNetwork->tlsConnectParams.cert_verify == true)
	{
		mbedtls_ssl_conf_authmode(&(tlsDataParams->conf), MBEDTLS_SSL_VERIFY_REQUIRED);
	}
	else
	{
		mbedtls_ssl_conf_authmode(&(tlsDataParams->conf), MBEDTLS_SSL_VERIFY_OPTIONAL);
	}

	mbedtls_ssl_conf_rng(&(tlsDataParams->conf), mbedtls_random_port, NULL);

	mbedtls_ssl_conf_ca_chain(&(tlsDataParams->conf), &(tlsDataParams->cacert), NULL);

	if ((pNetwork->tlsConnectParams.client_cert) && (pNetwork->tlsConnectParams.client_key))
	{
		if ((ret = mbedtls_ssl_conf_own_cert(&(tlsDataParams->conf), &(tlsDataParams->clicert), &(tlsDataParams->pkey))) != 0)
		{
			ESP_LOGE(TAG, "failed! mbedtls_ssl_conf_own_cert returned %d\n\n", ret);
			mbedtls_x509_crt_free(&(tlsDataParams->cacert));

			return OPRT_MID_TLS_CONNECTION_ERROR;
		}
	}

	/* Assign the resulting configuration to the SSL context. */
	if ((ret = mbedtls_ssl_setup(&(tlsDataParams->ssl), &(tlsDataParams->conf))) != 0)
	{
		ESP_LOGE(TAG, " failed! mbedtls_ssl_setup returned -0x%x\n\n", -ret);
		mbedtls_x509_crt_free(&(tlsDataParams->cacert));

		return OPRT_MID_TLS_CONNECTION_ERROR;
	}

	if ((ret = mbedtls_ssl_set_hostname(&(tlsDataParams->ssl), pNetwork->tlsConnectParams.host)) != 0)
	{
		ESP_LOGE(TAG, "failed! mbedtls_ssl_set_hostname returned %d\n\n", ret);
		mbedtls_x509_crt_free(&(tlsDataParams->cacert));

		return OPRT_MID_TLS_CONNECTION_ERROR;
	}

	ESP_LOGD(TAG, "performing the SSL/TLS handshake...");

	while ((ret = mbedtls_ssl_handshake(&(tlsDataParams->ssl))) != 0)
	{
		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			ESP_LOGE(TAG, "failed! mbedtls_ssl_handshake returned -0x%x\n", -ret);

			if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
			{
				ESP_LOGE(TAG, "    unable to verify the server's certificate. "
							  "Either it is invalid,\n"
							  "    or you didn't set ca_file or ca_path "
							  "to an appropriate value.\n"
							  "    Alternatively, you may want to use "
							  "auth_mode=optional for testing purposes.\n");
			}

			mbedtls_x509_crt_free(&(tlsDataParams->cacert));

			return OPRT_MID_TLS_CONNECTION_ERROR;
		}
	}

	ESP_LOGD(TAG, "TLS handshake complete.");
	ESP_LOGD(TAG, "release CA x509 parse.");

	mbedtls_x509_crt_free(&(tlsDataParams->cacert));

	ESP_LOGD(TAG, "ok\n    [ Protocol is %s ]\n    [ Ciphersuite is %s ]\n", mbedtls_ssl_get_version(&(tlsDataParams->ssl)),
			 mbedtls_ssl_get_ciphersuite(&(tlsDataParams->ssl)));

	if ((ret = mbedtls_ssl_get_record_expansion(&(tlsDataParams->ssl))) >= 0)
	{
		ESP_LOGD(TAG, "    [ Record expansion is %d ]\n", ret);
	}
	else
	{
		ESP_LOGD(TAG, "    [ Record expansion is unknown (compression) ]\n");
	}

	return OPRT_OK;
}

int network_tls_disconnect(NetworkContext_t *pNetwork)
{
	int ret = 0;
	tls_context_t *tlsDataParams = (tls_context_t *)(pNetwork->context);

	do
	{
		ret = mbedtls_ssl_close_notify(&tlsDataParams->ssl);
	} while (ret == MBEDTLS_ERR_SSL_WANT_WRITE);

	mbedtls_net_free(&(tlsDataParams->server_fd));
	mbedtls_x509_crt_free(&(tlsDataParams->clicert));
	mbedtls_pk_free(&(tlsDataParams->pkey));
	mbedtls_ssl_free(&(tlsDataParams->ssl));
	mbedtls_ssl_config_free(&(tlsDataParams->conf));

	return OPRT_OK;
}

int network_tls_destroy(NetworkContext_t *pNetwork)
{
	tls_context_t *tlsDataParams = (pNetwork->context);

	mbedtls_free(tlsDataParams);
	pNetwork->context = NULL;

	return OPRT_OK;
}

static int mbedtls_status_is_ssl_in_progress(int ret)
{
	return (ret == MBEDTLS_ERR_SSL_WANT_READ ||
			ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
			ret == MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS ||
			ret == MBEDTLS_ERR_SSL_TIMEOUT ||
			ret == MBEDTLS_ERR_SSL_ASYNC_IN_PROGRESS);
}

int network_tls_write(NetworkContext_t *pNetwork, const unsigned char *pMsg, size_t len)
{
	tls_context_t *tlsDataParams = (tls_context_t *)(pNetwork->context);
	int rv = mbedtls_ssl_write(&(tlsDataParams->ssl), pMsg, len);

	if (rv < 0)
	{
		if (mbedtls_status_is_ssl_in_progress(rv))
		{
			return 0;
		}

		return OPRT_MID_TLS_NET_SOCKET_ERROR;
	}

	return rv;
}

int network_tls_read(NetworkContext_t *pNetwork, unsigned char *pMsg, size_t len)
{
	tls_context_t *tlsDataParams = (tls_context_t *)(pNetwork->context);
	int rv = mbedtls_ssl_read(&(tlsDataParams->ssl), pMsg, len);

	if (rv < 0)
	{
		if (mbedtls_status_is_ssl_in_progress(rv))
		{
			return 0;
		}

		return OPRT_MID_TLS_NET_SOCKET_ERROR;
	}

	return rv;
}
