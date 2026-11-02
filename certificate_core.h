/* --------------------------------------------------------------------------------
 #
 #	certificate_core.h
 #	OpenSSL 3 implementation of the plugin commands, free of 4D types so it
 #	can be compiled into a stand-alone test program as well as the plugin.
 #	Included by 4DPlugin.cpp only (every function is static).
 #
 # --------------------------------------------------------------------------------*/

#ifndef CERTIFICATE_CORE_H
#define CERTIFICATE_CORE_H

// only the OpenSSL 3 API: a deprecated call is a compile error, not a warning
#ifndef OPENSSL_API_COMPAT
#define OPENSSL_API_COMPAT 30000
#endif
#ifndef OPENSSL_NO_DEPRECATED
#define OPENSSL_NO_DEPRECATED
#endif

#include "openssl/bio.h"
#include "openssl/core_names.h"
#include "openssl/encoder.h"
#include "openssl/err.h"
#include "openssl/evp.h"
#include "openssl/objects.h"
#include "openssl/pem.h"
#include "openssl/pkcs12.h"
#include "openssl/provider.h"
#include "openssl/rsa.h"
#include "openssl/x509.h"

#include <string>
#include <vector>
#include <utility>

#define ALGO_MD5 0
#define ALGO_SHA1 1
#define ALGO_SHA224 2
#define ALGO_SHA256 3
#define ALGO_SHA384 4
#define ALGO_SHA512 5
#define ALGO_MDC2 6
#define ALGO_RIPEMD160 7

// largest RSA modulus OpenSSL accepts (OPENSSL_RSA_MAX_MODULUS_BITS)
#define CERT_RSA_MAX_BITS 16384

typedef std::vector< std::pair<std::string, std::string> > cert_subject_t;

// OpenSSL 3 needs no global init or cleanup. Once any provider is loaded
// explicitly, the default one is no longer loaded implicitly, so load both:
// legacy (built into libcrypto with no-module) supplies MDC2 and the RC2/
// RC4/DES ciphers of PKCS#12 files exported by older tools.
// Call once, at plugin init, before any command can run.
static void cert_load_providers()
{
	static OSSL_PROVIDER *defaultProvider = NULL;
	static OSSL_PROVIDER *legacyProvider = NULL;
	if(!defaultProvider) defaultProvider = OSSL_PROVIDER_load(NULL, "default");
	if(!legacyProvider) legacyProvider = OSSL_PROVIDER_load(NULL, "legacy");
	//the RSA provider signs MDC2 with the OIW OID mdc2WithRSASignature
	//(1.3.14.3.2.14), but libcrypto only maps mdc2WithRSA (2.5.8.3.100) back
	//to MDC2 + RSA, so X509_REQ_verify() would fail with "unknown signature
	//algorithm"; register the OIW OID as well
	int nid = OBJ_txt2nid("1.3.14.3.2.14");
	if(nid == NID_undef) nid = OBJ_create("1.3.14.3.2.14", "RSA-MDC2-OIW", "mdc2WithRSASignature");
	if(nid != NID_undef)
	{
		int digestNid = NID_undef, keyNid = NID_undef;
		if(!OBJ_find_sigid_algs(nid, &digestNid, &keyNid))
		{
			OBJ_add_sigid(nid, NID_mdc2, NID_rsaEncryption);
		}
	}
	ERR_clear_error();
}

static const EVP_MD *cert_digest(int algo)
{
	switch (algo)
	{
		case ALGO_MD5:
			return EVP_md5();
		case ALGO_SHA1:
			return EVP_sha1();
		case ALGO_SHA224:
			return EVP_sha224();
		case ALGO_SHA256:
			return EVP_sha256();
		case ALGO_SHA384:
			return EVP_sha384();
		case ALGO_SHA512:
			return EVP_sha512();
		case ALGO_MDC2:
			return EVP_mdc2();
		case ALGO_RIPEMD160:
			return EVP_ripemd160();
		default:
			return EVP_sha256();
	}
}

static bool cert_bio_to_string(BIO *bio, std::string &out)
{
	char *buf = NULL;
	long len = BIO_get_mem_data(bio, &buf);
	if((len > 0) && buf)
	{
		out.assign(buf, (size_t)len);
		return true;
	}
	return false;
}

