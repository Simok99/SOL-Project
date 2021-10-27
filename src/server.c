#define _POSIX_C_SOURCE 2001112L
#define DEFAULT_CONFIG_PATHNAME "./config.txt"
#define DEFAULT_MAXFILES 10
#define DEFAULT_MAXMEMORY 1000000
#define DEFAULT_WORKERS 4
#define DEFAULT_SOCKETPATH "./socket"
#define DEFAULT_LOGPATH "./log.txt"
#define INFO "[INFO] "
#define WARNING "[WARNING] "
#define ERROR "[ERROR] "

#include "util.h"
#include "hash.h"
#include "fileList.h"
#include "queue.h"
#include "threadpool.h"

/* STRUTTURE DATI E FILES */
static config* conf = NULL;    //Configurazione impostata
static FILE* logFile = NULL;   //File di log impostato
static icl_hash_t* hashTable = NULL;   //Tabella hash contenente tutti i file
fileList* filesList;    //Lista di tutti i file nel server
fileList* openFilesList; //Lista dei file aperti nel server
queue* queueForLocks;   //Coda contenente i client in attesa di acquisire la lock su un file
threadpool_t* threadPool = NULL;     //ThreadPool
msg response;   //Contiene la risposta da inviare ad un client

typedef struct sigArgs  //Struttura per il passaggio di argomenti al thread di gestione dei segnali
{
    sigset_t* set;
    int pipe;
}signalArgs;


/* LOCKS */
pthread_mutex_t filesListLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t openFilesListLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queueForLocksLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t memLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t logLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t statsLock = PTHREAD_MUTEX_INITIALIZER;  //Usata per aggiornare le variabili globali e statistiche

/* VARIABILI GLOBALI */
static bool needToQuit = false, needToHangUp = false;
static int currentConnectedClients = 0;
static long currentReadBytes = 0, currentWrittenBytes = 0;

/* VARIABILI PER STATISTICHE */
static int maxConnectedClients = 0; //Massimo numero di client connessi contemporaneamente
static int maxFiles = 0;       //Massimo numero di file presenti
static long maxMemory = 0;     //Memoria massima occupata in byte
static int algoExecutions = 0;     //Numero di volte che l'algoritmo di rimpiazzamento è stato eseguito
static int lastFiles = 0;     //Numero di file presenti alla chiusura del server
static int howManyLocks = 0;
static int howManyOpenLocks = 0;
static int howManyUnlocks = 0;
static int howManyCloses = 0;
static int howManyReads = 0;
static int howManyWrites = 0;
static float meanFilesReadSize = 0;
static float meanFilesWriteSize = 0;

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

//Funzione utilizzata da un worker per fare il parsing di un messaggio del client
char** parseRequest(msg* request);

/**
 * Controlla che il server possa contenere un altro file, ed eventualmente rimuove il primo file inserito (FIFO)
 * @param[in] fileName -- puntatore che verra' aggiornato con il nome del file espulso
 * @param[in] data -- puntatore ai dati che verra' aggiornato con i dati del file espulso
 */
void checkFilesFIFO(char** fileName, void** data);

/**
 * Controlla che il server possa contenere un altro file, ed eventualmente rimuove i primi file inseriti (FIFO)
 * @param[in] expelledFilesQueue -- puntatore che verra' aggiornato con i file espulsi
 * @param[in] newFileSize -- dimensione del file che si vuole inserire
 */
void checkMemoryFIFO(queue** expelledFilesQueue, size_t newFileSize);

/*Tenta di dare la lock del file specificato al client, se il file risulta gia' acquisito da un altro client
 * restituisce false
 */
bool tryLock(char* fileName, long fdSocket);

