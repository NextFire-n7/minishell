#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include "readcmd.h"

void suivi_fils(int sig)
{
    int etat_fils, pid_fils;
    do
    {
        pid_fils = (int)waitpid(-1, &etat_fils, WNOHANG | WUNTRACED | WCONTINUED);
        if ((pid_fils == -1) && (errno != ECHILD))
        {
            perror("waitpid");
            exit(EXIT_FAILURE);
        }
        else if (pid_fils > 0)
        {
            if (WIFSTOPPED(etat_fils))
            {
                /* traiter la suspension */
            }
            else if (WIFCONTINUED(etat_fils))
            {
                /* traiter la reprise */
            }
            else if (WIFEXITED(etat_fils))
            {
                /* traiter exit */
            }
            else if (WIFSIGNALED(etat_fils))
            {
                /* traiter signal */
            }
        }
    } while (pid_fils > 0);
    /* autres actions après le suivi des changements d'état */
}

void jobs(char **subseq) {}

void stop(char **subseq) {}

void bg(char **subseq) {}

void fg(char **subseq) {}

int builtin(char **subseq)
{
    int is_builtin = 1;
    if (!strcmp(subseq[0], "cd"))
    {
        chdir(subseq[1]);
    }
    else if (!strcmp(subseq[0], "exit"))
    {
        exit(EXIT_SUCCESS);
    }
    else if (!strcmp(subseq[0], "jobs"))
    {
        jobs(subseq);
    }
    else if (!strcmp(subseq[0], "stop"))
    {
        stop(subseq);
    }
    else if (!strcmp(subseq[0], "bg"))
    {
        bg(subseq);
    }
    else if (!strcmp(subseq[0], "fg"))
    {
        fg(subseq);
    }
    else
    {
        is_builtin = 0;
    }
    return is_builtin;
}

void main()
{
    struct sigaction handler_sigchld;
    handler_sigchld.sa_handler = suivi_fils;
    sigaction(SIGCHLD, &handler_sigchld, NULL);

    struct cmdline *cmd;
    char cwd[1024];
    int i;
    while (1)
    {
        getcwd(cwd, sizeof(cwd));
        printf("%s$ ", cwd);
        cmd = readcmd();
        i = -1;
        while (cmd->seq[++i] != NULL)
        {
            if (!builtin(cmd->seq[i]))
            {
                switch (fork())
                {
                case -1:
                    printf("ECHEC fork\n");
                    break;
                case 0:
                    execvp(cmd->seq[i][0], cmd->seq[i]);
                    printf("%s\n", cmd->err);
                    exit(getpid());
                default:
                    if (!cmd->backgrounded)
                    {
                        wait(NULL);
                    }
                }
            }
        }
    }
}
