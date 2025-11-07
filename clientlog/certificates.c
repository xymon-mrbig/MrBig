#include "clientlog.h"
#include <wincrypt.h>

#define CERTIFICATES_ROW_SIZE (512)

typedef struct {
    DWORD PublicKeySize;
    CHAR *Subject;
    CHAR *FriendlyName;
    CHAR *Issuer;
    CHAR **EKUPurposes;
    FILETIME NotBefore;
    FILETIME NotAfter;
    CHAR *StoreLocation;
    CHAR *SignatureAlgorithm;
    CHAR *Fingerprint;
    CHAR *SerialNumber;
} Certificate;

LPSTR certificates_PrettyEKUPurposes(CHAR **eku, LPSTR out, size_t outSize) {
    if (eku != NULL) {
        DWORD i = 0, written = 0;
        while (eku[i] != NULL) {
            if (i != 0 && outSize - written > 1) {
                out[written++] = ',';
            }
            written += snprintf(&out[written], outSize - written, "%s", eku[i++]);
        }
    } else {
        snprintf(out, outSize, "None");
    }
    return out;
}

LPSTR certificates_PrettyCertificate(Certificate *c, LPSTR out) {
    CHAR eku[64], validFromBuf[64], validToBuf[64];
    SYSTEMTIME validFrom, validTo;
    FileTimeToSystemTime(&c->NotBefore, &validFrom);
    FileTimeToSystemTime(&c->NotAfter, &validTo);
    snprintf(out, CERTIFICATES_ROW_SIZE,
             "\nFriendly Name:      \t%s"
             "\nValid from:         \t%s"
             "\nValid to:           \t%s"
             "\nStore location:     \t%s"
             "\nSubject RDN:        \t%s"
             "\nIssuer RDN:         \t%s"
             "\nIntended purposes:  \t%s"
             "\nKey Size:           \t%lu"
             "\nSignature Algorithm:\t%s"
             "\nFinger Print:       \t%s"
             "\nSerial Number:      \t%s",
             c->FriendlyName,
             clog_utils_PrettySystemtime(&validFrom, clog_utils_TIMESTAMP_DATETIME, validFromBuf, 64),
             clog_utils_PrettySystemtime(&validTo, clog_utils_TIMESTAMP_DATETIME, validToBuf, 64),
             c->StoreLocation,
             c->Subject,
             c->Issuer,
             certificates_PrettyEKUPurposes(c->EKUPurposes, eku, sizeof(eku)),
             c->PublicKeySize,
             c->SignatureAlgorithm,
             c->Fingerprint,
             c->SerialNumber);
    return out;
}

size_t certificates_GetOIDName(CHAR *oID, CHAR *out, size_t outSize) {
    size_t written = 0;
    const CRYPT_OID_INFO *oIDInfo = CryptFindOIDInfo(CRYPT_OID_INFO_OID_KEY, oID, CRYPT_OID_DISABLE_SEARCH_DS_FLAG);
    if (!oIDInfo) {
        // Unknown OID;
        out[0] = '\0';
        return written;
    }

    LOG_DEBUG("\tcertificates.c: \t\tLookup of OID %s successful, getting readable name.", oID);
    written = wcstombs(out, oIDInfo->pwszName, outSize - 1);
    if (written > outSize || written <= 0) {
        LOG_DEBUG("\tcertificates.c: \t\tUnreadable character in name.", oID);
        written = 0;
    }

    out[written] = '\0';
    return written;
}

