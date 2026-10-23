// For eac-vm/tests/games-xattr.sh, built in the builder, run in the guest: what Wine's get_file_info asks of every
// directory entry it lists (dlls/ntdll/unix/file.c: user.WINEREPARSE into 16 KiB, user.DOSATTRIB into 64 bytes), for
// each regular file of <dir>, <rounds> times. Prints the calls, how each one ended and their mean time:
//   calls N enodata N eopnotsupp N other N mean_us X
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/xattr.h>
#include <time.h>

int main(int argc, char **argv) {
    static char buf[16384], path[4096];
    long calls = 0, enodata = 0, eopnotsupp = 0, other = 0;
    struct timespec a, b;
    int rounds = argc > 2 ? atoi(argv[2]) : 1;
    if (argc < 2) return 2;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int r = 0; r < rounds; r++) {
        DIR *d = opendir(argv[1]);
        struct dirent *e;
        if (!d) return perror(argv[1]), 2;
        while ((e = readdir(d))) {
            if (e->d_type != DT_REG) continue;
            snprintf(path, sizeof path, "%s/%s", argv[1], e->d_name);
            for (int k = 0; k < 2; k++, calls++) {
                if (getxattr(path, k ? "user.DOSATTRIB" : "user.WINEREPARSE", buf, k ? 64 : sizeof buf) >= 0) other++;
                else if (errno == ENODATA) enodata++;
                else if (errno == EOPNOTSUPP) eopnotsupp++;
                else other++;
            }
        }
        closedir(d);
    }
    clock_gettime(CLOCK_MONOTONIC, &b);
    double us = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 1e3;
    printf("calls %ld enodata %ld eopnotsupp %ld other %ld mean_us %.1f\n", calls, enodata, eopnotsupp, other,
           calls ? us / calls : 0.0);
    return 0;
}
