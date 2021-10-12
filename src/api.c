#include "api.h"

/* FILE DESCRIPTOR DELLA SOCKET */
static long fdSocket;

/* FUNZIONE DI UTILITA' PER CREAZIONE DEI MESSAGGI DA INVIARE */

//Restituisce un messaggio: formato comando "fdSocket,Operazione+parametri,Flags"
msg buildRequest(char **commands, void* data){
    void* command = malloc(sizeof(char)*1024);
    for (int i = 0; i < 3; i++)
    {
        if (commands[i] == NULL)
        {
            sprintf(command, "$NULL");
            continue;
        }
        
        sprintf(command, "%s,", commands[i]);
    }
    msg req;
    req.command = command;
    req.size = sizeof(data);
    req.data = data;
    free(command);
    return req;
}

/* IMPLEMENTAZIONE FUNZIONI API */

int openConnection(const char *sockname, int msec, const struct timespec abstime){
    if (sockname == NULL)
    {
        fprintf(stderr, "Impossibile connettersi al server: socket path non valido\n");
        errno = EINVAL;
        return -1;
    }
    struct timespec currentTime;
    fdSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fdSocket == -1)
    {
        fprintf(stderr, "Impossibile connettersi al server: errore socket\n");
        errno = ENETUNREACH;
        return -1;
    }
    struct sockaddr_un sa;
    memset(&sa, '0', sizeof(sa));
    strncpy(sa.sun_path, sockname, strlen(sockname) + 1);
    sa.sun_family = AF_UNIX;
    if(clock_gettime(CLOCK_REALTIME, &currentTime) == -1){
        fprintf(stderr, "Impossibile connettersi al server: errore clock\n");
        return -1;
    }

    bool connected = false;
    //Prova a connettersi al server ogni "msec" millisecondi fino al tempo massimo "abstime"
    while (abstime.tv_sec > currentTime.tv_sec)
    {
        if (connect(fdSocket, (struct sockaddr*)&sa, sizeof(sa)) != 0)
        {
            usleep(msec * 1000);
        }
        else {
            connected = true;
            break;
        }
        if (clock_gettime(CLOCK_REALTIME, &currentTime) == -1)
        {
            fprintf(stderr, "Impossibile connettersi al server: errore clock\n");
            return -1;
        }
    }
    
    //Tempo massimo ecceduto, timeout
    errno = ETIMEDOUT;
    if (connected) return 0;
    return -1;
}

int closeConnection(const char *sockname){
    if (sockname == NULL)
    {
        fprintf(stderr, "Errore in chiusura del file descriptor: path socket non valido\n");
        errno = EINVAL;
        return -1;
    }
    if (close(fdSocket) == -1) {
        fprintf(stderr, "Errore in chiusura del file descriptor della socket\n");
        return -1;
    }

    return 0;
}

int openFile(const char *pathname, int flags){
    if (pathname == NULL)
    {
        fprintf(stderr, "Errore: pathname socket non valido\n");
        errno = EINVAL;
        return -1;
    }

    char** params = malloc(sizeof(char*)*3);
    snprintf(params[0], sizeof(long), "%ld", fdSocket);
    char* operation = malloc(sizeof(char)*512);
    snprintf(operation, sizeof(char)*512, "%u %s", OPENFILE, realpath(pathname, NULL));
    params[1] = operation;
    free(operation);
    snprintf(params[2], sizeof(int), "%d", flags);

    msg request = buildRequest(params, NULL);
    free(params);

    if (writen(fdSocket, (void*)&request, sizeof(request)) != 1)
    {
        fprintf(stderr, "Errore nell'invio della richiesta al server\n");
        return -1;
    }
    
    msg response;
    if(readn(fdSocket, &(response), sizeof(response)) == -1){
        fprintf(stderr, "Errore nella ricezione della risposta del server\n");
        return -1;
    }

    //La risposta del server sara' un intero
    int replyCode = atoi((char*)response.data);
    if (replyCode == 0) return 0;
    
    return -1;
}

int readFile(const char *pathname, void **buf, size_t *size){
    if (pathname == NULL)
    {
        fprintf(stderr, "Errore: pathname socket non valido\n");
        errno = EINVAL;
        return -1;
    }

    char **params = malloc(sizeof(char *) * 3);
    snprintf(params[0], sizeof(long), "%ld", fdSocket);
    char *operation = malloc(sizeof(char)*512);
    snprintf(operation, sizeof(char) * 512, "%u %s", READFILE, realpath(pathname, NULL));
    params[1] = operation;
    params[2] = NULL;

    msg request = buildRequest(params, NULL);
    free(params);

    if (writen(fdSocket, (void *)&request, sizeof(request)) != 1)
    {
        fprintf(stderr, "Errore nell'invio della richiesta al server\n");
        return -1;
    }

    msg response;
    if (readn(fdSocket, &(response), sizeof(response)) == -1)
    {
        fprintf(stderr, "Errore nella ricezione della risposta del server\n");
        return -1;
    }

    //La risposta del server sara' il contenuto del file e la sua lunghezza
    *size = response.size;
    void* data = response.data;
    //TODO size+1 in memcpy?
    memcpy(*buf, data, response.size);
    return 0;
}





int closeFile(const char *pathname){
    if (pathname == NULL)
    {
        fprintf(stderr, "Errore: pathname socket non valido\n");
        errno = EINVAL;
        return -1;
    }

    char **params = malloc(sizeof(char *) * 3);
    snprintf(params[0], sizeof(long), "%ld", fdSocket);
    char *operation = malloc(sizeof(char) * 512);
    snprintf(operation, sizeof(char) * 512, "%u %s", CLOSEFILE, realpath(pathname, NULL));
    params[1] = operation;
    params[2] = NULL;

    msg request = buildRequest(params, NULL);
    free(params);

    if (writen(fdSocket, (void *)&request, sizeof(request)) != 1)
    {
        fprintf(stderr, "Errore nell'invio della richiesta al server\n");
        return -1;
    }

    msg response;
    if (readn(fdSocket, &(response), sizeof(response)) == -1)
    {
        fprintf(stderr, "Errore nella ricezione della risposta del server\n");
        return -1;
    }

    //La risposta del server sara' un intero
    int replyCode = *((int *)response.data);
    if (replyCode == 0)
        return 0;

    return -1;
}