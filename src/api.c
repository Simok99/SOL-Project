#include "api.h"

/* FILE DESCRIPTOR DELLA SOCKET */
static long fdSocket;

/* DIRECTORY IN CUI SALVARE I FILE ESPULSI DAL SERVER */
char* expelledDir = EXPELLED_FILES_FOLDER;

/* FUNZIONE DI UTILITA' PER CREAZIONE DEI MESSAGGI DA INVIARE */

//Restituisce un messaggio: formato comando "fdSocket,Operazione+parametri,Flags"
msg buildRequest(char **commands, void* data){
    void* command = malloc(sizeof(char)*1024);
    snprintf(command, sizeof(char) * 1024, "%s,%s,%s", commands[0],commands[1],commands[2]);
    msg req;
    req.command = command;
    if (!data)
        req.size = 0;
    else
        req.size = strlen((char *)data);
    req.data = data;
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
    for (int i = 0; i < 3; i++)
    {
        params[i] = malloc(sizeof(char)*1024);
    }
    
    snprintf(params[0], sizeof(long), "%ld", fdSocket);
    snprintf(params[1], sizeof(char)*1024, "%d %s", OPENFILE, pathname);
    snprintf(params[2], sizeof(int), "%d", flags);

    msg request = buildRequest(params, NULL);
    if (writen(fdSocket, (void*)&request, sizeof(request)) != 1)
    {
        fprintf(stderr, "Errore nell'invio della richiesta al server\n");
        return -1;
    }
    for (int i = 0; i < 3; i++)
    {
        free(params[i]);
    }
    free(params);
    free(request.command);
    free(request.data);

    msg response;
    if(readn(fdSocket, &(response), sizeof(msg)) == -1){
        fprintf(stderr, "Errore nella ricezione della risposta del server\n");
        return -1;
    }

    //Se non ci sono file espulsi la risposta del server sara' 0
    char* command = malloc(response.size);
    strcpy(command, (char*)response.data);
    if (strcmp(command, "-1"))
    {
        fprintf(stderr, "Errore nella scrittura del file %s\n", pathname);
        free(command);
        return -1;
    }
    else if (strcmp(command, "0"))
    {
        free(command);
        return 0;
    }
    else{
        //Ho ricevuto file espulsi indietro
        char *fileName = (char *)response.command;
        if (writeOnDisk(fileName, response.data, expelledDir, response.size) != 0)
        {
            fprintf(stderr, "Impossibile salvare il file %s inviato dal server nella cartella %s\n", fileName, expelledDir);
        }
        printf("File %s espulso dal server salvato nella cartella %s\n", fileName, expelledDir);
        while (readn(fdSocket, &(response), sizeof(response)) != 0)
        {
            fileName = realloc(fileName, strlen((char*)response.command));
            strcpy(fileName, (char*)response.command);
            if (writeOnDisk(fileName, response.data, expelledDir, response.size) != 0)
            {
                fprintf(stderr, "Impossibile salvare il file %s inviato dal server nella cartella %s\n", fileName, expelledDir);
                continue;
            }
            printf("File %s espulso dal server salvato nella cartella %s\n", fileName, expelledDir);
        }
        free(command);
        return 0;
    }
    free(command);
    return -1;
}

int readFile(const char *pathname, void **buf, size_t *size){
    if (pathname == NULL)
    {
        fprintf(stderr, "Errore: pathname file non valido\n");
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

int readNFiles(int N, const char *dirname){
    //TODO Implement
    return -1;
}

int writeFile(const char *pathname, const char *dirname){
    if (pathname == NULL)
    {
        fprintf(stderr, "Errore: pathname file non valido\n");
        errno = EINVAL;
        return -1;
    }

    char **params = malloc(sizeof(char *) * 3);
    snprintf(params[0], sizeof(long), "%ld", fdSocket);
    char *operation = malloc(sizeof(char) * 512);
    snprintf(operation, sizeof(char) * 512, "%u %s", WRITEFILE, realpath(pathname, NULL));
    params[1] = operation;
    params[2] = NULL;

    //Salva il contenuto del file in un buffer
    FILE* f = fopen(pathname, "rb");
    char *buffer = 0;
    long length;

    if (f)
    {
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        fseek(f, 0, SEEK_SET);
        buffer = malloc(length);
        if (buffer)
        {
            fread(buffer, 1, length, f);
        }
        fclose(f);
    }

    if (!buffer)
    {
        fprintf(stderr, "Errore: impossibile leggere il file %s da inviare al server\n",pathname);
        errno = EXIT_FAILURE;
        return -1;
    }
    
    msg request = buildRequest(params, (void*)buffer);

    if (writen(fdSocket, (void *)&request, sizeof(request)) != 1)
    {
        fprintf(stderr, "Errore nell'invio della richiesta al server\n");
        return -1;
    }
    free(params);
    free(buffer);

    msg response;
    if (readn(fdSocket, &(response), sizeof(response)) == -1)
    {
        fprintf(stderr, "Errore nella ricezione della risposta del server\n");
        return -1;
    }

    if (response.size==0)
    {
        //Nessun file espulso da parte del server
        if (strcmp(response.command, "FAILED"))
        {
            //Se command=="FAILED" 
            fprintf(stderr, "Impossibile caricare file %s sul server (SERVER_ERROR)\n", pathname);
            return -1;
        }
        else if (strcmp(response.command, "UNCHANGED"))
        {
            fprintf(stderr, "Impossibile aggiornare il file %s sul server (SERVER_ERROR)\n", pathname);
            return -1;
        }
        
        return 0;
    }

    //Il server ha inviato dati indietro, sono relativi ad un file espulso con nome=command, size=size e contenuto=data
    //Il server ha espulso un file, se dirname != NULL lo devo salvare in locale
    if (dirname != NULL)
    {
        char *fileName = (char *)response.command;
        if (writeOnDisk(fileName, response.data, dirname, response.size) != 0)
        {
            fprintf(stderr, "Impossibile salvare il file %s inviato dal server nella cartella %s\n", fileName, dirname);
            return 0;
        }
        fprintf(stderr, "File %s espulso dal server salvato nella cartella %s\n", fileName, dirname);
        return 0;
    }
    
    //Il server ha inviato un file espulso, ma non devo salvarlo lato client
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