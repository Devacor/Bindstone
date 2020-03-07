/* com_wolfssl_WolfSSL.c
 *
 * Copyright (C) 2006-2018 wolfSSL Inc.
 *
 * This file is part of wolfSSL.
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdio.h>
#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#include <wolfssl/error-ssl.h>
#include <wolfssl/wolfcrypt/logging.h>
#include <wolfssl/wolfcrypt/hmac.h>

#include "com_wolfssl_globals.h"
#include "com_wolfssl_WolfSSL.h"

/* global JavaVM reference for JNIEnv lookup */
JavaVM*  g_vm;

/* global object refs for logging callbacks */
static jobject g_loggingCbIfaceObj;

/* custom native fn prototypes */
void NativeLoggingCallback(const int logLevel, const char *const logMessage);

/* called when native library is loaded */
jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    /* store JavaVM */
    g_vm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_init
  (JNIEnv* jenv, jobject jcl)
{
    return (jint)wolfSSL_Init();
}

/* used in unit tests */
JNIEXPORT void JNICALL Java_com_wolfssl_WolfSSL_nativeFree
  (JNIEnv* jenv, jobject jcl, jlong ptr)
{
    if((void*)ptr)
        free((void*)ptr);
}

/* functions to return BulkCipherAlgorithm enum values from ./wolfssl/ssl.h  */
JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumNULL
  (JNIEnv* jenv, jclass jcl)
{
    return wolfssl_cipher_null;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumRC4
  (JNIEnv* jenv, jclass jcl)
{
    return wolfssl_rc4;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumRC2
  (JNIEnv* jenv, jclass jcl)
{
    return wolfssl_rc2;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumDES
  (JNIEnv* jenv, jclass jcl)
{
    return wolfssl_des;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnum3DES
  (JNIEnv* jenv, jclass jcl)
{
    return wolfssl_triple_des;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumDES40
  (JNIEnv* jenv, jclass jcl)
{
    return wolfssl_des40;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumIDEA
  (JNIEnv* jenv, jclass jcl)
{
#ifdef HAVE_IDEA
    return wolfssl_idea;
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumAES
  (JNIEnv* jenv, jclass jcl)
{
    return wolfssl_aes;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumAESGCM
  (JNIEnv* jenv, jclass jcl)
{
    return wolfssl_aes_gcm;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumAESCCM
  (JNIEnv* jenv, jclass jcl)
{
    return wolfssl_aes_ccm;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumCHACHA
  (JNIEnv* jenv, jclass jcl)
{
    return wolfssl_chacha;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumCAMELLIA
  (JNIEnv* jenv, jclass jcl)
{
    return wolfssl_camellia;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumHC128
  (JNIEnv* jenv, jclass jcl)
{
    return wolfssl_hc128;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumRABBIT
  (JNIEnv* jenv, jclass jcl)
{
    return wolfssl_rabbit;
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_SSLv3_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
#if defined(WOLFSSL_ALLOW_SSLV3) && !defined(NO_OLD_TLS)
    return (jlong)wolfSSLv3_server_method();
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_SSLv3_1ClientMethod
  (JNIEnv* jenv, jclass jcl)
{
#if defined(WOLFSSL_ALLOW_SSLV3) && !defined(NO_OLD_TLS)
    return (jlong)wolfSSLv3_client_method();
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
#if !defined(NO_OLD_TLS) && defined(WOLFSSL_ALLOW_TLSV10)
    return (jlong)wolfTLSv1_server_method();
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_1ClientMethod
  (JNIEnv* jenv, jclass jcl)
{
#if !defined(NO_OLD_TLS) && defined(WOLFSSL_ALLOW_TLSV10)
    return (jlong)wolfTLSv1_client_method();
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_11_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
    return (jlong)wolfTLSv1_1_server_method();
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_11_1ClientMethod
  (JNIEnv* jenv, jclass jcl)
{
    return (jlong)wolfTLSv1_1_client_method();
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_12_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
    return (jlong)wolfTLSv1_2_server_method();
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_12_1ClientMethod(
    JNIEnv* jenv, jclass jcl)
{
    return (jlong)wolfTLSv1_2_client_method();
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_DTLSv1_1ClientMethod
  (JNIEnv* jenv, jclass jcl)
{
#ifdef WOLFSSL_DTLS
    return (jlong)wolfDTLSv1_client_method();

#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_DTLSv1_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
#ifdef WOLFSSL_DTLS
    return (jlong)wolfDTLSv1_server_method();
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_DTLSv1_12_1ClientMethod
  (JNIEnv* jenv, jclass jcl)
{
#ifdef WOLFSSL_DTLS
    return (jlong)wolfDTLSv1_2_client_method();
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_DTLSv1_12_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
#ifdef WOLFSSL_DTLS
    return (jlong)wolfDTLSv1_2_server_method();
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_SSLv23_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
    return (jlong)wolfSSLv23_server_method();
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_SSLv23_1ClientMethod
  (JNIEnv* jenv, jclass jcl)
{
    return (jlong)wolfSSLv23_client_method();
}

JNIEXPORT jstring JNICALL Java_com_wolfssl_WolfSSL_getErrorString
  (JNIEnv* jenv, jclass jcl, jlong errNumber)
{
    char buffer[WOLFSSL_MAX_ERROR_SZ];
    jstring retString;

    wolfSSL_ERR_error_string(errNumber, buffer);
    retString = (*jenv)->NewStringUTF(jenv, buffer);

    return retString;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_cleanup
  (JNIEnv* jenv, jclass jcl)
{
    return wolfSSL_Cleanup();
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_debuggingON
  (JNIEnv* jenv, jclass jcl)
{
    return wolfSSL_Debugging_ON();
}

JNIEXPORT void JNICALL Java_com_wolfssl_WolfSSL_debuggingOFF
  (JNIEnv* jenv, jclass jcl)
{
    return wolfSSL_Debugging_OFF();
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_setLoggingCb
  (JNIEnv* jenv, jclass jcl, jobject callback)
{
    int ret = 0;

    if (!jenv || !callback) {
        return BAD_FUNC_ARG;
    }

    /* store Java logging callback Interface object */
    g_loggingCbIfaceObj = (*jenv)->NewGlobalRef(jenv, callback);
    if (!g_loggingCbIfaceObj) {
        printf("error storing global logging callback interface\n");
        return SSL_FAILURE;
    }

    ret = wolfSSL_SetLoggingCb(NativeLoggingCallback);

    return ret;
}

void NativeLoggingCallback(const int logLevel, const char *const logMessage)
{
    JNIEnv*   jenv;
    jint      vmret  = 0;
    jclass    excClass;
    jmethodID logMethod;
    jobjectRefType refcheck;

    /* get JNIEnv from JavaVM */
    vmret = (int)((*g_vm)->GetEnv(g_vm, (void**) &jenv, JNI_VERSION_1_6));
    if (vmret == JNI_EDETACHED) {
#ifdef __ANDROID__
        vmret = (*g_vm)->AttachCurrentThread(g_vm, &jenv, NULL);
#else
        vmret = (*g_vm)->AttachCurrentThread(g_vm, (void**) &jenv, NULL);
#endif
        if (vmret) {
            printf("Failed to attach JNIEnv to thread\n");
        }
    } else if (vmret != JNI_OK) {
        printf("Unable to get JNIEnv from JavaVM\n");
    }

    /* find exception class */
    excClass = (*jenv)->FindClass(jenv, "java/lang/Exception");
    if ((*jenv)->ExceptionOccurred(jenv)) {
        (*jenv)->ExceptionDescribe(jenv);
        (*jenv)->ExceptionClear(jenv);
        return;
    }

    /* check if our stored object reference is valid */
    refcheck = (*jenv)->GetObjectRefType(jenv, g_loggingCbIfaceObj);
    if (refcheck == 2) {

        /* lookup WolfSSLLoggingCallback class from global object ref */
        jclass logClass = (*jenv)->GetObjectClass(jenv, g_loggingCbIfaceObj);
        if (!logClass) {
            if ((*jenv)->ExceptionOccurred(jenv)) {
                (*jenv)->ExceptionDescribe(jenv);
                (*jenv)->ExceptionClear(jenv);
            }

            (*jenv)->ThrowNew(jenv, excClass,
                "Can't get native WolfSSLLoggingCallback class reference");
            return;
        }

        logMethod = (*jenv)->GetMethodID(jenv, logClass,
                                            "loggingCallback",
                                            "(ILjava/lang/String;)V");
        if (logMethod == 0) {
            if ((*jenv)->ExceptionOccurred(jenv)) {
                (*jenv)->ExceptionDescribe(jenv);
                (*jenv)->ExceptionClear(jenv);
            }
            (*jenv)->ThrowNew(jenv, excClass,
                "Error getting loggingCallback method from JNI");
            return;
        }

        /* create jstring from char* */
        jstring logMsg = (*jenv)->NewStringUTF(jenv, logMessage);

        (*jenv)->CallVoidMethod(jenv, g_loggingCbIfaceObj, logMethod,
                logLevel, logMsg);

        if ((*jenv)->ExceptionOccurred(jenv)) {
            (*jenv)->ExceptionDescribe(jenv);
            (*jenv)->ExceptionClear(jenv);

            (*jenv)->ThrowNew(jenv, excClass,
                    "Error calling logging callback from JNI");
            return;
        }

    } else {
        if ((*jenv)->ExceptionOccurred(jenv)) {
            (*jenv)->ExceptionDescribe(jenv);
            (*jenv)->ExceptionClear(jenv);
        }

        (*jenv)->ThrowNew(jenv, excClass,
                "Object reference invalid in NativeLoggingCallback");
    }
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_memsaveSessionCache
  (JNIEnv* jenv, jclass jcl, jbyteArray mem, jint sz)
{
#ifdef PERSIST_SESSION_CACHE
    int ret;
    int cacheSz;
    char memBuf[sz];

    if (!jenv || !mem || (sz <= 0))
        return BAD_FUNC_ARG;

    ret = wolfSSL_memsave_session_cache(memBuf, sz);

    /* how much data do we need to write? */
    cacheSz = wolfSSL_get_session_cache_memsize();

    /* set jbyteArray for return */
    if (cacheSz >= 0) {
        (*jenv)->SetByteArrayRegion(jenv, mem, 0, cacheSz, (jbyte*)memBuf);
        if ((*jenv)->ExceptionOccurred(jenv)) {
            (*jenv)->ExceptionDescribe(jenv);
            (*jenv)->ExceptionClear(jenv);
            return SSL_FAILURE;
        }
    }

    return ret;
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_memrestoreSessionCache
  (JNIEnv* jenv, jclass jcl, jbyteArray mem, jint sz)
{
#ifdef PERSIST_SESSION_CACHE
    int ret;
    char memBuf[sz];

    if (!jenv || !mem || (sz <= 0))
        return BAD_FUNC_ARG;

    (*jenv)->GetByteArrayRegion(jenv, mem, 0, sz, (jbyte*)memBuf);
    if ((*jenv)->ExceptionOccurred(jenv)) {
        (*jenv)->ExceptionDescribe(jenv);
        (*jenv)->ExceptionClear(jenv);
        return SSL_FAILURE;
    }

    ret = wolfSSL_memrestore_session_cache(memBuf, sz);
    return ret;
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getSessionCacheMemsize
  (JNIEnv* jenv, jclass jcl)
{
#ifdef PERSIST_SESSION_CACHE
    return wolfSSL_get_session_cache_memsize();
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jbyteArray JNICALL Java_com_wolfssl_WolfSSL_x509_1getDer
  (JNIEnv* jenv, jclass jcl, jlong x509)
{
#if defined(KEEP_PEER_CERT) || defined(SESSION_CERTS)
    int* outSz = NULL;
    const unsigned char* derCert;
    jbyteArray out = NULL;

    if (!jenv || !x509)
        return NULL;

    derCert = wolfSSL_X509_get_der((WOLFSSL_X509*)x509, outSz);

    if (outSz >= 0) {

        (*jenv)->SetByteArrayRegion(jenv, out, 0, *outSz, (jbyte*)derCert);
        if ((*jenv)->ExceptionOccurred(jenv)) {
            (*jenv)->ExceptionDescribe(jenv);
            (*jenv)->ExceptionClear(jenv);
            return NULL;
        }
        return out;

    } else {
        return NULL;
    }
#else
    return NULL;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getHmacMaxSize
  (JNIEnv* jenv, jclass jcl)
{
    return MAX_DIGEST_SIZE;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledCRL
  (JNIEnv* jenv, jclass jcl)
{
#ifdef HAVE_CRL
    return 1;
#else
    return 0;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledCRLMonitor
  (JNIEnv* jenv, jclass jcl)
{
#ifdef HAVE_CRL_MONITOR
    return 1;
#else
    return 0;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledOCSP
  (JNIEnv* jenv, jclass jcl)
{
#ifdef HAVE_OCSP
    return 1;
#else
    return 0;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledPSK
  (JNIEnv* jenv, jclass jcl)
{
#ifndef NO_PSK
    return 1;
#else
    return 0;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledDTLS
  (JNIEnv* jenv, jclass jcl)
{
#ifdef WOLFSSL_DTLS
    return 1;
#else
    return 0;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledAtomicUser
  (JNIEnv* jenv, jclass jcl)
{
#ifdef ATOMIC_USER
    return 1;
#else
    return 0;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledPKCallbacks
  (JNIEnv* jenv, jclass jcl)
{
#ifdef HAVE_PK_CALLBACKS
    return 1;
#else
    return 0;
#endif
}

