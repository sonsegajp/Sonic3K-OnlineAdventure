#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <vector>
static bool official_image(HMODULE exe) {
    wchar_t path[32768];if(!GetModuleFileNameW(exe,path,32768))return false;
    FILE *f=_wfopen(path,L"rb");if(!f)return false;
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;unsigned char digest[32];bool ok=false;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0){fclose(f);return false;}
    if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0) {
        unsigned char buf[65536];size_t n;ok=true;
        while((n=fread(buf,1,sizeof buf,f)))if(BCryptHashData(hash,buf,(ULONG)n,0)<0){ok=false;break;}
        ok=ok&&!ferror(f)&&BCryptFinishHash(hash,digest,sizeof digest,0)>=0;
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg,0);fclose(f);
    static const unsigned char expected[]={0x39,0x0d,0x3a,0x34,0x48,0xa7,0x0a,0xfa,0xbd,0x0e,0x0b,0x04,0xe7,0x03,0xa2,0xa2,0x99,0xfa,0x96,0x1f,0xc4,0x18,0x2b,0x8e,0x73,0x39,0x6b,0x33,0x77,0x18,0x5d,0xfd};
    return ok&&!memcmp(digest,expected,sizeof expected);
}
