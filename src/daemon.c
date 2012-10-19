#if defined __SUNPRO_C || defined __DECC || defined __HP_cc
# pragma ident "@(#)$Header: /cvsroot/wikipedia/willow/src/bin/willow/daemon.c,v 1.1 2005/05/02 19:15:21 kateturner Exp $"
# pragma ident "$NetBSD: daemon.c,v 1.9 2003/08/07 16:42:46 agc Exp $"
#endif

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

int daemon(int nochdir, int noclose)
{
    int fd;

    switch (fork()) {
    case -1:
        return (-1);
    case 0:
        break;
    default:
        _exit(EXIT_SUCCESS);
    }

    if (setsid() == -1)
        return (-1);

    if (nochdir == 0)
        (void)chdir("/");

    if (noclose == 0 && (fd = open("/dev/null", O_RDWR, 0)) != -1) {
        (void)dup2(fd, STDIN_FILENO);
        (void)dup2(fd, STDOUT_FILENO);
        (void)dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO)
            (void)close(fd);
    }
    return (0);
}
