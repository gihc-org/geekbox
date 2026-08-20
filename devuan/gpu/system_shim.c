/* system_shim.c — overtager system(): kører kommandoen med et RENT env.
   Baggrund: efter hybris-init har processen et ødelagt environ (execve fejler
   med EFAULT fra glibc 2.41's system() på 3.10-kernen). Shim'en fork/exec'er
   selv med envp=NULL, så execve ikke skal læse det ødelagte blok. */
#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>

int system(const char *cmd)
{
    if (!cmd) return 1;
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
