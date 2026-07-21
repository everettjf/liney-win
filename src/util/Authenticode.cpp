#include "util/Authenticode.h"

#include <windows.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>

#include <vector>

namespace liney {

bool verifyAuthenticode(const std::wstring& path) {
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = path.c_str();

    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL |
                       WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = WinVerifyTrust(nullptr, &policy, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &data);
    return status == ERROR_SUCCESS;
}

namespace {
std::vector<BYTE> signerFingerprint(const std::wstring& path) {
    HCERTSTORE store = nullptr;
    HCRYPTMSG message = nullptr;
    DWORD encoding = 0, content = 0, format = 0;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, path.c_str(),
                          CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0, &encoding,
                          &content, &format, &store, &message, nullptr))
        return {};
    DWORD size = 0;
    std::vector<BYTE> fingerprint;
    if (CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &size)) {
        std::vector<BYTE> storage(size);
        if (CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0,
                             storage.data(), &size)) {
            const auto* signer =
                reinterpret_cast<const CMSG_SIGNER_INFO*>(storage.data());
            CERT_INFO lookup{};
            lookup.Issuer = signer->Issuer;
            lookup.SerialNumber = signer->SerialNumber;
            PCCERT_CONTEXT cert = CertFindCertificateInStore(
                store, encoding, 0, CERT_FIND_SUBJECT_CERT, &lookup, nullptr);
            if (cert) {
                DWORD hashSize = 0;
                if (CertGetCertificateContextProperty(
                        cert, CERT_SHA256_HASH_PROP_ID, nullptr, &hashSize)) {
                    fingerprint.resize(hashSize);
                    if (!CertGetCertificateContextProperty(
                            cert, CERT_SHA256_HASH_PROP_ID,
                            fingerprint.data(), &hashSize))
                        fingerprint.clear();
                }
                CertFreeCertificateContext(cert);
            }
        }
    }
    if (message) CryptMsgClose(message);
    if (store) CertCloseStore(store, 0);
    return fingerprint;
}
} // namespace

bool sameAuthenticodePublisher(const std::wstring& first,
                               const std::wstring& second) {
    if (!verifyAuthenticode(first) || !verifyAuthenticode(second)) return false;
    const std::vector<BYTE> a = signerFingerprint(first);
    const std::vector<BYTE> b = signerFingerprint(second);
    return !a.empty() && a == b;
}

} // namespace liney
