#define _POSIX_C_SOURCE 2001112L
#define DEFAULT_CONFIG_PATHNAME "./config.txt"
#define INFO "[INFO] "
#define WARNING "\e[0;33m[WARNING] " //Warning giallo
#define ERROR "\e[1;31m[ERROR] "    //Errore rosso

#include "util.h"
#include "hash.h"
#include "fileList.h"
#include "threadpool.h"

/* STRUTTURE DATI E FILES */
static config* conf = NULL;    //Configurazione impostata
static FILE* logFile = NULL;   //File di log impostato
static icl_hash_t* hashTable = NULL;   //Tabella hash contenente tutti i file
fileList* filesList = NULL;    //Lista di tutti i file nel server
threadpool_t* threadPool = NULL;     //ThreadPool

typedef struct sigArgs  //Struttura per il passaggio di argomenti al thread di gestione dei segnali
{
    sigset_t* set;
    int pipe;
}signalArgs;


/* LOCKS */
pthread_mutex_t filesListLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t memLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t logLock = PTHREAD_MUTEX_INITIALIZER;

/* VARIABILI GLOBALI */
bool needToQuit = false, needToHangUp = false;

/* VARIABILI PER STATISTICHE */
static int maxFiles = 0;       //Massimo numero di file presenti
static long maxMemory = 0;     //Memoria massima occupata in byte
static int algoExecutions = 0;     //Numero di volte che l'algoritmo di rimpiazzamento è stato eseguito
static int lastFiles = 0;     //Numero di file presenti alla chiusura del server

/* DICHIARAZIONE FUNZIONI UTILIZZATE */

//Scrive un messaggio sul file di log
void wLog(char* message);

//Stampa informazioni di fine esecuzione
void printFinalInfos();

//Funzione di cleanup
void cleanUp();

//Controlla i valori impostati con il file di configurazione, ed eventualmente li reimposta a valori default
void checkConfig();

//Funzione utilizzata dal thread di gestione dei segnali
void* signalTask(void* args);

//Funzione utilizzata per aggiornare il file descriptor più grande presente
int updateMax(fd_set set, int fdmax);

//Funzione utilizzata da un qualsiasi thread worker
void workerTask(void* args);

