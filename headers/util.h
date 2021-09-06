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
#include <time.h>

//Imposta la dimensione massima di un file
#if !defined(MAX_FILE_SIZE)
#define MAX_FILE_SIZE 1000000
#endif

//Imposta la lunghezza massima di un path
#if !defined(MAX_PATH_LENGTH)
#define MAX_PATH_LENGTH 256
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
config* readConfig(char * pathname);

//Funzione utilizzata per scrivere un messaggio sul file di log
void wLog(char * msg);

#endif