/* What mn-steam-host tells its ntdll.so stand-in (host_wine.c). */
#ifndef MN_HOST_WINE_H
#define MN_HOST_WINE_H
struct mn_reg_log;
void mn_stub_set_map(const char *host, const char *guest);
void mn_stub_set_reg_log(struct mn_reg_log *log);
#endif