CHAR **certificates_GetEKUPurposes(CERT_INFO *ctx, clog_Arena *a) {
    CHAR **res = NULL;
    for (DWORD i = 0; i < ctx->cExtension; i++) {
        CERT_EXTENSION *ext = &ctx->rgExtension[i];
        if (strcmp(ext->pszObjId, szOID_ENHANCED_KEY_USAGE) != 0) continue;

        LOG_DEBUG("\tcertificates.c: \t\tFound EKU purposes OID, decrypting object.");
        DWORD decodedSize = 0;
        CryptDecodeObjectEx(X509_ASN_ENCODING, szOID_ENHANCED_KEY_USAGE, ext->Value.pbData, ext->Value.cbData, CRYPT_DECODE_NOCOPY_FLAG, NULL, NULL, &decodedSize);

        BYTE decodedData[decodedSize];
        WINBOOL success = CryptDecodeObjectEx(X509_ASN_ENCODING, szOID_ENHANCED_KEY_USAGE, ext->Value.pbData, ext->Value.cbData, CRYPT_DECODE_NOCOPY_FLAG, NULL, decodedData, &decodedSize);

        if (!success) {
            res = clog_ArenaAlloc(a, CHAR *, 2);
            res[0] = clog_ArenaAlloc(a, CHAR, 32);
            sprintf(res[0], "<Error with code %#010lx>", GetLastError());
            res[1] = NULL;
            break;
        }
        CTL_USAGE *p = ((CTL_USAGE *)decodedData);
        res = clog_ArenaAlloc(a, CHAR *, p->cUsageIdentifier + 1);
        for (DWORD j = 0; j < p->cUsageIdentifier; j++) {
            CHAR *oID = p->rgpszUsageIdentifier[j];
            CHAR purposeBuf[128];
            LOG_DEBUG("\tcertificates.c: \t\tGetting OID info %lu.", j);
            size_t purposeLen = certificates_GetOIDName(oID, purposeBuf, 128);

            if (purposeLen <= 0) {
                purposeLen = sprintf(purposeBuf, "Unknown purpose (%s)", oID);
            }

            CHAR *purpose = clog_ArenaAlloc(a, CHAR, purposeLen + 1);
            memcpy(purpose, purposeBuf, purposeLen);
            purpose[purposeLen] = '\0';
            res[j] = purpose;
            LOG_DEBUG("\tcertificates.c: \t\tOID info %lu = '%s'", j, purpose);
        }
        res[p->cUsageIdentifier] = NULL;
        break;
    }
    return res;
}

WINBOOL CertCloseStoreWrapper(HCERTSTORE h) {
    return CertCloseStore(h, 0);
}