/* MAIN */
int main(int argc, char *argv[])
{
    //Imposta i parametri dal file di configurazione
    if ((conf = readConfig(DEFAULT_CONFIG_PATHNAME)) == NULL)
    {
        fprintf(stderr, ERROR "Errore in lettura del file di configurazione\n");
        return -1;
    }
    else checkConfig();

    //Apre il file di log
    if ((logFile=fopen(conf->logPath, "w")) == NULL)
    {
        fprintf(stderr, ERROR "Impossibile aprire il file di log al path:%s\n",conf->logPath);
        return -1;
    }

    //Crea la tabella hash dei file, con dimensione massima specificata nella configurazione
    hashTable = icl_hash_create(conf->numFiles, NULL, NULL, conf->memorySpace);

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
        return -1;
    }

    //Ignora SIGPIPE per non essere terminato da una scrittura sul socket
    struct sigaction s;
    memset(&s, 0, sizeof(s));
    s.sa_handler = SIG_IGN;
    if ((sigaction(SIGPIPE, &s, NULL)) == -1)
    {
        fprintf(stderr, ERROR "Errore: impossibile ignorare segnale SIGPIPE\n");
        cleanUp();
        return -1;
    }

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
        fprintf(stderr, ERROR "Errore nella creazione della socket\n");
        if (pthread_join(signalThread, NULL) != 0)
        {
            fprintf(stderr, ERROR "Errore nella join con il thread di gestione dei segnali\n");
        }
        cleanUp();
        return -1;
    }
    struct sockaddr_un sa;
    memset(&sa, '0', sizeof(sa));
    strncpy(sa.sun_path, conf->socketPath, strlen(conf->socketPath) + 1);
    sa.sun_family = AF_UNIX;
    if (bind(fdSocket, (struct sockaddr*) &sa, sizeof(sa)) == -1)
    {
        fprintf(stderr, ERROR "Errore nella bind: probabilmente non esiste un file socket al path %s\n",conf->socketPath);
        if (pthread_join(signalThread, NULL) != 0)
        {
            fprintf(stderr, ERROR "Errore nella join con il thread di gestione dei segnali\n");
        }
        cleanUp();
        return -1;
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
    FD_SET(signalPipe[0], &set);
    FD_SET(requestPipe[0], &set);
    int fdMaximum;
    if (fdSocket > signalPipe[0])
    {
        fdMaximum = fdSocket;
    }
    else fdMaximum = signalPipe[0];
    

    //Setup coda per le lock
    queueForLocks = createQueue();

    //Crea un threadpool con numero di workers specificato nella configurazione
    threadPool = createThreadPool((int)conf->numWorkers, 0);

    //Server pronto per l'esecuzione
    bool running = true;
    printf(INFO "Server pronto\n\n");
    wLog(INFO "Server pronto\n\n");

    long* args = NULL;
    while (running)
    {
        tempset = set;
        if (select(fdMaximum + 1, &tempset, NULL, NULL, NULL) == -1)
        {
            fprintf(stderr, ERROR "Errore nella select\n");
            cleanUp();
            if (pthread_join(signalThread, NULL) != 0)
            {
                fprintf(stderr, ERROR "Errore nella join con il thread di gestione dei segnali\n");
            }
            exit(EXIT_FAILURE);
        }

        //Nuova richiesta ricevuta
        for (int i = 0; i <= fdMaximum; i++)
        {
            if (FD_ISSET(i, &tempset))
            {
                long fdNew;
                if (i == fdSocket)
                {
                    //Nuova richiesta di connessione
                    if ((fdNew = accept(fdSocket, (struct sockaddr*)NULL, NULL)) == -1)
                    {
                        fprintf(stderr, ERROR "Errore nell'accept di una nuova connessione\n");
                        cleanUp();
                        if (pthread_join(signalThread, NULL) != 0)
                        {
                            fprintf(stderr, ERROR "Errore nella join con il thread di gestione dei segnali\n");
                        }
                        exit(EXIT_FAILURE);
                    }

                    FD_SET(fdNew, &set);
                    if (fdNew > fdMaximum)
                    {
                        fdMaximum = fdNew;
                    }
                    printf(INFO "Nuovo client connesso\n");
                    wLog(INFO "Nuovo client connesso al server\n");
                    currentConnectedClients++;
                    if (maxConnectedClients < currentConnectedClients)
                    {
                        maxConnectedClients = currentConnectedClients;
                    }
                    
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
                    char loginfo[1024];
                    snprintf(loginfo, 1024, INFO "Un thread ha servito un client\n");
                    printf("%s",loginfo);
                    wLog(loginfo);
                    FD_SET(fdDone, &set);
                    if (fdDone > fdMaximum)
                    {
                        fdMaximum = fdDone;
                    }
                }
                else {
                    //Una richiesta di un client gia' connesso
                    args = realloc(args,sizeof(long)*2);
                    if (!args)
                    {
                        fprintf(stderr, ERROR "Errore fatale malloc\n");
                        cleanUp();
                        if (pthread_join(signalThread, NULL) != 0)
                        {
                            fprintf(stderr, ERROR "Errore nella join con il thread di gestione dei segnali\n");
                        }
                        exit(EXIT_FAILURE);
                    }

                    args[0] = fdNew;
                    args[1] = (int) requestPipe[1];
                    FD_CLR(i, &set);
                    fdMaximum = updateMax(set, fdMaximum);
                    int res = addToThreadPool(threadPool, workerTask, (void*)args);
                    if (res == 0)
                    {
                        //Richiesta inviata al threadPool
                        continue;
                    }
                    else if (res < 0)
                    {
                        //Errore
                        fprintf(stderr, WARNING "Impossibile aggiungere richiesta al threadPool\n");
                    }
                    //Non ci sono thread disponibili, ne creo uno detached
                    wLog(WARNING "Nessun thread disponibile nel threadPool, uso uno in modalita' detached\n");
                    res = spawnThread(workerTask, (void*)args);
                    if (res == 0)
                    {
                        //Thread avviato con successo
                        continue;
                    }
                    else {
                        //Errore
                        fprintf(stderr, WARNING "Impossibile avviare un thread in modalita' detached\n");
                        wLog(ERROR "Impossibile avviare un thread per eseguire la richiesta\n");
                        continue;
                    }
                }
                //Richiesta non gestibile
                continue;
            }
        }
    }

    free(args);

    close(fdSocket);

    if (howManyReads == 0) meanFilesReadSize = 0;
    else meanFilesReadSize = (float)currentReadBytes/(float)howManyReads;

    if (howManyWrites == 0) meanFilesWriteSize = 0;
    else meanFilesWriteSize = (float)currentWrittenBytes/(float)howManyWrites;

    lastFiles = lengthList(filesList);

    printFinalInfos();

    if (pthread_join(signalThread, NULL) != 0)
    {
        fprintf(stderr, ERROR "Errore nella join con il thread di gestione dei segnali\n");
        exit(EXIT_FAILURE);
    }

    if (fclose(logFile) != 0)
    {
        fprintf(stderr, WARNING "Errore in chiusura del file di log\n");
    }

    cleanUp();

    free(conf);

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
    icl_hash_dump(stdout, hashTable);
    fprintf(stdout, "\n\n-----Statistiche finali Server-----\n\n"
    "\nNumero di read effettuate: %d"
    "\nSize media delle read effettuate: %.2f bytes"
    "\nNumero di write effettuate: %d"
    "\nSize media delle write effettuate: %.2f bytes"
    "\nNumero di lock effettuate: %d"
    "\nNumero di open-lock effettuate: %d"
    "\nNumero di unlock effettuate: %d"
    "\nNumero di close effettuate: %d"
    "\nNumero di file massimi memorizzati: %d"
    "\nMemoria massima raggiunta (in Bytes): %ld"
    "\nNumero di esecuzioni dell'algoritmo di rimpiazzamento: %d"
    "\nNumero di connessioni massime contemporanee: %d"
    "\nFile presenti nello storage alla chiusura: %d\n",
    howManyReads, meanFilesReadSize, howManyWrites, meanFilesWriteSize,
    howManyLocks, howManyOpenLocks, howManyUnlocks, howManyCloses,
    maxFiles, maxMemory, algoExecutions, maxConnectedClients, lastFiles);

    for (int i = 0; i < conf->numWorkers; i++)
    {
        fprintf(stdout, "Il thread %d ha eseguito %d richieste\n", i, threadPool->requestsServedByThread[i]);
    }
    printf("\n\n\n");
    

    //Scrive sul file di log
    pthread_mutex_lock(&logLock);
    icl_hash_dump(logFile, hashTable);
    fprintf(logFile, "\n\n-----Statistiche finali Server-----\n\n"
    "\nNumero di read effettuate: %d"
    "\nSize media delle read effettuate: %.2f bytes"
    "\nNumero di write effettuate: %d"
    "\nSize media delle write effettuate: %.2f bytes"
    "\nNumero di lock effettuate: %d"
    "\nNumero di open-lock effettuate: %d"
    "\nNumero di unlock effettuate: %d"
    "\nNumero di close effettuate: %d"
    "\nNumero di file massimi memorizzati: %d"
    "\nMemoria massima raggiunta (in Bytes): %ld"
    "\nNumero di esecuzioni dell'algoritmo di rimpiazzamento: %d"
    "\nNumero di connessioni massime contemporanee: %d"
    "\nFile presenti nello storage alla chiusura: %d\n",
    howManyReads, meanFilesReadSize, howManyWrites, meanFilesWriteSize,
    howManyLocks, howManyOpenLocks, howManyUnlocks, howManyCloses,
    maxFiles, maxMemory, algoExecutions, maxConnectedClients, lastFiles);

    for (int i = 0; i < conf->numWorkers; i++)
    {
        fprintf(logFile, "Il thread %d ha eseguito %d richieste\n", i, threadPool->requestsServedByThread[i]);
    }
    fflush(logFile);
    pthread_mutex_unlock(&logLock);
}

void cleanUp(){
    unlink(conf->socketPath);
    icl_hash_destroy(hashTable, free, free);
    destroyFileList(&filesList);
    destroyFileList(&openFilesList);
    free(queueForLocks);
    if (needToQuit)
    {
        destroyThreadPool(threadPool, 1);
    }
    else if (needToHangUp)
    {
        destroyThreadPool(threadPool, 0);
    }
    else destroyThreadPool(threadPool, 1);
}

