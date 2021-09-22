#if !defined(_UTIL_H)
#define _UTIL_H

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

//Imposta la dimensione massima di un file
#if !defined(MAX_FILE_SIZE)
#define MAX_FILE_SIZE 1000000
#endif

//Imposta la lunghezza massima di un path
#if !defined(MAX_PATH_LENGTH)
#define MAX_PATH_LENGTH 256
#endif

#if !defined(EXTRA_LEN_PRINT_ERROR)
#define EXTRA_LEN_PRINT_ERROR 512
#endif

//Flags associabili ad un file gestito dal server
enum flags{
    O_CREATE = 1,
    O_LOCK = 2,
    O_CREATE_OR_O_LOCK = 3,
    NOFLAGS = 0
};

//Struttura dati contenente i dati di configurazione
typedef struct config{
    unsigned int numFiles;
    long memorySpace;
    unsigned int numWorkers;
    char socketPath[MAX_PATH_LENGTH];
    char logPath[MAX_PATH_LENGTH];
} config;

//Struttura dati che rappresenta un messaggio inviato dal client al server o viceversa
typedef struct msg{
    void* data;
    size_t size;
} msg;

//Funzione utilizzata per effettuare il parsing del file configurazione
config * readConfig(char* pathname);

#define SYSCALL_EXIT(name, r, sc, str, ...) \
    if ((r = sc) == -1)                     \
    {                                       \
        perror(#name);                      \
        int errno_copy = errno;             \
        print_error(str, __VA_ARGS__);      \
        exit(errno_copy);                   \
    }

#define SYSCALL_PRINT(name, r, sc, str, ...) \
    if ((r = sc) == -1)                      \
    {                                        \
        perror(#name);                       \
        int errno_copy = errno;              \
        print_error(str, __VA_ARGS__);       \
        errno = errno_copy;                  \
    }

#define SYSCALL_RETURN(name, r, sc, str, ...) \
    if ((r = sc) == -1)                       \
    {                                         \
        perror(#name);                        \
        int errno_copy = errno;               \
        print_error(str, __VA_ARGS__);        \
        errno = errno_copy;                   \
        return r;                             \
    }

#define CHECK_EQ_EXIT(name, X, val, str, ...) \
    if ((X) == val)                           \
    {                                         \
        perror(#name);                        \
        int errno_copy = errno;               \
        print_error(str, __VA_ARGS__);        \
        exit(errno_copy);                     \
    }

#define CHECK_NEQ_EXIT(name, X, val, str, ...) \
    if ((X) != val)                            \
    {                                          \
        perror(#name);                         \
        int errno_copy = errno;                \
        print_error(str, __VA_ARGS__);         \
        exit(errno_copy);                      \
    }

/**
 * \brief Procedura di utilita' per la stampa degli errori
 *
 */
static inline void print_error(const char *str, ...)
{
    const char err[] = "ERROR: ";
    va_list argp;
    char *p = (char *)malloc(strlen(str) + strlen(err) + EXTRA_LEN_PRINT_ERROR);
    if (!p)
    {
        perror("malloc");
        fprintf(stderr, "FATAL ERROR nella funzione 'print_error'\n");
        return;
    }
    strcpy(p, err);
    strcpy(p + strlen(err), str);
    va_start(argp, str);
    vfprintf(stderr, p, argp);
    va_end(argp);
    free(p);
}

#endif