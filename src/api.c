#include "api.h"

/* FILE DESCRIPTOR DELLA SOCKET */
static long fdSocket;

/* FUNZIONE DI UTILITA' PER CREAZIONE DEI MESSAGGI DA INVIARE */

//Restituisce un messaggio: formato dati "fdSocket,Operazione+parametri,Flags"
msg buildRequest(char **data){
    void *buffer;
    for (int i = 0; i < sizeof(data) / sizeof(data[0]); i++)
    {
        snprintf(buffer, sizeof(data), "%s,", data[i]);
    }
    msg req;
    req.size = sizeof(buffer);
    req.data = buffer;
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

    char* params[2];
    snprintf(params[0], sizeof(long), "%ld", fdSocket);
    char* operation;
    strcpy(operation, "OPENFILE ");
    strcat(operation, realpath(pathname, NULL));
    params[1] = operation;
    snprintf(params[2], sizeof(int), "%d", flags);

    msg request = buildRequest(params);

    if (writen(fdSocket, (void*)&request, sizeof(request)) != 1)
    {
        fprintf(stderr, "Errore nell'invio della richiesta al server");
        return -1;
    }
    
    msg response;
    if(readn(fdSocket, &response, sizeof(response))){
        fprintf(stderr, "Errore nella ricezione della risposta del server");
        return -1;
    }

    return 0;
}