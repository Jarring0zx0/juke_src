#include "Blacklist.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>

#pragma comment(lib, "bcrypt.lib")

extern "C" void jukeOut(const char* s);

namespace Juke {

    static const std::vector<BlacklistEntry> s_Blacklist = {
        {
            "Mech V7",
            "c92ea97c663cea40caac4bccd62aa748be5a2c9e6067a09b37eb409223943b2c",
            "394cde8f28f5fffefd74c9656350cccea6497be777d900b187686432b5aa269e",
            "You just tried loading Mech V7. This bot is blacklisted due to being leaked without the owner's permission."
        },
        {
            "Brain",
            "c5fd011e990d40d50b6b00b590a79553b681e2cfa5155aa6b2c7e79d6e8997a8",
            "0c807d96f4cb3d4bdab99268a761aa872830c619dce8ccdba5a7ff1e9b6c17ee",
            "You just tried loading Brain. This bot is blacklisted due to being leaked without the owner's permission."
        }
    };

    static std::string CalculateSHA256(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) return "";

        std::ifstream file(path, std::ios::binary);
        if (!file) return "";

        BCRYPT_ALG_HANDLE hAlg = NULL;
        BCRYPT_HASH_HANDLE hHash = NULL;
        NTSTATUS status;
        std::string hashHex;

        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) >= 0) {
            DWORD hashObjSize = 0, dummy = 0;
            BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&hashObjSize, sizeof(DWORD), &dummy, 0);

            std::vector<BYTE> hashObj(hashObjSize);

            if (BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, NULL, 0, 0) >= 0) {
                char buffer[4096];
                while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
                    BCryptHashData(hHash, (PBYTE)buffer, (ULONG)file.gcount(), 0);
                    if (file.eof()) break;
                }

                DWORD hashSize = 0;
                BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&hashSize, sizeof(DWORD), &dummy, 0);
                std::vector<BYTE> hash(hashSize);
                BCryptFinishHash(hHash, hash.data(), hashSize, 0);

                std::stringstream ss;
                ss << std::hex << std::setfill('0');
                for (BYTE b : hash) {
                    ss << std::setw(2) << (int)b;
                }
                hashHex = ss.str();

                BCryptDestroyHash(hHash);
            }
            BCryptCloseAlgorithmProvider(hAlg, 0);
        }

        return hashHex;
    }

    bool CheckBlacklist(const std::filesystem::path& policyPath, const std::filesystem::path& sharedPath) {
        std::string pHash = CalculateSHA256(policyPath);
        std::string sHash = "";

        if (!sharedPath.empty() && std::filesystem::exists(sharedPath)) {
            sHash = CalculateSHA256(sharedPath);
        }

        for (const auto& entry : s_Blacklist) {
            bool pMatch = (pHash == entry.PolicyHash);
            bool sMatch = true;
            if (!entry.SharedHash.empty()) {
                sMatch = (sHash == entry.SharedHash);
            }

            if (pMatch && sMatch) {
                jukeOut("=================================================");
                jukeOut(entry.Message.c_str());
                jukeOut("=================================================");
                return true;
            }
        }

        return false;
    }
}