void clog_certificates(clog_Arena scratch) {
    const LPSTR storeLocation = "MY";

    clog_ArenaAppend(&scratch, "[certificates]");

    LOG_DEBUG("\tcertificates.c: Opening certificate store.");
    HCERTSTORE hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM_A, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_READONLY_FLAG, "My");
    clog_Defer(&scratch, hStore, RETURN_INT, &CertCloseStoreWrapper);

    // HCERTSTORE hStore = CertOpenSystemStoreA(0, storeLocation);
    // CertControlStore(hStore, 0, CERT_STORE_CTRL_AUTO_RESYNC, NULL);

    const CERT_CONTEXT *ctx = NULL;
    CHAR certificateBuf[CERTIFICATES_ROW_SIZE];
    DWORD numCertificates = 0;

    while ((ctx = CertEnumCertificatesInStore(hStore, ctx))) {
        LOG_DEBUG("\tcertificates.c: Start of certificate enumeration.");

        clog_Defer(&scratch, (void *)ctx, RETURN_INT, &CertFreeCertificateContext);
        numCertificates++;
        Certificate c = {0};

        // Friendly Name
        DWORD getNameStringType = CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG;
        LOG_DEBUG("\tcertificates.c: \tGetting RDN friendly name.");
        DWORD friendlySize = CertGetNameStringA(ctx, CERT_NAME_FRIENDLY_DISPLAY_TYPE, CERT_NAME_DISABLE_IE4_UTF8_FLAG, &getNameStringType, NULL, 0);
        c.FriendlyName = clog_ArenaAlloc(&scratch, void, friendlySize);
        LOG_DEBUG("\tcertificates.c: \tGetting RDN friendly name.");
        CertGetNameStringA(ctx, CERT_NAME_FRIENDLY_DISPLAY_TYPE, CERT_NAME_DISABLE_IE4_UTF8_FLAG, &getNameStringType, c.FriendlyName, friendlySize);
        LOG_DEBUG("\tcertificates.c: \tRDN friendly name = '%s'.", c.FriendlyName);

        // Subject
        getNameStringType = CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG;
        LOG_DEBUG("\tcertificates.c: \tGetting subject RDN size.");
        DWORD subjectSize = CertGetNameStringA(ctx, CERT_NAME_RDN_TYPE, CERT_NAME_DISABLE_IE4_UTF8_FLAG, &getNameStringType, NULL, 0);
        c.Subject = clog_ArenaAlloc(&scratch, void, subjectSize);
        LOG_DEBUG("\tcertificates.c: \tGetting subject RDN.");
        CertGetNameString(ctx, CERT_NAME_RDN_TYPE, CERT_NAME_DISABLE_IE4_UTF8_FLAG, &getNameStringType, c.Subject, subjectSize);
        LOG_DEBUG("\tcertificates.c: \tSubject RDN = '%s'.", c.Subject);

        // Issued By
        getNameStringType = CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG;
        LOG_DEBUG("\tcertificates.c: \tGetting issuer RDN size.");
        DWORD issuerSize = CertGetNameStringA(ctx, CERT_NAME_RDN_TYPE, CERT_NAME_ISSUER_FLAG | CERT_NAME_DISABLE_IE4_UTF8_FLAG, &getNameStringType, NULL, 0);
        c.Issuer = clog_ArenaAlloc(&scratch, void, issuerSize);
        LOG_DEBUG("\tcertificates.c: \tGetting issuer RD.");
        CertGetNameString(ctx, CERT_NAME_RDN_TYPE, CERT_NAME_DISABLE_IE4_UTF8_FLAG, &getNameStringType, c.Issuer, issuerSize);
        LOG_DEBUG("\tcertificates.c: \tIssuer RDN = '%s'.", c.Issuer);

        // Intended Purposes
        LOG_DEBUG("\tcertificates.c: \tGetting cert EKU purposes.");
        c.EKUPurposes = certificates_GetEKUPurposes(ctx->pCertInfo, &scratch);

        // Signature Algorithm
        CHAR algoBuf[64];
        size_t algoLen = certificates_GetOIDName(ctx->pCertInfo->SignatureAlgorithm.pszObjId, algoBuf, 64);
        if (algoLen <= 0)
            c.SignatureAlgorithm = "<unknown>";
        else {
            CHAR *algo = clog_ArenaAlloc(&scratch, CHAR, algoLen + 1);
            memcpy(algo, algoBuf, algoLen);
            algo[algoLen] = '\0';
            c.SignatureAlgorithm = algo;
        }

        // Fingerprint
        BYTE fingerprintHash[64] = {0};
        DWORD fingeprintSize = sizeof(fingerprintHash);
        CHAR fingerprintHex[128] = "";
        if (CertGetCertificateContextProperty(ctx, CERT_HASH_PROP_ID, fingerprintHash, &fingeprintSize)) {
            size_t written = 0;
            for (int i = 0; i < fingeprintSize; i++) {
                written += sprintf(&fingerprintHex[written], "%02x", fingerprintHash[i]);
            }
        }
        c.Fingerprint = fingerprintHex;

        // Serial Number
        CHAR serialHex[128] = "";
        size_t serialWritten = 0;
        for (int i = ctx->pCertInfo->SerialNumber.cbData - 1; i >= 0; i--) {
            serialWritten += sprintf(&serialHex[serialWritten], "%02x", ctx->pCertInfo->SerialNumber.pbData[i]);
        }
        c.SerialNumber = serialHex;

        // rest
        LOG_DEBUG("\tcertificates.c: \tGetting other data.");
        c.NotBefore = ctx->pCertInfo->NotBefore;
        c.NotAfter = ctx->pCertInfo->NotAfter;
        c.StoreLocation = "Local Machine";
        c.PublicKeySize = CertGetPublicKeyLength(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, &ctx->pCertInfo->SubjectPublicKeyInfo);

        LOG_DEBUG("\tcertificates.c: \tPrinting cert to buffer.");
        clog_ArenaAppend(&scratch, "%s\n", certificates_PrettyCertificate(&c, certificateBuf));
        LOG_DEBUG("\tcertificates.c: \tPrinted cert to buffer.");
        clog_IgnorePopDefer(&scratch);
    }
    if (numCertificates == 0) {
        clog_ArenaAppend(&scratch, "\n(No certificates found in store '%s')", storeLocation);
    }
    LOG_DEBUG("\tcertificates.c: End.");
}

#ifdef STANDALONE
int main(int argc, TCHAR *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(0x10000);
    clog_certificates(st->Memory);
    printf("%s", st->Start);
    return 0;
}
#endif