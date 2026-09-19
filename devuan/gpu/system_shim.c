/* system_shim.c — overtager system(): kører kommandoen med et RENT env.
   Baggrund: efter hybris-init har processen et ødelagt environ (execve fejler
   med EFAULT fra glibc 2.41's system() på 3.10-kernen). Shim'en fork/exec'er
   selv med envp=NULL, så execve ikke skal læse det ødelagte blok.

   19. sep 2026 (fælde 44): hybris' EGL-init kører en "display-dans" ved HVER
   Firefox-start —
     /usr/bin/find /sys/class/display/<x>/enable | xargs ... echo 0/1
   samt chvt frem og tilbage. Efter nok gentagelser holder X op med at skrive
   til framebufferen: skærmen fryser, musen virker stadig, og først
   `service nodm restart` hjælper. Med env SHIM_NO_DISPLAY_DANCE=1 springes
   enable-bommene over, med SHIM_NO_CHVT=1 springes chvt over (begge logges).
   Begge er OPT-IN: uden env er shim'en uændret, så den spillbare opskrift
   kører som før. */
#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

static int shim_env_on(const char *name)
{
    const char *e = getenv(name);
    return e && *e && strcmp(e, "0") ? 1 : 0;
}

int system(const char *cmd)
{
    if (!cmd) return 1;
    if (shim_env_on("SHIM_NO_DISPLAY_DANCE") &&
        strstr(cmd, "/sys/class/display/")) {
        fprintf(stderr, "[system-shim] SKIPPED (SHIM_NO_DISPLAY_DANCE): %s\n",
                cmd);
        return 0;
    }
    if (shim_env_on("SHIM_NO_CHVT") && strstr(cmd, "chvt")) {
        fprintf(stderr, "[system-shim] SKIPPED (SHIM_NO_CHVT): %s\n", cmd);
        return 0;
    }
    fprintf(stderr, "[system-shim] %s\n", cmd);
    pid_t pid = fork();
    if (pid == 0) {
        char *argv[] = { "/bin/sh", "-c", (char *)cmd, NULL };
        char *envp[] = { NULL };
        execve("/bin/sh", argv, envp);
        _exit(127);
    }
    if (pid < 0) return -1;
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}