/* MAIN */
int main(int argc, char *argv[])
{
    //Imposta i parametri dal file di configurazione
    if ((conf = readConfig(DEFAULT_CONFIG_PATHNAME)) == NULL)
    {
        fprintf(stderr, ERROR "Errore in lettura del file di configurazione\n");
        exit(EXIT_FAILURE);
    }
    else checkConfig();

    //Apre il file di log
    if ((logFile=fopen(conf->logPath, "w")) == NULL)
    {
        fprintf(stderr, ERROR "Errore in apertura del file di log\n");
        exit(EXIT_FAILURE);
    }

    //Crea la tabella hash dei file, con dimensione massima specificata nella configurazione
    hashTable = icl_hash_create(conf->numFiles, NULL, NULL);

    //Inizializza la lista dei file
    initFileList(filesList);

    //Crea un threadpool con numero di workers specificato nella configurazione
    threadPool = createThreadPool(conf->numWorkers, 0);

    //Maschera per i segnali
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGHUP);

    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0)
    {
        fprintf(stderr, ERROR "Errore nell'applicazione della maschera dei segnali\n");
        cleanUp();
        exit(EXIT_FAILURE);
    }

    //TODO ingora sigpipe

    //Pipes per le comunicazioni server-thread gestione segnali e server-thread workers
    int signalPipe[2], requestPipe[2];
    if (pipe(signalPipe) == -1 || pipe(requestPipe) == -1)
    {
        fprintf(stderr, ERROR "Errore nella creazione delle pipe\n");
        cleanUp();
        exit(EXIT_FAILURE);
    }
    
    //Setup thread gestione dei segnali
    pthread_t signalThread;
    signalArgs sigThrArgs;
    sigThrArgs.set = &mask;
    sigThrArgs.pipe = signalPipe[1];
    if (pthread_create(&signalThread, NULL, signalTask, &sigThrArgs) != 0)
    {
        fprintf(stderr, ERROR "Errore nella creazione del thread di gestione dei segnali\n");
        cleanUp();
        exit(EXIT_FAILURE);
    }
    
    //Setup Socket
    unlink(conf->socketPath);
    int fdSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fdSocket == -1)
    {
        fprintf(stderr, ERROR "Errore nella creazione della socket '%s'\n", conf->socketPath);
        if (pthread_join(signalThread, NULL) != 0)
        {
            fprintf(stderr, ERROR "Errore nella join con il thread di gestione dei segnali\n");
        }
        cleanUp();
        exit(EXIT_FAILURE);
    }
    struct sockaddr_un sa;
    memset(&sa, '0', sizeof(sa));
    strncpy(sa.sun_path, conf->socketPath, strlen(conf->socketPath) + 1);
    sa.sun_family = AF_UNIX;
    if (bind(fdSocket, &sa, sizeof(sa)) == -1)
    {
        fprintf(stderr, ERROR "Errore nella bind\n");
        if (pthread_join(signalThread, NULL) != 0)
        {
            fprintf(stderr, ERROR "Errore nella join con il thread di gestione dei segnali\n");
        }
        cleanUp();
        exit(EXIT_FAILURE);
    }
    if (listen(fdSocket, MAXBACKLOG) != 0)
    {
        fprintf(stderr, ERROR "Errore nella listen\n");
        if (pthread_join(signalThread, NULL) != 0)
        {
            fprintf(stderr, ERROR "Errore nella join con il thread di gestione dei segnali\n");
        }
        cleanUp();
        exit(EXIT_FAILURE);
    }

    //Setup select
    fd_set set, tempset;
    FD_ZERO(&set);
    FD_ZERO(&tempset);
    FD_SET(fdSocket, &set);
    int fdMaximum;
    if (fdSocket > signalPipe[0])
    {
        fdMaximum = fdSocket;
    }
    else fdMaximum = signalPipe[0];
    

    //Server pronto per l'esecuzione
    bool running = true;
    printf(INFO "Server pronto\n\n");
    wLog(INFO "Server pronto\n\n");

    while (running)
    {
        tempset = set;
        if (select(fdMaximum + 1, &tempset, NULL, NULL, NULL) == -1)
        {
            fprintf(stderr, ERROR "Errore nella select\n");
            if (pthread_join(signalThread, NULL) != 0)
            {
                fprintf(stderr, ERROR "Errore nella join con il thread di gestione dei segnali\n");
            }
            cleanUp();
            exit(EXIT_FAILURE);
        }

        //Nuova richiesta ricevuta
        for (int i = 0; i < fdMaximum; i++)
        {
            if (FD_ISSET(i, &tempset))
            {
                long fdNew;
                if (i == fdSocket)
                {
                    //Nuova richiesta di connessione
                    if (fdNew = accept(fdSocket, (struct sockaddr*)NULL, NULL) == -1)
                    {
                        fprintf(stderr, ERROR "Errore nell'accept di una nuova connessione\n");
                        if (pthread_join(signalThread, NULL) != 0)
                        {
                            fprintf(stderr, ERROR "Errore nella join con il thread di gestione dei segnali\n");
                        }
                        cleanUp();
                        exit(EXIT_FAILURE);
                    }

                    FD_SET(fdNew, &set);
                    if (fdNew > fdMaximum)
                    {
                        fdMaximum = fdNew;
                    }
                    printf(INFO "Nuovo client connesso\n");
                    wLog(INFO "Nuovo client connesso al server\n");
                }
                else if (i == signalPipe[0])
                {
                    //Nuovo segnale ricevuto
                    running = false;
                    break;
                }
                else if (i == requestPipe[0])
                {
                    //Un thread ha eseguito una richiesta
                    long fdDone;
                    if (readn(requestPipe[0], &fdDone, sizeof(long)) == -1)
                    {
                        fprintf(stderr, WARNING "Errore in lettura su un file descriptor di un thread\n");
                        continue;
                    }
                    FD_SET(fdDone, &set);
                    if (fdDone > fdMaximum)
                    {
                        fdMaximum = fdDone;
                    }
                }
                else {
                    //Una richiesta di un client gia' connesso
                    //TODO invia richiesta a threadPool
                }
            }
        }
    }

    close(fdSocket);

    printFinalInfos();

    cleanUp();

    if (pthread_join(signalThread, NULL) != 0)
    {
        fprintf(stderr, ERROR "Errore nella join con il thread di gestione dei segnali\n");
        exit(EXIT_FAILURE);
    }

    if (fclose(logFile) != 0)
    {
        fprintf(stderr, WARNING "Errore in chiusura del file di log\n");
    }

    return 0;
}




/* IMPLEMENTAZIONE FUNZIONI UTILIZZATE */

void wLog(char *msg){
    pthread_mutex_lock(&logLock);
    fputs(msg, logFile);
    fflush(logFile);
    pthread_mutex_unlock(&logLock);
}

