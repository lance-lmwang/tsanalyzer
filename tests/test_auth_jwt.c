#include <assert.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mongoose.h"
#include "tsa_auth.h"

/* Helper to encode base64url */
static void b64url_encode(const unsigned char* in, int in_len, char* out) {
    static const char* b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    int i, j;
    for (i = 0, j = 0; i < in_len; i += 3) {
        int v = in[i] << 16;
        if (i + 1 < in_len) v |= in[i + 1] << 8;
        if (i + 2 < in_len) v |= in[i + 2];

        out[j++] = b64chars[(v >> 18) & 0x3F];
        out[j++] = b64chars[(v >> 12) & 0x3F];
        if (i + 1 < in_len) out[j++] = b64chars[(v >> 6) & 0x3F];
        if (i + 2 < in_len) out[j++] = b64chars[v & 0x3F];
    }
    out[j] = '\0';
}

static char* create_jwt(const char* secret, const char* header_b64, const char* payload_b64) {
    char data[1024];
    snprintf(data, sizeof(data), "%s.%s", header_b64, payload_b64);

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    HMAC(EVP_sha256(), secret, strlen(secret), (unsigned char*)data, strlen(data), hash, &hash_len);

    char sig_b64[128];
    b64url_encode(hash, hash_len, sig_b64);

    char* jwt = malloc(2048);
    snprintf(jwt, 2048, "%s.%s", data, sig_b64);
    return jwt;
}

void test_jwt_success() {
    printf("Testing JWT success path...\n");
    const char* secret = "test-secret-key-123";
    setenv("TSA_API_SECRET", secret, 1);
    tsa_auth_init(NULL);

    const char* header = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9";  // {"alg":"HS256","typ":"JWT"}
    const char* payload = "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ";
    char* jwt = create_jwt(secret, header, payload);

    struct mg_http_message hm;
    memset(&hm, 0, sizeof(hm));
    char auth_header[2100];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", jwt);
    hm.headers[0].name = mg_str("Authorization");
    hm.headers[0].value = mg_str(auth_header);

    assert(tsa_auth_verify_request(&hm) == true);
    printf("JWT success path passed!\n");
    free(jwt);
}

void test_jwt_failure_tampered() {
    printf("Testing JWT tampered failure...\n");
    const char* secret = "test-secret-key-123";
    setenv("TSA_API_SECRET", secret, 1);
    tsa_auth_init(NULL);

    const char* header = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9";
    const char* payload = "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ";
    char* jwt = create_jwt(secret, header, payload);

    // Tamper the payload
    jwt[40] = (jwt[40] == 'A') ? 'B' : 'A';

    struct mg_http_message hm;
    memset(&hm, 0, sizeof(hm));
    char auth_header[2100];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", jwt);
    hm.headers[0].name = mg_str("Authorization");
    hm.headers[0].value = mg_str(auth_header);

    assert(tsa_auth_verify_request(&hm) == false);
    printf("JWT tampered failure passed!\n");
    free(jwt);
}

int main() {
    test_jwt_success();
    test_jwt_failure_tampered();
    return 0;
}
