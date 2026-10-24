// A mount point's answers agree (Wine patch 0043): Wine calls a directory whose st_dev differs from its parent's a
// reparse point with IO_REPARSE_TAG_MOUNT_POINT, so FSCTL_GET_REPARSE_POINT on it has to answer that tag too, with a
// volume mount point's buffer (\??\Volume{GUID}\, no print name), as Windows does for a mounted volume. 3DMark's Steel
// Nomad stats each component of its path that way and stopped at the VM's games share (error 4390).
//   x64-mountpoint mount <path> | dir <path> ...  each pair: a mount point (Z:\System\Volumes\Preboot on macOS,
//                                                 Z:\mnt\share in the VM) or a plain directory, no reparse point
// One line per row, `ok <row>` or `FAIL <row>: <why>`, then PASS or FAIL x64-mountpoint.
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <wchar.h>

static int failed;

static void row(int ok, const WCHAR *path, const char *name, const char *why) {
  if (ok) {
    printf("ok %ls %s\n", path, name);
    return;
  }
  printf("FAIL %ls %s: %s\n", path, name, why);
  failed++;
}

// REPARSE_DATA_BUFFER's mount point form (ntifs.h, not in the SDK headers).
typedef struct {
  DWORD tag;
  WORD data_length, reserved, sub_offset, sub_length, print_offset, print_length;
  WCHAR path[1];
} mount_point_buffer;

static HANDLE open_dir(const WCHAR *path, DWORD flags) {
  return CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                     OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | flags, NULL);
}

// FSCTL_GET_REPARSE_POINT on a handle: the bytes returned, or -1 with GetLastError()'s code in *error.
static int get_reparse(HANDLE h, BYTE *buf, DWORD size, DWORD *error) {
  DWORD got = 0;
  *error = 0;
  if (DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, buf, size, &got, NULL)) return got;
  *error = GetLastError();
  return -1;
}

static void check_mount(const WCHAR *path) {
  static BYTE buf[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
  WIN32_FILE_ATTRIBUTE_DATA data;
  WIN32_FIND_DATAW find;
  FILE_ATTRIBUTE_TAG_INFO tag;
  DWORD error;
  char why[160];

  BOOL got = GetFileAttributesExW(path, GetFileExInfoStandard, &data);
  snprintf(why, sizeof(why), "attributes %#lx", got ? data.dwFileAttributes : GetLastError());
  row(got && (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ==
                 (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT),
      path, "attributes", why);

  HANDLE f = FindFirstFileW(path, &find);
  snprintf(why, sizeof(why), "find tag %#lx", f != INVALID_HANDLE_VALUE ? find.dwReserved0 : GetLastError());
  row(f != INVALID_HANDLE_VALUE && find.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT, path, "find", why);
  if (f != INVALID_HANDLE_VALUE) FindClose(f);

  HANDLE h = open_dir(path, FILE_FLAG_OPEN_REPARSE_POINT);
  snprintf(why, sizeof(why), "open error %lu", GetLastError());
  row(h != INVALID_HANDLE_VALUE, path, "open", why);
  if (h == INVALID_HANDLE_VALUE) return;
  got = GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag));
  snprintf(why, sizeof(why), "tag info %#lx", got ? tag.ReparseTag : GetLastError());
  row(got && tag.ReparseTag == IO_REPARSE_TAG_MOUNT_POINT, path, "taginfo", why);

  // Steel Nomad's call: the error it threw on was this one's 4390.
  int n = get_reparse(h, buf, sizeof(buf), &error);
  CloseHandle(h);
  mount_point_buffer *mp = (mount_point_buffer *)buf;
  snprintf(why, sizeof(why), "error %lu", error);
  row(n >= 0, path, "fsctl", why);
  if (n < 0) return;
  snprintf(why, sizeof(why), "tag %#lx, %d bytes for a data length of %u", mp->tag, n, mp->data_length);
  row(mp->tag == IO_REPARSE_TAG_MOUNT_POINT && n == 8 + mp->data_length, path, "fsctl-tag", why);
  const WCHAR *sub = mp->path + mp->sub_offset / sizeof(WCHAR);
  int sub_len = mp->sub_length / sizeof(WCHAR);
  snprintf(why, sizeof(why), "substitute %.*ls, print length %u", sub_len, sub, mp->print_length);
  row(sub_len == 49 && !wcsncmp(sub, L"\\??\\Volume{", 11) && !wcsncmp(sub + 47, L"}\\", 2) && !mp->print_length,
      path, "fsctl-volume", why);

  // Opened without FILE_FLAG_OPEN_REPARSE_POINT, the handle is the mounted volume's root: no reparse point.
  h = open_dir(path, 0);
  n = h != INVALID_HANDLE_VALUE ? get_reparse(h, buf, sizeof(buf), &error) : -1;
  if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
  snprintf(why, sizeof(why), "%d bytes, error %lu", n, error);
  row(n < 0 && error == ERROR_NOT_A_REPARSE_POINT, path, "followed", why);
}

static void check_dir(const WCHAR *path) {
  static BYTE buf[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
  DWORD attrs = GetFileAttributesW(path), error = 0;
  char why[160];
  snprintf(why, sizeof(why), "attributes %#lx", attrs);
  row(attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT),
      path, "attributes", why);
  HANDLE h = open_dir(path, FILE_FLAG_OPEN_REPARSE_POINT);
  int n = h != INVALID_HANDLE_VALUE ? get_reparse(h, buf, sizeof(buf), &error) : -1;
  if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
  snprintf(why, sizeof(why), "%d bytes, error %lu", n, error);
  row(n < 0 && error == ERROR_NOT_A_REPARSE_POINT, path, "fsctl", why);
}

int wmain(int argc, WCHAR **argv) {
  if (argc < 3 || argc % 2 == 0) {
    printf("FAIL x64-mountpoint: usage: x64-mountpoint mount <path> | dir <path> ...\n");
    return 1;
  }
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!wcscmp(argv[i], L"mount")) check_mount(argv[i + 1]);
    else check_dir(argv[i + 1]);
  }
  printf("%s x64-mountpoint\n", failed ? "FAIL" : "PASS");
  return failed;
}