void printFinalInfos(){
    //Scrive sulla console
    fprintf(stdout, "\n\n-----Statistiche finali Server-----\n\n"
    "\nNumero di file massimi memorizzati: %d"
    "\nNumero massima raggiunta (in MB): %ld"
    "\nNumero di esecuzioni dell'algoritmo di rimpiazzamento: %d"
    "\nFile presenti nello storage alla chiusura: %d\n\n\n",
    maxFiles, maxMemory/1000000, algoExecutions, lastFiles);

    //Scrive sul file di log
    pthread_mutex_lock(&logLock);
    fprintf(logFile, "\n\n-----Statistiche finali Server-----\n\n"
    "\nNumero di file massimi memorizzati: %d"
    "\nNumero massima raggiunta (in MB): %ld"
    "\nNumero di esecuzioni dell'algoritmo di rimpiazzamento: %d"
    "\nFile presenti nello storage alla chiusura: %d\n\n\n",
    maxFiles, maxMemory/1000000, algoExecutions, lastFiles);
    icl_hash_dump(logFile, hashTable);
    fflush(logFile);
    pthread_mutex_unlock(&logLock);
}

void cleanUp(){
    unlink(conf->socketPath);
    icl_hash_destroy(hashTable, free, free);
    destroyFileList(filesList);
    if (needToQuit)
    {
        destroyThreadPool(threadPool, 1);
    }
    else if (needToHangUp)
    {
        destroyThreadPool(threadPool, 0);
    }
    else destroyThreadPool(threadPool, 1);
    free(conf);
}

void checkConfig(){
    if (!conf->numFiles)
    {
        unsigned int numFiles = 10;
        fprintf(stderr, WARNING "Numero di file massimo non impostato, utilizzo %d\n", numFiles);
        conf->numFiles = numFiles;
    }
    else if (conf->numFiles <= 0)
    {
        fprintf(stderr, ERROR "Il numero massimo di file deve essere un numero positivo");
        exit(EXIT_FAILURE);
    }
    

    if (!conf->memorySpace)
    {
        long mem = 1000000;
        fprintf(stderr, WARNING "Memoria massima utilizzabile non impostata, utilizzo %dMB\n", mem / 1000000);
        conf->memorySpace = mem;
    }
    else if (conf->memorySpace <= 0)
    {
        fprintf(stderr, ERROR "La memoria massima utilizzabile deve essere un numero positivo");
        exit(EXIT_FAILURE);
    }

    if (!conf->numWorkers)
    {
        unsigned int work = 4;
        fprintf(stderr, WARNING "Numero di thread worker non impostato, utilizzo %d\n", work);
        conf->numWorkers = work;
    }
    else if (conf->numWorkers <= 0)
    {
        fprintf(stderr, ERROR "Il numero massimo di workers deve essere un numero positivo");
        exit(EXIT_FAILURE);
    }

    if (conf->socketPath[0] == '\0')
    {
        char *sock = "./socket";
        fprintf(stderr, WARNING "Path del socket non impostato, utilizzo %s\n", sock);
        strcpy(conf->socketPath, sock);
    }

    if (conf->logPath[0] == '\0')
    {
        char *log = "./log.txt";
        fprintf(stderr, WARNING "Path del file di log non impostato, utilizzo %s\n", log);
        strcpy(conf->logPath, log);
    }
}

void* signalTask(void* args){
    int pipe = ((signalArgs*) args)->pipe;
    sigset_t* set = ((signalArgs *)args)->set;
    //Aspetta in un ciclo infinito un segnale
    while (true)
    {
        int sigReceived;
        if((errno = sigwait(set, &sigReceived)) != 0){
            //Errore nella sigwait
            perror(ERROR "Errore fatale sigwait thread gestione dei segnali\n");
            return NULL;
        }

        if (sigReceived == 'SIGINT' || sigReceived == 'SIGQUIT')
        {
            printf(WARNING "Ricevuto segnale SIGINT/SIGQUIT, chiusura del server imminente\n");
            needToQuit = true;
            close(pipe);    //Chiude il file descriptor associato alla pipe
            return NULL;
        }
        else if (sigReceived == 'SIGHUP')
        {
            printf(WARNING "Ricevuto segnale SIGHUP, il server aspettera' i thread attivi e terminera'\n");
            needToHangUp = true;
            close(pipe);
            return NULL;
        }
    }
    return NULL;
}

int updateMax(fd_set set, int fdmax){
    for (int i = (fdmax - 1); i >= 0; --i)
    {
        if (FD_ISSET(i, &set))
            return i;
    }
    assert(1 == 0);
    return -1;
}

void workerTask(void* args){
    //TODO Implement worker task
}