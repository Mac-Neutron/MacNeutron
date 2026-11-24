// Text and TLS through Windows APIs (ship-base spec §5): each line exercises one of Wine's four dlopens of the
// bundled libraries. win32u's FreeType: a Tahoma font's metrics, a text extent and the dialog base units, plus
// Proton's key Windows-named font families when installed. dwrite's FreeType: the system font collection's family count
// and family enumeration. secur32's gnutls: an outbound schannel credential. crypt32's gnutls: a PFX import (a
// throwaway self-signed certificate, empty password). Usage: arm64-fonts-tls.exe <pfx path>.
#define COBJMACROS
#define INITGUID
#define SECURITY_WIN32
#include <windows.h>
#include <wincrypt.h>
#include <security.h>
#include <schannel.h>
#include <dwrite.h>
#include <stdio.h>

static const WCHAR *const proton_fonts[] = {
    L"Arial",
    L"Times New Roman",
    L"Georgia",
    L"Courier New",
    L"Microsoft YaHei",
    L"SimSun",
    L"Malgun Gothic",
    L"MS Gothic",
    L"Microsoft Sans Serif",
    L"Nirmala UI",
    L"Lucida Console",
};

int main(int argc, char **argv) {
  HDC dc = CreateCompatibleDC(NULL);
  HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Tahoma");
  TEXTMETRICW tm = {0};
  SIZE sz = {0};
  LONG dbu = GetDialogBaseUnits();
  IDWriteFactory *factory = NULL;
  IDWriteFontCollection *fonts = NULL;
  UINT32 families = 0;
  int proton_installed = 0;
  int proton_ok = 1;
  size_t proton_count = sizeof(proton_fonts) / sizeof(proton_fonts[0]);
  size_t proton_resolved = 0;
  CredHandle cred;
  TimeStamp expiry;
  SECURITY_STATUS st;
  HANDLE file;
  BYTE buf[16384];
  DWORD len = 0, certs = 0;
  CRYPT_DATA_BLOB blob;
  HCERTSTORE store;
  PCCERT_CONTEXT cert = NULL;
  setvbuf(stdout, NULL, _IONBF, 0);  // a crash keeps what was printed before it
  if (argc != 2) {
    printf("FAIL arm64-fonts-tls: usage: arm64-fonts-tls.exe <pfx path>\n");
    return 1;
  }

  SelectObject(dc, font);
  GetTextMetricsW(dc, &tm);
  GetTextExtentPoint32W(dc, L"Hello", 5, &sz);
  printf("font %ld %ldx%ld dbu %d,%d\n", tm.tmHeight, sz.cx, sz.cy, LOWORD(dbu), HIWORD(dbu));

  if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown **)&factory))
      && SUCCEEDED(IDWriteFactory_GetSystemFontCollection(factory, &fonts, FALSE)))
    families = IDWriteFontCollection_GetFontFamilyCount(fonts);
  printf("dwrite families %u\n", families);

  proton_installed = (GetFileAttributesW(L"C:\\windows\\Fonts\\msyh.ttf") != INVALID_FILE_ATTRIBUTES);
  for (size_t i = 0; i < proton_count; i++) {
    UINT32 idx = 0;
    BOOL exists = FALSE;
    if (fonts && SUCCEEDED(IDWriteFontCollection_FindFamilyName(fonts, proton_fonts[i], &idx, &exists)) && exists) {
      HFONT pf = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, proton_fonts[i]);
      TEXTMETRICW ptm = {0};
      SIZE psz = {0};
      WCHAR face[128] = L"";
      HGDIOBJ prev = pf ? SelectObject(dc, pf) : NULL;
      if (pf) {
        GetTextMetricsW(dc, &ptm);
        GetTextExtentPoint32W(dc, L"Hello", 5, &psz);
        GetTextFaceW(dc, 128, face);
        SelectObject(dc, prev);
        DeleteObject(pf);
      }
      if (!pf || ptm.tmHeight <= 0 || psz.cx <= 0 || psz.cy <= 0) {
        printf("FAIL proton-font %ls metrics %ld %ldx%ld\n", proton_fonts[i], ptm.tmHeight, psz.cx, psz.cy);
        proton_ok = 0;
      } else {
        proton_resolved++;
        printf("proton-font %ls height %ld extent %ldx%ld face %ls\n", proton_fonts[i], ptm.tmHeight, psz.cx, psz.cy,
               face);
      }
    } else if (proton_installed) {
      printf("FAIL proton-font %ls missing from DirectWrite collection\n", proton_fonts[i]);
      proton_ok = 0;
    }
  }
  printf("proton-fonts %zu/%zu resolved (installed=%d)\n", proton_resolved, proton_count, proton_installed);
  if (proton_installed && proton_resolved != proton_count) proton_ok = 0;

  st = AcquireCredentialsHandleW(NULL, (SEC_WCHAR *)UNISP_NAME_W, SECPKG_CRED_OUTBOUND, NULL, NULL, NULL, NULL, &cred,
                                 &expiry);
  printf("schannel: 0x%08lx\n", (unsigned long)st);
  if (st == SEC_E_OK) FreeCredentialsHandle(&cred);

  file = CreateFileA(argv[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (file != INVALID_HANDLE_VALUE) {
    ReadFile(file, buf, sizeof(buf), &len, NULL);
    CloseHandle(file);
  } else {
    printf("pfx: can't open %s: error %lu\n", argv[1], GetLastError());
  }
  blob.cbData = len;
  blob.pbData = buf;
  store = len ? PFXImportCertStore(&blob, L"", 0) : NULL;
  if (store) {
    while ((cert = CertEnumCertificatesInStore(store, cert))) certs++;
    CertCloseStore(store, 0);
  } else if (len) {
    printf("pfx: PFXImportCertStore: error 0x%08lx\n", GetLastError());
  }
  printf("pfx certs %lu\n", certs);

  if (tm.tmHeight > 0 && sz.cx > 0 && sz.cy > 0 && LOWORD(dbu) > 0 && HIWORD(dbu) > 0 && families > 0 && proton_ok
      && st == SEC_E_OK && certs == 1) {
    printf("PASS arm64-fonts-tls\n");
    return 0;
  }
  printf("FAIL arm64-fonts-tls\n");
  return 1;
}
