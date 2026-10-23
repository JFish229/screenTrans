#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <string>
#pragma comment(lib,"bcrypt.lib")
namespace asr_trace {
inline std::string sha256(const std::filesystem::path& path) {
    struct Handles {
        BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
        ~Handles() { if(hash) BCryptDestroyHash(hash); if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0); }
    } h;
    std::ifstream file(path,std::ios::binary);
    if(!file || BCryptOpenAlgorithmProvider(&h.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 || BCryptCreateHash(h.algorithm,&h.hash,nullptr,0,nullptr,0,0)<0) return "unavailable";
    unsigned char block[65536],digest[32];
    while(file) { file.read(reinterpret_cast<char*>(block),sizeof(block)); if(file.gcount() && BCryptHashData(h.hash,block,ULONG(file.gcount()),0)<0) return "unavailable"; }
    if(!file.eof() || BCryptFinishHash(h.hash,digest,sizeof(digest),0)<0) return "unavailable";
    std::string result;const char* hex="0123456789abcdef";for(auto c:digest){result+=hex[c>>4];result+=hex[c&15];}return result;
}
}
