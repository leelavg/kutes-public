/* trying to have a single bash instance for running commands */
#include <nappgui.h>

typedef struct _kpsuper_t Kpsuper;
typedef struct _kprocess_t Kproc;
typedef enum _kperror_t
{
    ktPPIPE = 1,
    ktPEXEC,
    ktPOK,
    ktPAGAIN
} kperror_t;

Kpsuper *kpsuper_create(kperror_t *error);
void kpsuper_exec(const Kpsuper *ks, const char_t *cmd);
void kpsuper_destroy(Kpsuper **kpsuper);
Kproc *kproc_exec(Kpsuper *super, const char_t *command, kperror_t *error);
void kproc_close(Kproc **proc);
bool_t kproc_cancel(Kproc *proc);
uint32_t kproc_wait(Kproc *proc);
bool_t kproc_finish(Kproc *proc, uint32_t *code);
bool_t kproc_read(Kproc *proc, byte_t *data, const uint32_t size, uint32_t *rsize, kperror_t *error);
bool_t kproc_eread(Kproc *proc, byte_t *data, const uint32_t size, uint32_t *rsize, kperror_t *error);
bool_t kproc_write(Kproc *proc, const byte_t *data, const uint32_t size, uint32_t *wsize, kperror_t *error);
bool_t kproc_read_close(Kproc *proc);
bool_t kproc_eread_close(Kproc *proc);
bool_t kproc_write_close(Kproc *proc);
void kproc_exit(const uint32_t code);

#include <core/hfile.h>
#include <core/strings.h>
#include <errno.h>
#include <fcntl.h>
#include <osbs/bproc.h>
#include <osbs/bthread.h>
#include <poll.h>
#include <sewer/bmem.h>
#include <sewer/cassert.h>
#include <sewer/ptr.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined __LINUX__
int kill(pid_t pid, int sig);
#endif

struct _kpsuper_t
{
    String *fifoin;
    String *fifoout;
    String *fifoerr;
    int pwin;
    int prout;
    int prerr;
    pid_t sid;
    Proc *_reg;
};

struct _kprocess_t
{
    char_t hello;
};

static uint32_t nextnum = 0;
static time_t sesid = 0;

/* INT32_MAX has 10 digits */
const char_t *cmd_fmt = "(printf '%%-10s' $BASHPID && %s)\n";

void kpsuper_destroy(Kpsuper **kpsuper)
{
    cassert_no_null(kpsuper);
    cassert_no_null((*kpsuper));

    if ((*kpsuper)->sid > 0)
    {
        kill(~((*kpsuper)->sid) + 1, SIGKILL);
        bproc_cancel((*kpsuper)->_reg);
        bproc_close(&(*kpsuper)->_reg);
    }

    unlink(tc((*kpsuper)->fifoin));
    str_destroy(&(*kpsuper)->fifoin);

    unlink(tc((*kpsuper)->fifoout));
    str_destroy(&(*kpsuper)->fifoout);

    unlink(tc((*kpsuper)->fifoerr));
    str_destroy(&(*kpsuper)->fifoerr);
}

