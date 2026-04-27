#pragma once
#include <string>
#include <cstring>

class AuthToken {
public:
    static std::string generate() {
        unsigned char bytes[32];
#ifdef __APPLE__
        arc4random_buf(bytes, sizeof(bytes));
#else
        FILE* f = fopen("/dev/urandom", "rb");
        if (f) { fread(bytes, 1, sizeof(bytes), f); fclose(f); }
#endif
        return base64url(bytes, sizeof(bytes));
    }

    static bool validate(const std::string& provided, const std::string& expected) {
        if (provided.size() != expected.size()) return false;
        volatile unsigned char result = 0;
        for (size_t i = 0; i < provided.size(); i++)
            result |= provided[i] ^ expected[i];
        return result == 0;
    }

    // Expose for testing
    static std::string base64url_public(const unsigned char* data, size_t len) {
        return base64url(data, len);
    }

private:
    static std::string base64url(const unsigned char* data, size_t len) {
        static const char table[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        std::string out;
        out.reserve((len * 4 + 2) / 3);
        for (size_t i = 0; i < len; i += 3) {
            uint32_t n = (uint32_t)data[i] << 16;
            if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
            if (i + 2 < len) n |= (uint32_t)data[i + 2];
            out += table[(n >> 18) & 0x3F];
            out += table[(n >> 12) & 0x3F];
            if (i + 1 < len) out += table[(n >> 6) & 0x3F];
            if (i + 2 < len) out += table[n & 0x3F];
        }
        return out;
    }
};
