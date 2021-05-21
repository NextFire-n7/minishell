#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include "readcmd.h"
#include "builtin.h"
#include "process.h"

#define MAX_PIPES 10     // jamais vu plus de 5 pipes en une cmd...
#define GREEN "\x1B[32m" // pour que le prompt soit joli
#define RESET "\x1B[0m"  // reset la couleur

static struct process *pl = NULL; // liste chainee des processus
static pid_t pid_bg;              // pid du processus en bg
static int fd_stdin, fd_stdout;   // backup des fd stdin et stdout

static void suivi_fils(int sig)
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
                struct process **p_stopped = pl_get_by_pid(&pl, pid_fils);
                (*p_stopped)->is_running = STOPPED;
                printf("[%d] %d: %s — Stopped\n", (*p_stopped)->id, (*p_stopped)->pid, (*p_stopped)->cmd);
            }
            else if (WIFCONTINUED(etat_fils))
            {
                /* traiter la reprise */
                struct process **p_started = pl_get_by_pid(&pl, pid_fils);
                (*p_started)->is_running = RUNNING;
                printf("[%d] %d: %s\n", (*p_started)->id, (*p_started)->pid, (*p_started)->cmd);
            }
            else if (WIFEXITED(etat_fils))
            {
                /* traiter exit */
                pl_remove(&pl, pid_fils);
            }
            else if (WIFSIGNALED(etat_fils))
            {
                /* traiter signal */
                pl_remove(&pl, pid_fils);
            }
        }
    } while (pid_fils > 0);
    /* autres actions après le suivi des changements d'état */
}

// prompt
static void print_prompt()
{
    printf(GREEN "%s" RESET "$ ", getcwd(NULL, 0));
}

// SIGSTP -> SIGSTOP
static void fwd_sig_stop(int sig)
{
    printf("\n");
    if (pid_bg)
    {
        kill(pid_bg, SIGSTOP);
    }
    else
    {
        print_prompt();
    }
}

// SIGINT -> SIGKILL
static void fwd_sig_kill(int sig)
{
    printf("\n");
    if (pid_bg)
    {
        kill(pid_bg, SIGKILL);
    }
    else
    {
        print_prompt();
    }
}

// mets en place les redirections
static int redirections(struct cmdline cmdl)
{
    // en input
    if (cmdl.in)
    {
        int fd_in = open(cmdl.in, O_RDONLY);
        if (fd_in == -1)
        {
            return -1;
        }
        dup2(fd_in, STDIN_FILENO);
    }

    // en output
    if (cmdl.out)
    {
        int fd_out = open(cmdl.out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out == -1)
        {
            return -1;
        }
        dup2(fd_out, STDOUT_FILENO);
    }
    return 0;
}

int main(int argc, char const *argv[])
{
    // def des handlers
    struct sigaction handler_sigchld, handler_fwd_stop, handler_fwd_kill, handler_mask;
    handler_sigchld.sa_handler = suivi_fils;
    handler_fwd_stop.sa_handler = fwd_sig_stop;
    handler_fwd_kill.sa_handler = fwd_sig_kill;
    handler_mask.sa_handler = SIG_IGN;

    // attribution des handlers
    sigaction(SIGCHLD, &handler_sigchld, NULL);
    sigaction(SIGTSTP, &handler_fwd_stop, NULL);
    sigaction(SIGINT, &handler_fwd_kill, NULL);

    // backup des fd stdin/stdout originels
    fd_stdin = dup(STDIN_FILENO);
    fd_stdout = dup(STDOUT_FILENO);

    // variables locales
    struct cmdline *cmdl;
    pid_t pid_fils;
    int id;
    int i;
    int pipes[MAX_PIPES][2];

    while (1)
    {
        // restauration du stdin et stdout en entree du prompt
        dup2(fd_stdin, STDIN_FILENO);
        dup2(fd_stdin, STDOUT_FILENO);

        pid_bg = 0;     // plus de process en avant-plan
        print_prompt(); // affichage prompt
        do
        {
            cmdl = readcmd(); // lecture de la ligne de cmd
        } while (!cmdl || !cmdl->seq);

        // mise en place des redirections
        if (redirections(*cmdl) == -1)
        {
            perror("redirections");
            continue;
        }

        // iteration sur les commandes
        i = -1;
        while (cmdl->seq[++i])
        {
            // creation pipe si cmd suivante existe
            if (cmdl->seq[i + 1])
            {
                if (pipe(pipes[i]) == -1)
                {
                    perror("pipe");
                }
            }

            // fermeture entree pipe precedent
            if (i > 0)
            {
                close(pipes[i - 1][1]);
            }

            // fork si pas une commande built-in
            if (!builtin(&pl, cmdl->seq[i], &pid_bg))
            {
                pid_fils = fork();
                if (pid_fils == -1)
                {
                    perror("fork");
                    break;
                }
                if (!pid_fils) // fils
                {
                    // masquage SIGTSTP et SIGINT
                    sigaction(SIGTSTP, &handler_mask, NULL);
                    sigaction(SIGINT, &handler_mask, NULL);
                    // mise en place pipes entree sortie
                    if (i > 0)
                    {
                        dup2(pipes[i - 1][0], STDIN_FILENO);
                    }
                    if (cmdl->seq[i + 1])
                    {
                        dup2(pipes[i][1], STDOUT_FILENO);
                    }
                    // exec
                    execvp(cmdl->seq[i][0], cmdl->seq[i]);
                    // si fail
                    perror(cmdl->seq[i][0]);
                    exit(getpid());
                }
                else // pere
                {
                    // fermeture pipe que le pere ne lit pas
                    if (i > 0)
                    {
                        close(pipes[i - 1][0]);
                    }
                    // ajout du process dans la liste
                    id = pl_add(&pl, pid_fils, cmdl->seq[i]);
                    // gestion bg ou pas
                    if (cmdl->backgrounded)
                    {
                        printf("[%d] %d\n", id, pid_fils);
                    }
                    else
                    {
                        pid_bg = pid_fils;
                        pause();
                    }
                }
            }
        }
    }

    return 1; // Pas normal si on se retrouve ici...
}