Kpsuper *kpsuper_create(kperror_t *error)
{
    char_t buf[20];
    uint32_t next;
    int crwin, cwout, cwerr;
    Kpsuper *kps = cast(bmem_malloc(sizeof(Kpsuper)), Kpsuper);

    if (!sesid)
    {
        sesid = time(NULL);
    }
    next = nextnum;
    nextnum++;

    {
        sprintf(buf, "fifo.%ld.%d.0", sesid, next);
        kps->fifoin = hfile_appdata(buf);

        sprintf(buf, "fifo.%ld.%d.1", sesid, next);
        kps->fifoout = hfile_appdata(buf);

        sprintf(buf, "fifo.%ld.%d.2", sesid, next);
        kps->fifoerr = hfile_appdata(buf);

        if (mkfifo(tc(kps->fifoin), 0666))
        {
            kpsuper_destroy(&kps);
            ptr_assign(error, ktPPIPE);
            return NULL;
        }
        if (mkfifo(tc(kps->fifoout), 0666))
        {
            kpsuper_destroy(&kps);
            ptr_assign(error, ktPPIPE);
            return NULL;
        }
        if (mkfifo(tc(kps->fifoerr), 0666))
        {
            kpsuper_destroy(&kps);
            ptr_assign(error, ktPPIPE);
            return NULL;
        }
    }

    {
        pid_t pid;

        kps->prout = open(tc(kps->fifoout), O_RDONLY | O_NONBLOCK);
        kps->prerr = open(tc(kps->fifoerr), O_RDONLY | O_NONBLOCK);

        pid = fork();
        if (pid == -1)
        {
            kpsuper_destroy(&kps);
            ptr_assign(error, ktPEXEC);
            return NULL;
        }
        else if (pid == 0)
        {
            crwin = open(tc(kps->fifoin), O_RDWR);
            dup2(crwin, STDIN_FILENO);
            close(crwin);

            cwout = open(tc(kps->fifoout), O_WRONLY);
            dup2(cwout, STDOUT_FILENO);
            close(cwout);
            close(kps->prout);

            cwerr = open(tc(kps->fifoerr), O_WRONLY);
            dup2(cwerr, STDERR_FILENO);
            close(cwerr);
            close(kps->prerr);

            execlp("bash", "bash", NULL);
            cassert_msg(FALSE, "It's a zombie!");
            ptr_assign(error, ktPEXEC);
            return NULL;
        }
        else
        {
            kps->pwin = open(tc(kps->fifoin), O_WRONLY);
            signal(SIGPIPE, SIG_IGN);
            kps->sid = pid;
            /* to get the internal accounting */
            kps->_reg = bproc_exec("sleep inifinity", NULL);
            bproc_write_close(kps->_reg);
            bproc_read_close(kps->_reg);
            bproc_eread_close(kps->_reg);
            return kps;
        }
    }
}

void kpsuper_exec(const Kpsuper *ks, const char_t *cmd)
{
    byte_t buf[1024];
    byte_t pid_max[11];
    pid_t child;
    uint32_t max = 1024;
    int32_t bread = 0;
    struct pollfd fds[2];
    int ret;

    if (!strcmp(cmd, "\n"))
        return;

    ret = waitpid(ks->sid, 0, WNOHANG);
    if (ret == -1 && errno == ECHILD)
    {
        printf("super doesn't exit\n");
        return;
    }

    dprintf(ks->pwin, cmd_fmt, cmd, NULL);
    fds[0].fd = ks->prout;
    fds[0].events = POLLIN;
    fds[1].fd = ks->prerr;
    fds[1].events = POLLIN;
    ret = poll(fds, 2, 2);
    if (ret > 0)
    {
        if (fds[0].revents & POLLIN)
        {
            bread = read(ks->prout, pid_max, 10);
            pid_max[bread] = '\0';
            child = atoi(cast(pid_max, char_t));
        }
    }
    else
    {
        printf("failed\n");
    }

    while (TRUE)
    {
        bool_t finished = FALSE;
        if (child)
        {
            ret = kill(child, 0);
            if (ret == -1 && errno == ESRCH)
            {
                /* process finished */
                finished = TRUE;
            }
            else if (ret == 0)
            {
                /* process still running */
                bthread_sleep(100);
            }
        }
        else
        {
            finished = TRUE;
        }

        if ((bread = read(ks->prout, cast(buf, char_t), max - 1)) > 0)
        {
            buf[bread] = '\0';
            printf("------ out ------\n%s-----------------\n", buf);
        }
        else if ((bread = read(ks->prerr, cast(buf, char_t), max - 1)) > 0)
        {
            buf[bread] = '\0';
            printf("------ err ------\n%s-----------------\n", buf);
        }
        else if (finished)
        {
            break;
        }
    }
}

/* sample usage */
int main(void)
{
    char_t *line = NULL;
    size_t size;
    int out = 0;
    kperror_t err;
    Kpsuper *kps = kpsuper_create(&err);
    if (!kps)
        return;
    while ((out = getline(&line, &size, stdin)) != -1)
    {
        kpsuper_exec(kps, line);
        if (!strcmp(line, "exit\n"))
            break;
    }
    if (kps)
        kpsuper_destroy(&kps);
}