// public key as PKCS#1 "BEGIN RSA PUBLIC KEY" (type-specific structure),
// the format PEM_write_bio_RSAPublicKey produced
static bool cert_write_public_key_pkcs1(EVP_PKEY *key, std::string &out)
{
	bool ok = false;
	BIO *bio = BIO_new(BIO_s_mem());
	if(bio)
	{
		OSSL_ENCODER_CTX *ectx = OSSL_ENCODER_CTX_new_for_pkey(key,
																													 OSSL_KEYMGMT_SELECT_PUBLIC_KEY,
																													 "PEM",
																													 "type-specific",
																													 NULL);
		if(ectx)
		{
			if((OSSL_ENCODER_CTX_get_num_encoders(ectx) > 0) && OSSL_ENCODER_to_bio(ectx, bio))
			{
				ok = cert_bio_to_string(bio, out);
			}
			OSSL_ENCODER_CTX_free(ectx);
		}
		BIO_free(bio);
	}
	return ok;
}

// private key, unencrypted, in the traditional per-algorithm format
// ("BEGIN RSA PRIVATE KEY", "BEGIN EC PRIVATE KEY", ...); key types that have
// no traditional format (Ed25519, ...) fall back to PKCS#8 "BEGIN PRIVATE KEY"
static bool cert_write_private_key(EVP_PKEY *key, std::string &out)
{
	bool ok = false;
	BIO *bio = BIO_new(BIO_s_mem());
	if(bio)
	{
		if(PEM_write_bio_PrivateKey_traditional(bio, key, NULL, NULL, 0, NULL, NULL))
		{
			ok = cert_bio_to_string(bio, out);
		}
		BIO_free(bio);
	}
	if(!ok)
	{
		ERR_clear_error();
		bio = BIO_new(BIO_s_mem());
		if(bio)
		{
			if(PEM_write_bio_PrivateKey(bio, key, NULL, NULL, 0, NULL, NULL))
			{
				ok = cert_bio_to_string(bio, out);
			}
			BIO_free(bio);
		}
	}
	return ok;
}

// RSA key pair + PKCS#10 request signed with it.
// request/publicKey/privateKey are set only on success.
static bool cert_make_request(int algo,
															int bits,
															const cert_subject_t &subject,
															std::string &request,
															std::string &publicKey,
															std::string &privateKey)
{
	// a 4D-supplied key length goes straight into RSA key generation, whose
	// cost grows steeply with the size: an oversized value would keep the
	// calling process busy for minutes or hours
	if((bits <= 0) || (bits > CERT_RSA_MAX_BITS)) return false;

	bool ok = false;
	EVP_PKEY *key = NULL;
	EVP_PKEY_CTX *kctx = EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL);
	if(kctx)
	{
		//public exponent: the default, 65537 (RSA_F4)
		if((EVP_PKEY_keygen_init(kctx) > 0)
			 && (EVP_PKEY_CTX_set_rsa_keygen_bits(kctx, bits) > 0))
		{
			if(EVP_PKEY_generate(kctx, &key) <= 0) key = NULL;
		}
		EVP_PKEY_CTX_free(kctx);
	}
	if(key)
	{
		X509_REQ *req = X509_REQ_new();
		if(req)
		{
			//PKCS#10 defines a single version, v1, encoded as 0
			if(X509_REQ_set_version(req, X509_REQ_VERSION_1))
			{
				X509_NAME *name = X509_REQ_get_subject_name(req);
				for(size_t i = 0; i < subject.size(); ++i)
				{
					const std::string &field = subject[i].first;
					const std::string &value = subject[i].second;
					if(field.size() && value.size())
					{
						//an unknown field name or an invalid value is skipped
						if(!X509_NAME_add_entry_by_txt(name,
																					 field.c_str(),
																					 MBSTRING_UTF8,
																					 (const unsigned char *)value.data(),
																					 (int)value.size(),
																					 -1,
																					 0))
						{
							ERR_clear_error();
						}
					}
				}
				const EVP_MD *md = cert_digest(algo);
				if(X509_REQ_set_pubkey(req, key) && X509_REQ_sign(req, key, md))
				{
					std::string reqStr, pubStr, privStr;
					BIO *requestBio = BIO_new(BIO_s_mem());
					if(requestBio)
					{
						bool written = PEM_write_bio_X509_REQ(requestBio, req) && cert_bio_to_string(requestBio, reqStr);
						BIO_free(requestBio);
						if(written
							 && cert_write_public_key_pkcs1(key, pubStr)
							 && cert_write_private_key(key, privStr))
						{
							request = reqStr;
							publicKey = pubStr;
							privateKey = privStr;
							ok = true;
						}
					}
				}
			}
			X509_REQ_free(req);
		}
		EVP_PKEY_free(key);
	}
	ERR_clear_error();
	return ok;
}