void checkConfig(){
    if (!conf->numFiles)
    {
        unsigned int numFiles = DEFAULT_MAXFILES;
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
        long mem = DEFAULT_MAXMEMORY;
        fprintf(stderr, WARNING "Memoria massima utilizzabile non impostata, utilizzo %ldMB\n", mem / 1000000);
        conf->memorySpace = mem;
    }
    else if (conf->memorySpace <= 0)
    {
        fprintf(stderr, ERROR "La memoria massima utilizzabile deve essere un numero positivo");
        exit(EXIT_FAILURE);
    }

    if (!conf->numWorkers)
    {
        unsigned int work = DEFAULT_WORKERS;
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
        char *sock = DEFAULT_SOCKETPATH;
        fprintf(stderr, WARNING "Path del socket non impostato, utilizzo %s\n", sock);
        strcpy(conf->socketPath, sock);
    }

    if (conf->logPath[0] == '\0')
    {
        char *log = DEFAULT_LOGPATH;
        fprintf(stderr, WARNING "Path del file di log non impostato, utilizzo %s\n", log);
        strcpy(conf->logPath, log);
    }
}

void* signalTask(void* args){
    int pipe = ((signalArgs*) args)->pipe;
    sigset_t* set = ((signalArgs *)args)->set;
    //Aspetta in un ciclo infinito un segnale
    int sigReceived;
    bool error = false;
    while (!error)
    {
        if((errno = sigwait(set, &sigReceived)) != 0){
            //Errore nella sigwait
            perror(ERROR "Errore fatale sigwait thread gestione dei segnali\n");
            error = true;
        }
        if (sigReceived == SIGINT || sigReceived == SIGQUIT)
        {
            printf(WARNING "Ricevuto segnale SIGINT/SIGQUIT, chiusura del server imminente\n");
            needToQuit = true;
            close(pipe);    //Chiude il file descriptor associato alla pipe
            return NULL;
        }
        else if (sigReceived == SIGHUP)
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
    if (args == NULL) return;
    long* arg = (long*)args;
    long fdSocket = arg[0];
    int pipe = (int)arg[1];

    //Riceve la richiesta del client
    msg request;
    memset(&request, 0, sizeof(msg));
    int read = 0;
    if ((read=readn(fdSocket, (void*)&request, sizeof(msg))) < 0)
    {
        fprintf(stderr, "Errore nella ricezione del messaggio del client\n");
        return;
    }
    if (read == 0)
    {
        //Richieste terminate, effettuo unlock dei file del client e chiudo connessione
        LOCK(&filesListLock);
        int n = 0;
        char** lockedFiles = getLockedFiles(filesList, fdSocket, &n);
        if (n != 0 && lockedFiles != NULL)
        {
            for (int i = 0; i < n; i++)
            {
                listUnlockFile(&filesList, lockedFiles[i], fdSocket);
                LOCK(&statsLock);
                howManyUnlocks++;
                UNLOCK(&statsLock);
                char loginfo[1024];
                snprintf(loginfo, 1024, INFO "Client %ld unlocked file %s\n", fdSocket, lockedFiles[i]);
                printf("%s", loginfo);
                wLog(loginfo);
                // Il primo client in coda su quel file prende la lock
                LOCK(&queueForLocksLock);
                node *notifyClientNode = searchInQueue(queueForLocks, lockedFiles[i]);
                if (notifyClientNode != NULL)
                {
                    long notifiedSocket = atol((char *)notifyClientNode->data);
                    if (tryLock(lockedFiles[i], notifiedSocket))
                    {
                        // Invia 0 al nuovo client che ha preso la lock
                        char *r = "0";
                        strcpy(response.data, r);
                        if (writen(notifiedSocket, (void*)&response, sizeof(msg)) == -1)
                        {
                            fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                            break;
                        }
                    }
                    else{
                        // Invia -1 al nuovo client che non e' riuscito a prendere la lock
                        char *res = "-1";
                        strcpy(response.data, res);
                        if (writen(notifiedSocket, (void*)&response, sizeof(msg)) == -1)
                        {
                            fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                            break;
                        }

                        free(notifyClientNode->data);
                        free(notifyClientNode->id);
                        free(notifyClientNode);
                    }
                }
                UNLOCK(&queueForLocksLock);
                LOCK(&openFilesListLock);
                if(deleteFile(&openFilesList, lockedFiles[i], fdSocket) != -1){
                    LOCK(&statsLock);
                    howManyCloses++;
                    UNLOCK(&statsLock);
                }
                UNLOCK(&openFilesListLock);
                free(lockedFiles[i]);
            }
        }
        free(lockedFiles);
        UNLOCK(&filesListLock);
        char loginfo[1024];
        snprintf(loginfo, 1024, INFO "Il client %ld si e' disconnesso\n", fdSocket);
        printf("%s", loginfo);
        wLog(loginfo);
        close(fdSocket);
        LOCK(&statsLock);
        currentConnectedClients--;
        UNLOCK(&statsLock);
        return;
    }
    
    //Fa il parsing del comando ricevuto
    char** commands = parseRequest(&request);
    //long unusedsocket = atol(strtok(commands[0], ","));
    int operation = atoi(strtok(commands[1], " "));
    char* param = strdup(strtok(NULL, "\r\n\t"));
    int flags = atoi(commands[2]);
    char* data = (char*)request.data;

    /*printf("OPERATION:%d\n",operation);
    printf("PARAM:%s\n", param);
    printf("FLAGS:%d\n", flags);
    printf("DATA:%s\n",data);*/

    //Esegue la richiesta se risulta valida
    switch (operation)
    {
    case OPENFILE:
        //Richiesta OPENFILE
        if (flags == O_CREATE)
        {   
            char *pathname = param;
            if ((icl_hash_find(hashTable, (void*)pathname)) != NULL)
            {
                //File gia' presente
                fprintf(stderr, WARNING "Non sono riuscito a creare il file %s\n", pathname);
                //Invia messaggio valore di ritorno -1
                char *r = "-1";
                strcpy(response.data, r);
                strcpy(response.command, " ");
                int k = 0;
                if (writen(fdSocket, (void *)&k, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                if (writen(fdSocket, &response, sizeof(response)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                break;
            }
            
            queue* expelledFilesQueue = createQueue();
            char* fileExpelled = NULL;
            void* expelledData = NULL;
            checkFilesFIFO(&fileExpelled, &expelledData);

            if(fileExpelled) insertQueue(expelledFilesQueue, fileExpelled, '~', expelledData);

            checkMemoryFIFO(&expelledFilesQueue, request.size);
            icl_entry_t* new = icl_hash_insert(hashTable, (void*)pathname, (void*)strdup(data), (long)request.size);
            if (new == NULL)
            {
                //Errore
                fprintf(stderr, WARNING "Non sono riuscito a creare il file %s\n", pathname);
                //Invia messaggio valore di ritorno -1
                char* r = "-1";
                strcpy(response.data, r);
                strcpy(response.command, " ");
                int k = 0;
                if (writen(fdSocket, (void *)&k, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                if (writen(fdSocket, &response, sizeof(response)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                free(expelledFilesQueue);
                break;
            }
            LOCK(&filesListLock);
            insertFile(&filesList, param, fdSocket);
            UNLOCK(&filesListLock);
            LOCK(&openFilesListLock);
            insertFile(&openFilesList, param, fdSocket);
            UNLOCK(&openFilesListLock);

            //File inserito correttamente
            LOCK(&statsLock);
            if (maxFiles < hashTable->nentries)
                maxFiles = hashTable->nentries;
            if (maxMemory < hashTable->currentMemory)
                maxMemory = hashTable->currentMemory;
            maxFiles++;
            UNLOCK(&statsLock);
            char loginfo[1024];
            snprintf(loginfo, 1024, INFO "File %s inserito nello storage\n", pathname);
            printf("%s",loginfo);
            wLog(loginfo);
            //Invia messaggio valore di ritorno 0 se non c'è stato un file espulso, altrimenti invia n
            //risposte con command="nomefile" e contenuto=data per ogni file espulso
            if (queueLength(expelledFilesQueue) == 0)
            {
                char *r = "0";
                strcpy(response.data, r);
                strcpy(response.command, " ");
                int k = 0;
                if (writen(fdSocket, (void *)&k, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
            }
            else {
                int n = queueLength(expelledFilesQueue);
                if (writen(fdSocket, (void *)&n, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    deleteQueue(expelledFilesQueue);
                    free(expelledFilesQueue);
                    break;
                }
                for (int i = 0; i < n; i++)
                {
                    node* newFile = popQueue(expelledFilesQueue);
                    strcpy(response.command, newFile->id);
                    response.size = strlen((char*)newFile->data);
                    strcpy(response.data, (char*)newFile->data);
                    if (writen(fdSocket, &response, sizeof(msg)) == -1)
                    {
                        fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                        break;
                    }
                    free(newFile->id);
                    free(newFile->data);
                    free(newFile);
                }
            }
            free(expelledFilesQueue);
            break;
        }
        else if (flags == O_LOCK)
        {
            if (icl_hash_find(hashTable, (void *)param) == NULL)
            {
                // Invia messaggio valore di ritorno NOTFOUND
                char *r = "NOTFOUND";
                strcpy(response.data, r);
                strcpy(response.command, " ");
                int k = 0;
                if (writen(fdSocket, (void *)&k, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                free(param);
                break;
            }
            //File trovato, tenta di acquisire la lock
            LOCK(&filesListLock);
            if (containsFile(filesList, param, fdSocket) == 0)
            {
                UNLOCK(&filesListLock);
                // Il client ha gia' la lock sul file specificato
                // Invia messaggio valore di ritorno ALREADYLOCKED
                char *r = "ALREADYLOCKED";
                strcpy(response.data, r);
                int k = 0;
                if (writen(fdSocket, (void *)&k, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                free(param);
                break;
            }
            UNLOCK(&filesListLock);
            if (tryLock(param, fdSocket))
            {
                // Lock acquisita
                LOCK(&openFilesListLock);
                insertFile(&openFilesList, param, fdSocket);
                UNLOCK(&openFilesListLock);
                //  Invia messaggio valore di ritorno 0
                char *r = "0";
                strcpy(response.data, r);
                int k = 0;
                if (writen(fdSocket, (void *)&k, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                LOCK(&statsLock);
                howManyOpenLocks++;
                howManyLocks--;
                UNLOCK(&statsLock);
                char loginfo[1024];
                snprintf(loginfo, 1024, INFO "Client %ld open-locked file %s\n", fdSocket, param);
                printf("%s", loginfo);
                wLog(loginfo);
                free(param);
                break;
            }
            // Lock non acquisita, inserisco il client in coda
            LOCK(&queueForLocksLock);
            char *sock = malloc(sizeof(long));
            snprintf(sock, sizeof(long), "%ld", fdSocket);
            insertQueue(queueForLocks, param, '~', (void *)sock);
            UNLOCK(&queueForLocksLock);
            break;
        }
        else if (flags == O_CREATE_OR_O_LOCK)
        {
            char *pathname = param;
            if (icl_hash_find(hashTable, (void *)pathname) != NULL)
            {
                //File gia' presente
                fprintf(stderr, WARNING "Non sono riuscito a creare il file %s perche' gia' presente\n", pathname);
                //Invia messaggio valore di ritorno INSTORAGE
                char *r = "ALREADYINSTORAGE";
                strcpy(response.data, r);
                strcpy(response.command, " ");
                int k = 0;
                if (writen(fdSocket, (void *)&k, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                free(pathname);
                break;
            }

            queue *expelledFilesQueue = createQueue();
            char* fileExpelled = NULL;
            void* expelledData = NULL;
            checkFilesFIFO(&fileExpelled, &expelledData);

            if(fileExpelled) insertQueue(expelledFilesQueue, fileExpelled, '~', expelledData);

            checkMemoryFIFO(&expelledFilesQueue, request.size);

            icl_entry_t *new = icl_hash_insert(hashTable, (void*)pathname, (void*)(data), (long)request.size);
            if (new == NULL)
            {
                //Errore
                fprintf(stderr, WARNING "Non sono riuscito a creare il file %s\n", pathname);
                //Invia messaggio valore di ritorno -1
                char *r = "-1";
                strcpy(response.data, r);
                strcpy(response.command, " ");
                int k = 0;
                if (writen(fdSocket, (void *)&k, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                deleteQueue(expelledFilesQueue);
                free(pathname);
                break;
            }
            LOCK(&filesListLock);
            insertFile(&filesList, param, fdSocket);
            UNLOCK(&filesListLock);

            //File inserito correttamente
            LOCK(&statsLock);
            if (maxFiles < hashTable->nentries)
                maxFiles = hashTable->nentries;
            if (maxMemory < hashTable->currentMemory)
                maxMemory = hashTable->currentMemory;
            UNLOCK(&statsLock);
            char loginfo[1024];
            snprintf(loginfo, 1024, INFO "File %s inserito nello storage\n", pathname);
            printf("%s", loginfo);
            wLog(loginfo);
            
            //Prova a effettuare lock, se fallisce mette il client in coda di attesa per lock e non risponde
            if (tryLock(pathname, fdSocket))
            {
                //Lock acquisita dal client, risponde numero file espulsi+0 oppure numero file espulsi+file espulsi
                LOCK(&openFilesListLock);
                insertFile(&openFilesList, param, fdSocket);
                UNLOCK(&openFilesListLock);
                if (queueLength(expelledFilesQueue) == 0)
                {
                    char *r = "0";
                    strcpy(response.data, r);
                    strcpy(response.command, " ");
                    int k = 0;
                    if (writen(fdSocket, (void*)&k, sizeof(int)) == -1)
                    {
                        fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                        break;
                    }
                    if (writen(fdSocket, &response, sizeof(msg)) == -1)
                    {
                        fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                        break;
                    }
                }
                else
                {
                    int n = queueLength(expelledFilesQueue);
                    if (writen(fdSocket, (void*)&n, sizeof(int)) == -1)
                    {
                        fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                        deleteQueue(expelledFilesQueue);
                        free(expelledFilesQueue);
                        break;
                    }
                    for (int i = 0; i < n; i++)
                    {
                        node *newFile = popQueue(expelledFilesQueue);
                        strcpy(response.command, newFile->id);
                        response.size = strlen((char*)newFile->data);
                        strcpy(response.data, (char*)newFile->data);
                        if (writen(fdSocket, &response, sizeof(msg)) == -1)
                        {
                            fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                            break;
                        }
                        free(newFile->id);
                        free(newFile->data);
                        free(newFile);
                    }
                }
                LOCK(&statsLock);
                howManyOpenLocks++;
                howManyLocks--;
                UNLOCK(&statsLock);
                char loginfo[1024];
                snprintf(loginfo, 1024, INFO "Client %ld open-locked file %s\n", fdSocket, pathname);
                printf("%s", loginfo);
                wLog(loginfo);
                free(expelledFilesQueue);
                break;
            }
            //Impossibile dare la lock al client, lo inserisco in coda di attesa per il file
            LOCK(&queueForLocksLock);
            char *sock = malloc(sizeof(long));
            snprintf(sock, sizeof(long), "%ld", fdSocket);
            insertQueue(queueForLocks, pathname, '~', (void *)sock);
            UNLOCK(&queueForLocksLock);
            break;
        }
        else{
            //Nessun flag, apre il file se esiste
            if (icl_hash_find(hashTable, (void*)param) == NULL)
            {
                //Invia messaggio valore di ritorno NOTFOUND
                char *r = "NOTFOUND";
                strcpy(response.data, r);
                strcpy(response.command, " ");
                int k = 0;
                if (writen(fdSocket, (void *)&k, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                //free(param);
                break;
            }
            else{
                //File trovato, lo apre se non risulta locked
                if (isLocked(filesList, param))
                {
                    char loginfo[1024];
                    snprintf(loginfo, 1024, INFO "File %s non aperto dal client %ld (risulta locked)\n", param, fdSocket);
                    printf("%s", loginfo);
                    wLog(loginfo);
                    // Invia messaggio valore di ritorno -1 (file non aperto)
                    char *r = "-1";
                    strcpy(response.command, " ");
                    strcpy(response.data, r);
                    int k = 0;
                    if (writen(fdSocket, (void *)&k, sizeof(int)) == -1)
                    {
                        fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                        break;
                    }
                    if (writen(fdSocket, &response, sizeof(msg)) == -1)
                    {
                        fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                        break;
                    }
                    break;
                }
                
                //File non locked
                LOCK(&openFilesListLock);
                insertFile(&openFilesList, param, fdSocket);
                UNLOCK(&openFilesListLock);
                char loginfo[1024];
                snprintf(loginfo, 1024, INFO "File %s aperto dal client %ld\n", param, fdSocket);
                printf("%s",loginfo);
                wLog(loginfo);
                //Invia messaggio valore di ritorno 0 (file aperto)
                char *r = "0";
                strcpy(response.command, " ");
                strcpy(response.data, r);
                int k = 0;
                if (writen(fdSocket, (void *)&k, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
            }
            break;
        }
        break;

    case READFILE:
        {
            char *pathname = param;
            void *fileData = icl_hash_find(hashTable, (void *)pathname);
            if (fileData == NULL)
            {
                // File non trovato
                fprintf(stderr, WARNING "File %s non trovato nel server\n", pathname);
                // Invia messaggio valore di ritorno NOTINSTORAGE
                char *r = "NOTINSTORAGE";
                strcpy(response.command, r);
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                free(pathname);
                break;
            }
            //File trovato nel server
            LOCK(&openFilesListLock);
            if (containsFile(openFilesList, pathname, fdSocket) == 0)
            {
                UNLOCK(&openFilesListLock);
                size_t size = strlen((char*)fileData);
                // Invia messaggio valore di ritorno con data e size
                response.size = size;
                strcpy(response.data, (char*)fileData);
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }

                LOCK(&statsLock);
                howManyReads++;
                currentReadBytes += (long)size;
                UNLOCK(&statsLock);
                char loginfo[1024];
                snprintf(loginfo, 1024, INFO "Il client %ld ha letto %ld bytes del file %s\n", fdSocket, size, pathname);
                printf("%s", loginfo);
                wLog(loginfo);
                free(pathname);
                break;
            }
            else{
                //File presente ma non aperto dal client
                UNLOCK(&openFilesListLock);
                fprintf(stderr, WARNING "File %s non aperto dal client %ld (ha tentato di leggerlo)\n", pathname, fdSocket);
                // Invia messaggio valore di ritorno NOTOPEN
                char *r = "NOTOPEN";
                strcpy(response.command, r);
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                free(pathname);
                break;
            }
            free(pathname);
            break;
        }

    case READNFILES:
        {
            int n = atoi(param);
            free(param);
            if (n <= 0)
            {
                //Legge e restituisce tutti i file non locked
                int filesInStorage;
                LOCK(&filesListLock);
                char** unlockedFiles = getUnlockedFiles(filesList, &filesInStorage);
                if (writen(fdSocket, (void *)&filesInStorage, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                for (int i = 0; i < filesInStorage; i++)
                {
                    char* filename = unlockedFiles[i];
                    void* fileData = icl_hash_find(hashTable, (void *)filename);
                    response.size = strlen((char*)fileData);
                    strcpy(response.command, filename);
                    strcpy(response.data, (char*)fileData);
                    if (writen(fdSocket, (void *)&response, sizeof(msg)) == -1)
                    {
                        fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                        break;
                    }
                    LOCK(&statsLock);
                    howManyReads++;
                    currentReadBytes += (long) response.size;
                    UNLOCK(&statsLock);
                    free(unlockedFiles[i]);
                }
                free(unlockedFiles);
                UNLOCK(&filesListLock);
                //free(param);
                break;
            }
            
            //Prova a leggere fino a n files non locked e restituirli
            LOCK(&filesListLock);
            int filesUnlockedInStorage, i=0;
            char **unlockedFiles = getUnlockedFiles(filesList, &filesUnlockedInStorage);
            if (filesUnlockedInStorage == 0)
            {
                if (writen(fdSocket, (void *)&filesUnlockedInStorage, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
            }
            
            if (n >= filesUnlockedInStorage)
            {
                if (writen(fdSocket, (void *)&filesUnlockedInStorage, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
            }
            else {
                if (writen(fdSocket, (void *)&n, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
            }
            
            while (i < n && i <= filesUnlockedInStorage)
            {
                char *filename = unlockedFiles[i];
                void *fileData = icl_hash_find(hashTable, (void *)filename);
                response.size = strlen((char *)fileData);
                strcpy(response.command, filename);
                strcpy(response.data, (char *)fileData);
                if (writen(fdSocket, (void *)&response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                LOCK(&statsLock);
                howManyReads++;
                currentReadBytes += (long)response.size;
                UNLOCK(&statsLock);
                free(unlockedFiles[i]);
                i++;
            }
            free(unlockedFiles);
            UNLOCK(&filesListLock);
            //free(param);
        }
        break;

    case WRITEFILE:
        {
            char *filePath = param;
            if ((long)request.size > MAX_FILE_SIZE)
            {
                //File troppo grande
                fprintf(stderr, WARNING "File %s troppo grande (modificare in util.h se necessario)\n", filePath);
                //Invia messaggio valore di ritorno TOOBIG
                char *r = "TOOBIG";
                strcpy(response.data, "");
                strcpy(response.command, r);
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                free(filePath);
                break;
            }
            //Avendo effettuato openFile con il flag O_CREATE_OR_O_LOCK il file esiste nel server ed e' aperto dal client
            //TODO lock memoria?
            queue *expelledFilesQueue = createQueue();

            checkMemoryFIFO(&expelledFilesQueue, request.size);

            if(icl_hash_update_insert(hashTable, (void*)filePath, (void*)strdup(data), NULL, (long)request.size) == NULL)
            {
                LOCK(&openFilesListLock);
                deleteFile(&openFilesList, filePath, fdSocket);
                UNLOCK(&openFilesListLock);
                LOCK(&statsLock);
                howManyCloses++;
                UNLOCK(&statsLock);
                LOCK(&filesListLock);
                deleteFile(&filesList, filePath, fdSocket);
                UNLOCK(&filesListLock);
                fprintf(stderr, WARNING "File %s eliminato dal server (troppo grande)\n", filePath);
                free(filePath);
                //Invia i file espulsi per fare spazio
                int n = queueLength(expelledFilesQueue);
                if (writen(fdSocket, (void *)&n, sizeof(int)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    deleteQueue(expelledFilesQueue);
                    free(expelledFilesQueue);
                    break;
                }
                //Invia messaggio valore di ritorno TOOBIG
                char *r = "TOOBIG";
                strcpy(response.data, "");
                strcpy(response.command, r);
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                //Invia i file espulsi
                for (int i = 0; i < n; i++)
                {
                    node *newFile = popQueue(expelledFilesQueue);
                    strcpy(response.command, newFile->id);
                    response.size = strlen((char *)newFile->data);
                    strcpy(response.data, (char *)newFile->data);
                    if (writen(fdSocket, &response, sizeof(msg)) == -1)
                    {
                        fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                        break;
                    }
                    free(newFile->id);
                    free(newFile->data);
                    free(newFile);
                }
                free(expelledFilesQueue);
                break;
            }
            // File aggiornato correttamente
            int n = queueLength(expelledFilesQueue);
            if (writen(fdSocket, (void *)&n, sizeof(int)) == -1)
            {
                fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                deleteQueue(expelledFilesQueue);
                free(expelledFilesQueue);
                break;
            }
            // Invia messaggio valore di ritorno UPDATED
            char *r = "UPDATED";
            strcpy(response.data, "");
            strcpy(response.command, r);
            if (writen(fdSocket, &response, sizeof(msg)) == -1)
            {
                fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                break;
            }
            // Invia i file espulsi per fare spazio
            for (int i = 0; i < n; i++)
            {
                node *newFile = popQueue(expelledFilesQueue);
                strcpy(response.command, newFile->id);
                response.size = strlen((char *)newFile->data);
                strcpy(response.data, (char *)newFile->data);
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                free(newFile->id);
                free(newFile->data);
                free(newFile);
            }
            free(expelledFilesQueue);
            LOCK(&statsLock);
            if (maxFiles < hashTable->nentries)
                maxFiles = hashTable->nentries;
            if (maxMemory < hashTable->currentMemory)
                maxMemory = hashTable->currentMemory;
            howManyWrites++;
            currentWrittenBytes += (long)request.size;
            UNLOCK(&statsLock);
            char loginfo[1024];
            snprintf(loginfo, 1024, INFO "File %s aggiornato nel server\n", filePath);
            printf("%s", loginfo);
            wLog(loginfo);
            break;
        }

    case APPENDTOFILE:
        {
            char *filePath = param;
            if ((long)request.size > MAX_FILE_SIZE)
            {
                // File troppo grande
                fprintf(stderr, WARNING "File %s troppo grande (modificare in util.h se necessario)\n", filePath);
                // Invia messaggio valore di ritorno TOOBIG
                char *r = "TOOBIG";
                strcpy(response.data, "");
                strcpy(response.command, r);
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                free(filePath);
                break;
            }
            //File presente nel server e locked dal client (ha effettuato openfile con O_LOCK)
            void *backupData = icl_hash_find(hashTable, (void*)filePath);
            size_t oldDataSize = strlen((char*)backupData);
            char* newData = malloc(sizeof(char)*MAX_FILE_SIZE);
            strncpy(newData, (char*)backupData, oldDataSize);
            strncat(newData, data, strlen(data));
            if (icl_hash_update_insert(hashTable, (void *)filePath, (void *)strdup(newData), NULL, strlen(newData)) == NULL)
            {
                free(newData);
                // Errore nell'aggiornamento del contenuto del file, provo a ripristinare il vecchio contenuto
                if (icl_hash_update_insert(hashTable, (void *)filePath, backupData, NULL, oldDataSize) == NULL)
                {
                    // Non sono riuscito a ripristinare il file, provo a rimuoverlo
                    if (icl_hash_delete(hashTable, (void *)filePath, free, free) == 0)
                    {
                        LOCK(&openFilesListLock);
                        deleteFile(&openFilesList, filePath, fdSocket);
                        UNLOCK(&openFilesListLock);
                        LOCK(&statsLock);
                        howManyCloses++;
                        UNLOCK(&statsLock);
                        LOCK(&filesListLock);
                        deleteFile(&filesList, filePath, fdSocket);
                        UNLOCK(&filesListLock);
                        char loginfo[1024];
                        snprintf(loginfo, 1024, WARNING "File %s rimosso dal server (impossibile ripristinarlo)\n", filePath);
                        printf("%s", loginfo);
                        wLog(loginfo);
                        // Invia messaggio valore di ritorno data=NULL e command="FAILED" (impossibile ripristinare il file)
                        response.size = 0;
                        char *r = "FAILED";
                        strcpy(response.command, r);
                        strcpy(response.data, "");
                        if (writen(fdSocket, &response, sizeof(msg)) == -1)
                        {
                            fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                            break;
                        }
                        break;
                    }
                    char loginfo[1024];
                    snprintf(loginfo, 1024, ERROR "Impossibile rimuovere file %s dal server (dopo tentato ripristino)\n", filePath);
                    printf("%s", loginfo);
                    wLog(loginfo);
                }
                // File non aggiornato, ma ripristinato correttamente
                char loginfo[1024];
                snprintf(loginfo, 1024, INFO "File %s ripristinato nel server (impossibile aggiornarlo)\n", filePath);
                printf("%s", loginfo);
                wLog(loginfo);
                // Invia messaggio valore di ritorno data=NULL e command="UNCHANGED" (impossibile aggiornare il file)
                response.size = 0;
                char *r = "UNCHANGED";
                strcpy(response.command, r);
                strcpy(response.data, "");
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                break;
            }
            // File aggiornato correttamente
            LOCK(&statsLock);
            currentWrittenBytes -= (long)oldDataSize;
            currentWrittenBytes += (long)strlen(newData);
            if (maxFiles < hashTable->nentries)
                maxFiles = hashTable->nentries;
            if (maxMemory < hashTable->currentMemory)
                maxMemory = hashTable->currentMemory;
            UNLOCK(&statsLock);
            free(newData);

            char loginfo[1024];
            snprintf(loginfo, 1024, INFO "File %s aggiornato nel server\n", filePath);
            printf("%s", loginfo);
            wLog(loginfo);
            // Invia messaggio valore di ritorno data=NULL
            strcpy(response.data, "");
            if (writen(fdSocket, &response, sizeof(msg)) == -1)
            {
                fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                break;
            }
            break;
        }
        break;

    case LOCKFILE:
        {
            char* pathname = param;
            LOCK(&filesListLock);
            if (containsFile(filesList, param, fdSocket) == 0)
            {
                UNLOCK(&filesListLock);
                // Il client ha gia' la lock sul file specificato
                // Invia messaggio valore di ritorno ALREADYLOCKED
                char *r = "ALREADYLOCKED";
                strcpy(response.data, r);
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                free(pathname);
                break;
            }
            UNLOCK(&filesListLock);

            if (icl_hash_find(hashTable, (void*)pathname) == NULL)
            {
                //File non presente
                //Invia messaggio valore di ritorno NOTFOUND
                char *r = "NOTFOUND";
                strcpy(response.data, r);
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                free(pathname);
                break;
            }

            //File presente nel server
            if (tryLock(pathname, fdSocket))
            {
                //Lock acquisita
                // Invia messaggio valore di ritorno 0
                char *r = "0";
                strcpy(response.data, r);
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                free(pathname);
                break;
            }
            //Lock non acquisita, inserisco il client in coda
            LOCK(&queueForLocksLock);
            char *sock = malloc(sizeof(long));
            snprintf(sock, sizeof(long), "%ld", fdSocket);
            insertQueue(queueForLocks, pathname, '~', (void*)sock);
            UNLOCK(&queueForLocksLock);
        }
        break;

    case UNLOCKFILE:
        LOCK(&filesListLock);
        if (containsFile(filesList, param, fdSocket) == -1)
        {
            UNLOCK(&filesListLock);
            //Il client non ha la lock sul file specificato o file non presente
            //Invia messaggio valore di ritorno -1
            char *r = "-1";
            strcpy(response.data, r);
            if (writen(fdSocket, &response, sizeof(msg)) == -1)
            {
                fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                break;
            }
            free(param);
            break;
        }

        listUnlockFile(&filesList, param, fdSocket);

        UNLOCK(&filesListLock);

        LOCK(&statsLock);
        howManyUnlocks++;
        UNLOCK(&statsLock);
        char loginfo[1024];
        snprintf(loginfo, 1024, INFO "Client %ld unlocked file %s\n", fdSocket, param);
        printf("%s", loginfo);
        wLog(loginfo);
        //Invia al client che ha effettuato unlock 0
        char *r = "0";
        strcpy(response.data, r);
        if (writen(fdSocket, &response, sizeof(msg)) == -1)
        {
            fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
            break;
        }

        //Il primo client in coda su quel file prende la lock
        LOCK(&queueForLocksLock);
        node *notifyClientNode = searchInQueue(queueForLocks, param);
        if (notifyClientNode == NULL)
        {
            // Nessun client in attesa sul file liberato
            UNLOCK(&queueForLocksLock);
            free(param);
            break;
        }

        long notifiedSocket = atol((char*)notifyClientNode->data);
        if (tryLock(param, notifiedSocket))
        {
            //Invia 0 al nuovo client che ha preso la lock
            char *r = "0";
            strcpy(response.data, r);
            if (writen(notifiedSocket, &response, sizeof(msg)) == -1)
            {
                fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                break;
            }
        }
        else{
            //Invia -1 al nuovo client che non e' riuscito a prendere la lock
            char *res = "-1";
            strcpy(response.data, res);
            if (writen(notifiedSocket, &response, sizeof(msg)) == -1)
            {
                fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                break;
            }
        }
        free(notifyClientNode->data);
        free(notifyClientNode->id);
        free(notifyClientNode);
        UNLOCK(&queueForLocksLock);
        free(param);
        break;

    case CLOSEFILE:
        LOCK(&openFilesListLock);
        if (deleteFile(&openFilesList, param, fdSocket) == 0)
        {
            //File chiuso correttamente
            UNLOCK(&openFilesListLock);

            LOCK(&statsLock);
            howManyCloses++;
            UNLOCK(&statsLock);
            char loginfo[1024];
            snprintf(loginfo, 1024, INFO "File %s chiuso dal client %ld\n", param, fdSocket);
            wLog(loginfo);
            //Invia messaggio valore di ritorno 0 (file chiuso)
            char *r = "0";
            strcpy(response.data, r);
            if (writen(fdSocket, &response, sizeof(msg)) == -1)
            {
                fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                break;
            }

            //Il primo client in coda su quel file prende la lock
            LOCK(&queueForLocksLock);
            node *notifyClientNode = searchInQueue(queueForLocks, param);
            if (notifyClientNode == NULL)
            {
                //Nessun client in attesa sul file liberato
                UNLOCK(&queueForLocksLock);
                free(param);
                break;
            }
            
            long notifiedSocket = atol((char *)notifyClientNode->data);
            if (tryLock(param, notifiedSocket))
            {
                // Invia 0 al nuovo client che ha preso la lock
                char *r = "0";
                strcpy(response.data, r);
                if (writen(notifiedSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
            }
            else
            {
                // Invia -1 al nuovo client che non e' riuscito a prendere la lock
                char *res = "-1";
                strcpy(response.data, res);
                if (writen(notifiedSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
            }
            free(notifyClientNode->data);
            free(notifyClientNode->id);
            free(notifyClientNode);
            UNLOCK(&queueForLocksLock);
        }
        else{
            //Impossibile chiudere il file
            UNLOCK(&openFilesListLock);
            //Invia messaggio valore di ritorno -1 (file non chiuso)
            char *r = "-1";
            strcpy(response.data, r);
            if (writen(fdSocket, &response, sizeof(msg)) == -1)
            {
                fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                break;
            }
        }
        free(param);
        break;

    case REMOVEFILE:
        {
            char *pathname = param;
            LOCK(&filesListLock);
            if (containsFile(filesList, param, fdSocket) == 0)
            {
                UNLOCK(&filesListLock);
                // Il client ha la lock sul file specificato
                if (icl_hash_delete(hashTable, (void *)pathname, free, free) == 0)
                {
                    // File rimosso
                    LOCK(&filesListLock);
                    deleteFile(&filesList, pathname, fdSocket);
                    UNLOCK(&filesListLock);
                    //  Invia messaggio valore di ritorno 0
                    char *r = "0";
                    strcpy(response.data, r);
                    if (writen(fdSocket, &response, sizeof(msg)) == -1)
                    {
                        fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                        break;
                    }
                    char loginfo[1024];
                    snprintf(loginfo, 1024, INFO "Il client %ld ha rimosso il file %s\n", fdSocket, pathname);
                    printf("%s", loginfo);
                    wLog(loginfo);
                    free(pathname);
                    break;
                }
                // File non rimosso
                // Invia messaggio valore di ritorno -1
                char *r = "-1";
                strcpy(response.data, r);
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                fprintf(stderr, WARNING "Impossibile rimuovere il file %s\n", pathname);
                break;
            }
            UNLOCK(&filesListLock);

            // File non presente nel server o il client non ha la lock
            if (icl_hash_find(hashTable, (void*)pathname) == NULL)
            {
                // File non presente
                // Invia messaggio valore di ritorno -1
                char *r = "-1";
                strcpy(response.data, r);
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                fprintf(stderr, WARNING "Impossibile rimuovere il file %s\n", pathname);
                free(pathname);
                break;
            }
            
            if (tryLock(pathname, fdSocket))
            {
                // Lock acquisita
                if (icl_hash_delete(hashTable, (void*)pathname, free, free) == 0)
                {
                    //File rimosso
                    LOCK(&filesListLock);
                    deleteFile(&filesList, pathname, fdSocket);
                    UNLOCK(&filesListLock);
                    //  Invia messaggio valore di ritorno 0
                    char *r = "0";
                    strcpy(response.data, r);
                    if (writen(fdSocket, &response, sizeof(msg)) == -1)
                    {
                        fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                        break;
                    }
                    char loginfo[1024];
                    snprintf(loginfo, 1024, INFO "Il client %ld ha rimosso il file %s\n", fdSocket, pathname);
                    printf("%s", loginfo);
                    wLog(loginfo);
                    free(pathname);
                    break;
                }
                //File non rimosso
                //Invia messaggio valore di ritorno -1
                char *r = "-1";
                strcpy(response.data, r);
                if (writen(fdSocket, &response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, WARNING "Non sono riuscito a rispondere ad un client\n");
                    break;
                }
                fprintf(stderr, WARNING "Impossibile rimuovere il file %s\n",pathname);
                break;
            }
            break;
        }
        break;

    default:
        break;
    }

    for (int i = 0; i < 3; i++)
    {
        free(commands[i]);
    }
    free(commands);
    //Segnala al main di essere terminato
    if (writen(pipe, &fdSocket, sizeof(long)) == -1)
    {
        fprintf(stderr, WARNING "Errore comunicazione thread a main\n");
        return;
    }

    return;
}

char** parseRequest(msg* request){
    char** commands = malloc(sizeof(char*)*3);
    
    commands[0] = strdup(strtok(request->command, ","));
    commands[1] = strdup(strtok(NULL, ","));
    commands[2] = strdup(strtok(NULL, "\t\r\n"));
    return commands;
}

void checkFilesFIFO(char** fileName, void** data){
    LOCK(&memLock);
    if (hashTable->nentries + 1 > conf->numFiles)
    {
        //Devo eliminare il primo file inserito
        LOCK(&filesListLock);
        char* filePath = getLastFile(filesList);
        if (!filePath)
        {
            UNLOCK(&filesListLock);
            UNLOCK(&memLock);
            return;
        }
        //Salvo il contenuto ed il nome del file
        *fileName = strdup(filePath);
        *data = (void *)strdup((char *)icl_hash_find(hashTable, (void *)filePath));

        //Aggiorna info statistiche
        if (maxFiles < hashTable->nentries)
            maxFiles = hashTable->nentries;
        if (maxMemory < hashTable->currentMemory)
            maxMemory = hashTable->currentMemory;
        
        if (icl_hash_delete(hashTable, (void*)*fileName, free, free) == 0)
        {
            if(deleteLastFile(&filesList) == -1)
            {
                fprintf(stderr, ERROR "Impossibile eliminare il file %s dalla lista dei file\n", *fileName);
                UNLOCK(&filesListLock);
                UNLOCK(&memLock);
                return;
            }
            //File rimosso correttamente
            algoExecutions ++;
            UNLOCK(&filesListLock);
            UNLOCK(&memLock);
            char loginfo[1024];
            snprintf(loginfo, 1024, INFO "File %s espulso dal server dall'algoritmo di rimpiazzamento\n", *fileName);
            printf("%s",loginfo);
            wLog(loginfo);
            return;
        }
        fprintf(stderr, ERROR "Impossibile eliminare il file %s dal server\n", *fileName);
        UNLOCK(&filesListLock);
    }
    UNLOCK(&memLock);
    //Nessun file rimosso
    return;
}

void checkMemoryFIFO(queue** expelledFilesQueue, size_t newFileSize){
    printf("FILE SIZE: %ld\n", newFileSize);
    LOCK(&memLock);
    if ((long)newFileSize > hashTable->maxMemory)
    {
        //File troppo grande per il server
        UNLOCK(&memLock);
        char loginfo[1024];
        snprintf(loginfo, 1024, WARNING "Impossibile aggiungere un file di %ld bytes (dimensione massima %ld)\n", (long)newFileSize, hashTable->maxMemory);
        printf("%s", loginfo);
        wLog(loginfo);
        return;
    }

    //Se necessario, libera spazio finche' non puo' essere inserito il nuovo file
    while (hashTable->currentMemory + (long)newFileSize > hashTable->maxMemory)
    {
        LOCK(&filesListLock);
        char* fileToDelete = getLastFile(filesList);
        if (fileToDelete == NULL)
        {
            UNLOCK(&filesListLock);
            UNLOCK(&memLock);
            return;
        }
        if (maxFiles < hashTable->nentries)
            maxFiles = hashTable->nentries;
        if (maxMemory < hashTable->currentMemory)
            maxMemory = hashTable->currentMemory;

        //Salva il file che sta per essere eliminato nella lista
        void* fileData;
        if ((fileData=icl_hash_find(hashTable,(void*) fileToDelete)) == NULL)
        {
            //File non trovato nel server
            UNLOCK(&filesListLock);
            UNLOCK(&memLock);
            char loginfo[1024];
            snprintf(loginfo, 1024, WARNING "File %s da eliminare non trovato\n", fileToDelete);
            printf("%s", loginfo);
            wLog(loginfo);
            return;
        }
        insertQueue(*expelledFilesQueue, strdup(fileToDelete), '~', (void*)strdup((char*)fileData));
        
        //Elimina il file dal server
        if (icl_hash_delete(hashTable, (void*)fileToDelete, free, free) == -1)
        {
            //Errore
            UNLOCK(&filesListLock);
            UNLOCK(&memLock);
            char loginfo[1024];
            snprintf(loginfo, 1024, WARNING "Impossibile eliminare il file %s dal server\n", fileToDelete);
            printf("%s", loginfo);
            wLog(loginfo);
            return;
        }
        if (deleteLastFile(&filesList) == -1)
        {
            //Errore
            UNLOCK(&filesListLock);
            UNLOCK(&memLock);
            char loginfo[1024];
            snprintf(loginfo, 1024, WARNING "Impossibile eliminare il file %s dal server\n", fileToDelete);
            printf("%s", loginfo);
            wLog(loginfo);
            return;
        }

        //File eliminato correttamente ed inserito nella coda dei file espulsi
        UNLOCK(&filesListLock);
        algoExecutions++;
    }
    UNLOCK(&memLock);
    return;
}

bool tryLock(char *fileName, long fdSocket){
    LOCK(&filesListLock);
    if (isLocked(filesList, fileName))
    {
        //File gia' locked
        UNLOCK(&filesListLock);
        return false;
    }
    listLockFile(&filesList, fileName, fdSocket);
    LOCK(&statsLock);
    howManyLocks++;
    UNLOCK(&statsLock);
    UNLOCK(&filesListLock);
    char loginfo[1024];
    snprintf(loginfo, 1024, INFO "Client %ld locked file %s\n", fdSocket, fileName);
    printf("%s", loginfo);
    wLog(loginfo);
    return true;
}