// self-signed X.509 v3 certificate from a PEM request and its PEM private key.
// certificate is set only on success.
static bool cert_make_certificate(int algo,
																	int days,
																	const std::string &request,
																	const std::string &privateKey,
																	std::string &certificate)
{
	bool ok = false;
	X509 *cert = X509_new();
	if(cert)
	{
		BIO *reqBio = BIO_new_mem_buf((const void *)request.data(), (int)request.size());
		if(reqBio)
		{
			X509_REQ *req = PEM_read_bio_X509_REQ(reqBio, NULL, NULL, NULL);
			if(req)
			{
				EVP_PKEY *publicKey = X509_REQ_get_pubkey(req);//new reference
				if(publicKey)
				{
					if(X509_REQ_verify(req, publicKey) == 1)//-1 means error, not success
					{
						X509_NAME *name = X509_REQ_get_subject_name(req);
						//pass the days as days: days * 86400 overflows int above ~24855 days
						bool ready = X509_set_pubkey(cert, publicKey)
						&& X509_set_version(cert, X509_VERSION_3)
						&& ASN1_INTEGER_set(X509_get_serialNumber(cert), 1)
						&& (X509_gmtime_adj(X509_getm_notBefore(cert), 0) != NULL)
						&& (X509_time_adj_ex(X509_getm_notAfter(cert), days, 0, NULL) != NULL)
						&& X509_set_subject_name(cert, name)
						&& X509_set_issuer_name(cert, name);

						BIO *privKeyBio = BIO_new_mem_buf((const void *)privateKey.data(), (int)privateKey.size());
						if(privKeyBio)
						{
							EVP_PKEY *privKey = PEM_read_bio_PrivateKey(privKeyBio, NULL, NULL, NULL);
							if(privKey)
							{
								//don't return an unsigned certificate
								if(ready && (X509_sign(cert, privKey, cert_digest(algo)) > 0))
								{
									BIO *certBio = BIO_new(BIO_s_mem());
									if(certBio)
									{
										std::string certStr;
										if(PEM_write_bio_X509(certBio, cert) && cert_bio_to_string(certBio, certStr))
										{
											certificate = certStr;
											ok = true;
										}
										BIO_free(certBio);
									}
								}
								EVP_PKEY_free(privKey);
							}
							BIO_free(privKeyBio);
						}
					}
					EVP_PKEY_free(publicKey);
				}
				X509_REQ_free(req);
			}
			BIO_free(reqBio);
		}
		X509_free(cert);
	}
	ERR_clear_error();
	return ok;
}

// certificate and private key (any key type) out of PKCS#12 data.
// Returns false if the data can't be decoded with the password; otherwise
// certificate/privateKey are set when the file holds one.
static bool cert_p12_to_pem(const void *data,
														size_t length,
														const std::string &password,
														std::string &certificate,
														std::string &privateKey)
{
	bool ok = false;
	if(data && length && (length <= 0x7FFFFFFF))
	{
		BIO *bio = BIO_new_mem_buf(data, (int)length);
		if(bio)
		{
			PKCS12 *p12 = d2i_PKCS12_bio(bio, NULL);
			if(p12)
			{
				EVP_PKEY *key = NULL;
				X509 *cert = NULL;
				STACK_OF(X509) *ca = NULL;
				if(PKCS12_parse(p12, password.c_str(), &key, &cert, &ca))
				{
					ok = true;
					//a PKCS#12 file may hold no certificate or no key
					if(cert)
					{
						BIO *x509 = BIO_new(BIO_s_mem());
						if(x509)
						{
							std::string certStr;
							if(PEM_write_bio_X509(x509, cert) && cert_bio_to_string(x509, certStr))
							{
								certificate = certStr;
							}
							BIO_free(x509);
						}
					}
					if(key)
					{
						std::string keyStr;
						if(cert_write_private_key(key, keyStr))
						{
							privateKey = keyStr;
						}
					}
					//on success the caller owns key, cert and ca; on failure
					//OpenSSL 3 frees them and resets the pointers to NULL
				}
				EVP_PKEY_free(key);
				X509_free(cert);
				sk_X509_pop_free(ca, X509_free);
				PKCS12_free(p12);
			}
			BIO_free(bio);
		}
	}
	ERR_clear_error();
	return ok;
}

#endif /* CERTIFICATE_CORE_H */
