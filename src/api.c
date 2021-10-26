#include "api.h"

/* FILE DESCRIPTOR DELLA SOCKET */
static long fdSocket;

/* DIRECTORY IN CUI SALVARE I FILE ESPULSI DAL SERVER */
char* expelledDir = EXPELLED_FILES_FOLDER;

/* FUNZIONE DI UTILITA' PER CREAZIONE DEI MESSAGGI DA INVIARE */

//Restituisce un messaggio: formato comando "fdSocket,Operazione+parametri,Flags"
msg buildRequest(char **commands, void* data){
    char* command = malloc(sizeof(char)*1024);
    snprintf(command, sizeof(char) * 1024, "%s,%s,%s", commands[0],commands[1],commands[2]);
    msg req;
    memset(&req, 0, sizeof(msg));
    strncpy(req.command, command, 1024);
    if (strcmp((char*)data, "")){
        req.size = 0;
        memcpy(req.data, data, strlen((char*)data));
    }
    else{
        req.size = strlen((char *)data);
        memcpy(req.data, data, strlen((char *)data));
    }
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
        fprintf(stderr, "Errore: pathname non valido\n");
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

    msg request = buildRequest(params, "");
    if (writen(fdSocket, (void*)&request, sizeof(msg)) != 1)
    {
        fprintf(stderr, "Errore nell'invio della richiesta al server\n");
        return -1;
    }
    for (int i = 0; i < 3; i++)
    {
        free(params[i]);
    }
    free(params);

    int nexpelled;
    if (readn(fdSocket, (void *)&nexpelled, sizeof(int)) == -1){
        fprintf(stderr, "Errore nella ricezione della risposta del server\n");
        return -1;
    }

    msg response;
    if(readn(fdSocket, (void*)&response, sizeof(msg)) == -1){
        fprintf(stderr, "Errore nella ricezione della risposta del server\n");
        return -1;
    }

    //Se non ci sono file espulsi la risposta del server non avra' command
    char* command = strdup(response.command);
    char* data = strdup(response.data);

    if (nexpelled == 0)
    {
        if (strcmp(data, "-1") == 0)
        {
            fprintf(stderr, "Errore nella scrittura del file %s (errore server)\n", pathname);
            free(command);
            free(data);
            return -1;
        }
        else if (strcmp(data, "ALREADYINSTORAGE") == 0)
        {
            fprintf(stderr, "Errore nella scrittura del file %s (file presente nello storage)\n", pathname);
            free(command);
            free(data);
            return -1;
        }
        else if (strcmp(data, "NOTFOUND") == 0)
        {
            fprintf(stderr, "Errore in apertura del file %s (file non trovato)\n", pathname);
            free(command);
            free(data);
            return -1;
        }
        else if (strcmp(data, "ALREADYLOCKED") == 0)
        {
            fprintf(stderr, "Errore in apertura del file %s (file gia' aperto dal client)\n", pathname);
            free(command);
            free(data);
            return -1;
        }
        else if (strcmp(data, "0") == 0)
        {
            free(command);
            free(data);
            return 0;
        }
    }
    else{
        
        //Uno o piu' file sono stati espulsi
        int i = 0;
        while (i < nexpelled)
        {
            char *command = strdup(response.command);
            char *data = strdup(response.data);
            if (writeOnDisk(command, (void *)data, expelledDir, response.size) != 0)
            {
                fprintf(stderr, "Impossibile salvare il file %s inviato dal server nella cartella %s\n", command, expelledDir);
            }
            printf("File %s espulso dal server salvato nella cartella %s\n", command, expelledDir);
            free(command);
            free(data);
            i++;
            if (i<nexpelled)
            {
                //Devo leggere un altro file
                if (readn(fdSocket, (void *)&response, sizeof(msg)) == -1)
                {
                    fprintf(stderr, "Errore nella ricezione della risposta del server\n");
                    break;
                }
            }
            //File terminati
            else break;
        }
        free(command);
        free(data);
        return 0;
    }
    free(command);
    free(data);
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
    for (int i = 0; i < 3; i++)
    {
        params[i] = malloc(sizeof(char) * 1024);
    }

    snprintf(params[0], sizeof(long), "%ld", fdSocket);
    snprintf(params[1], sizeof(char) * 1024, "%d %s", READFILE, pathname);
    snprintf(params[2], sizeof(int), "%d", NOFLAGS);

    msg request = buildRequest(params, "");
    if (writen(fdSocket, (void *)&request, sizeof(msg)) != 1)
    {
        fprintf(stderr, "Errore nell'invio della richiesta al server\n");
        return -1;
    }
    for (int i = 0; i < 3; i++)
    {
        free(params[i]);
    }
    free(params);

    msg response;
    if (readn(fdSocket, (void*)&response, sizeof(msg)) == -1)
    {
        fprintf(stderr, "Errore nella ricezione della risposta del server\n");
        return -1;
    }

    char *command = strdup(response.command);
    if (strcmp(command, "NOTINSTORAGE") == 0)
    {
        fprintf(stderr, "File %s non trovato nel server\n", pathname);
        free(command);
        return -1;
    }
    else if (strcmp(command, "NOTOPEN") == 0)
    {
        fprintf(stderr, "Impossibile leggere il file %s (lo hai aperto?)\n", pathname);
        free(command);
        return -1;
    }

    // La risposta del server sara' il contenuto del file e la sua lunghezza
    *size = response.size;
    *buf = malloc(sizeof(char)*response.size+1);
    memcpy(*buf, response.data, response.size+1);
    free(command);
    return 0;
}

int readNFiles(int N, const char *dirname){
    if (dirname == NULL)
    {
        fprintf(stderr, "Errore: pathname cartella non valido\n");
        errno = EINVAL;
        return -1;
    }

    char **params = malloc(sizeof(char *) * 3);
    for (int i = 0; i < 3; i++)
    {
        params[i] = malloc(sizeof(char) * 1024);
    }

    snprintf(params[0], sizeof(long), "%ld", fdSocket);
    snprintf(params[1], sizeof(char) * 1024, "%d %d", READNFILES, N);
    snprintf(params[2], sizeof(int), "%d", NOFLAGS);

    msg request = buildRequest(params, "");
    if (writen(fdSocket, (void *)&request, sizeof(msg)) != 1)
    {
        fprintf(stderr, "Errore nell'invio della richiesta al server\n");
        return -1;
    }
    for (int i = 0; i < 3; i++)
    {
        free(params[i]);
    }
    free(params);

    //La risposta del server sara' numero di file letti + contenuti
    int filesRead;
    if (readn(fdSocket, (void *)&filesRead, sizeof(int)) == -1)
    {
        fprintf(stderr, "Errore nella ricezione della risposta del server\n");
        return -1;
    }

    for (int i = 0; i < filesRead; i++)
    {
        msg response;
        if (readn(fdSocket, (void *)&response, sizeof(msg)) == -1)
        {
            fprintf(stderr, "Errore nella ricezione della risposta del server\n");
            return -1;
        }

        void* buffer = malloc(sizeof(char)*response.size+1);
        memcpy(buffer, response.data, response.size+1);
        //Salva il file nella cartella passata come parametro
        if (writeOnDisk(response.command, buffer, dirname, response.size+1) == 0)
        {
            printf("File %s letto dal server e salvato nella cartella %s\n", response.command, dirname);
        }
        else {
            fprintf(stderr, "Impossibile salvare il file %s letto dal server nella cartella %s\n", response.command, dirname);
        }
        free(buffer);
        
    }
    return 0;
}

int writeFile(const char *pathname, const char *dirname){
    if (pathname == NULL)
    {
        fprintf(stderr, "Errore: pathname file non valido\n");
        errno = EINVAL;
        return -1;
    }

    char **params = malloc(sizeof(char *) * 3);
    for (int i = 0; i < 3; i++)
    {
        params[i] = malloc(sizeof(char) * 1024);
    }

    snprintf(params[0], sizeof(long), "%ld", fdSocket);
    snprintf(params[1], sizeof(char) * 1024, "%d %s", WRITEFILE, pathname);
    snprintf(params[2], sizeof(int), "%d", NOFLAGS);

    //Salva il contenuto del file in un buffer
    FILE* f = fopen(pathname, "rb");
    if (f == NULL)
    {
        fprintf(stderr, "Errore: impossibile aprire il file %s\n",pathname);
        errno = EINVAL;
        fclose(f);
        return -1;
    }
    char *buffer = malloc(sizeof(char)*MAX_FILE_SIZE);
    if (buffer == NULL)
    {
        fprintf(stderr, "Errore fatale malloc\n");
        errno = EINVAL;
        fclose(f);
        return -1;
    }
    long length = fread(buffer, sizeof(char), MAX_FILE_SIZE, f);
    if (length < 0)
    {
        fprintf(stderr, "Errore: impossibile leggere il file %s\n",pathname);
        errno = EINVAL;
        fclose(f);
        return -1;
    }
    fclose(f);
    
    msg request = buildRequest(params, "");
    strncpy(request.data, buffer, length);
    request.size = length;

    if (writen(fdSocket, (void *)&request, sizeof(msg)) != 1)
    {
        fprintf(stderr, "Errore nell'invio della richiesta al server\n");
        return -1;
    }

    for (int i = 0; i < 3; i++)
    {
        free(params[i]);
    }
    free(params);
    free(buffer);

    msg response;
    if (readn(fdSocket, (void*)&response, sizeof(msg)) == -1)
    {
        fprintf(stderr, "Errore nella ricezione della risposta del server\n");
        return -1;
    }

    bool updated = false;
    char* data = strdup(response.data);
    char* command = strdup(response.command);
    if (strcmp(data, "") == 0)
    {
        if (strcmp(command, "TOOBIG") == 0)
        {
            fprintf(stderr, "File %s troppo grande per il server (modificabile da util.h)\n", pathname);
        }
        else if (strcmp(command, "FAILED") == 0)
        {
            fprintf(stderr, "File %s rimosso dal server (impossibile ripristinare un backup)\n", pathname);
        }
        else if (strcmp(command, "UNCHANGED") == 0)
        {
            fprintf(stderr, "Impossibile aggiornare file %s (ripristinato a versione precendete)\n", pathname);
        }
        else
            // File aggiornato correttamente
            updated = true;
    }

    //Se il server ha inviato file indietro, sono salvati nella cartella expelledFiles
    //Se dirname != NULL, li devo copiare anche in dirname
    if (dirname != NULL)
    {
        //TODO fix
        int src_fd, dst_fd, n, err;
        unsigned char buffer[4096];
        src_fd = open(EXPELLED_FILES_FOLDER, O_RDONLY);
        dst_fd = open(dirname, O_CREAT | O_WRONLY);

        while (true)
        {
            err = read(src_fd, buffer, 4096);
            if (err == -1)
            {
                printf("Errore in lettura della cartella %s\n", EXPELLED_FILES_FOLDER);
                close(dst_fd);
                break;
            }
            n = err;

            if (n == 0){
                close(src_fd);
                close(dst_fd);
                break;
            }

            err = write(dst_fd, buffer, n);
            if (err == -1)
            {
                printf("Errore in scrittura nella cartella %s\n", dirname);
                close(src_fd);
                break;
            }
        }
    }
    
    free(data);
    free(command);
    if (updated) return 0;
    else return -1;
}

int appendToFile(const char *pathname, void *buf, size_t size, const char *dirname){
    if (!pathname)
    {
        fprintf(stderr, "appendToFile: Pathname non valido\n");
        errno = EINVAL;
        return -1;
    }
    if (!buf)
    {
        fprintf(stderr, "appendToFile: Contenuto da appendere non valido\n");
        errno = EINVAL;
        return -1;
    }

    if (openFile(pathname, O_LOCK) != 0)
    {
        fprintf(stderr, "appendToFile: Errore nell'apertura del file %s\n",pathname);
        return -1;
    }
    

    char **params = malloc(sizeof(char *) * 3);
    for (int i = 0; i < 3; i++)
    {
        params[i] = malloc(sizeof(char) * 1024);
    }

    snprintf(params[0], sizeof(long), "%ld", fdSocket);
    snprintf(params[1], sizeof(char) * 1024, "%d %s", APPENDTOFILE, pathname);
    snprintf(params[2], sizeof(int), "%d", NOFLAGS);

    msg request = buildRequest(params, "");
    strncpy(request.data, (char*)buf, size);

    if (writen(fdSocket, (void *)&request, sizeof(msg)) != 1)
    {
        fprintf(stderr, "Errore nell'invio della richiesta al server\n");
        return -1;
    }

    for (int i = 0; i < 3; i++)
    {
        free(params[i]);
    }
    free(params);

    msg response;
    if (readn(fdSocket, (void *)&response, sizeof(msg)) == -1)
    {
        fprintf(stderr, "Errore nella ricezione della risposta del server\n");
        return -1;
    }

    bool updated = false;
    char *data = strdup(response.data);
    char *command = strdup(response.command);
    if (strcmp(data, "") == 0)
    {
        if (strcmp(command, "TOOBIG") == 0)
        {
            fprintf(stderr, "File %s troppo grande per il server (modificabile da util.h)\n", pathname);
        }
        else if (strcmp(command, "FAILED") == 0)
        {
            fprintf(stderr, "File %s rimosso dal server (impossibile ripristinare un backup)\n", pathname);
        }
        else if (strcmp(command, "UNCHANGED") == 0)
        {
            fprintf(stderr, "Impossibile aggiornare file %s (ripristinato a versione precendete)\n", pathname);
        }
        else
            // File aggiornato correttamente
            updated = true;
    }

    // Se il server ha inviato file indietro, sono salvati nella cartella expelledFiles
    // Se dirname != NULL, li devo copiare anche in dirname
    if (dirname != NULL)
    {
        // TODO fix
        int src_fd, dst_fd, n, err;
        unsigned char buffer[4096];
        src_fd = open(EXPELLED_FILES_FOLDER, O_RDONLY);
        dst_fd = open(dirname, O_CREAT | O_WRONLY);

        while (true)
        {
            err = read(src_fd, buffer, 4096);
            if (err == -1)
            {
                printf("Errore in lettura della cartella %s\n", EXPELLED_FILES_FOLDER);
                close(dst_fd);
                break;
            }
            n = err;

            if (n == 0)
            {
                close(src_fd);
                close(dst_fd);
                break;
            }

            err = write(dst_fd, buffer, n);
            if (err == -1)
            {
                printf("Errore in scrittura nella cartella %s\n", dirname);
                close(src_fd);
                break;
            }
        }
    }

    free(data);
    free(command);
    if (updated) return 0;
    else return -1;
}

int lockFile(const char *pathname){
    if (pathname == NULL)
    {
        fprintf(stderr, "Errore: pathname non valido\n");
        errno = EINVAL;
        return -1;
    }

    char **params = malloc(sizeof(char *) * 3);
    for (int i = 0; i < 3; i++)
    {
        params[i] = malloc(sizeof(char) * 1024);
    }

    snprintf(params[0], sizeof(long), "%ld", fdSocket);
    snprintf(params[1], sizeof(char) * 1024, "%d %s", LOCKFILE, pathname);
    snprintf(params[2], sizeof(int), "%d", NOFLAGS);

    msg request = buildRequest(params, "");
    if (writen(fdSocket, (void *)&request, sizeof(msg)) != 1)
    {
        fprintf(stderr, "Errore nell'invio della richiesta al server\n");
        return -1;
    }
    for (int i = 0; i < 3; i++)
    {
        free(params[i]);
    }
    free(params);

    msg response;
    if (readn(fdSocket, (void *)&response, sizeof(msg)) == -1)
    {
        fprintf(stderr, "Errore nella ricezione della risposta del server\n");
        return -1;
    }

    char *data = strdup(response.data);
    if (strcmp(data, "NOTFOUND") == 0)
    {
        fprintf(stderr, "File %s non trovato nel server\n", pathname);
        free(data);
        return -1;
    }
    else if (strcmp(data, "ALREADYLOCKED") == 0)
    {
        fprintf(stderr, "Il file %s e' gia' locked\n", pathname);
        free(data);
        return -1;
    }
    else if (strcmp(data, "0") == 0)
    {
        free(data);
        return 0;
    }
    //Errore
    free(data);
    return -1;
}

int unlockFile(const char *pathname){
    if (pathname == NULL)
    {
        fprintf(stderr, "Errore: pathname non valido\n");
        errno = EINVAL;
        return -1;
    }

    char **params = malloc(sizeof(char *) * 3);
    for (int i = 0; i < 3; i++)
    {
        params[i] = malloc(sizeof(char) * 1024);
    }

    snprintf(params[0], sizeof(long), "%ld", fdSocket);
    snprintf(params[1], sizeof(char) * 1024, "%d %s", UNLOCKFILE, pathname);
    snprintf(params[2], sizeof(int), "%d", NOFLAGS);

    msg request = buildRequest(params, "");
    if (writen(fdSocket, (void *)&request, sizeof(msg)) != 1)
    {
        fprintf(stderr, "Errore nell'invio della richiesta al server\n");
        return -1;
    }
    for (int i = 0; i < 3; i++)
    {
        free(params[i]);
    }
    free(params);

    msg response;
    if (readn(fdSocket, (void *)&response, sizeof(msg)) == -1)
    {
        fprintf(stderr, "Errore nella ricezione della risposta del server\n");
        return -1;
    }

    char *data = strdup(response.data);
    if (strcmp(data, "-1") == 0)
    {
        fprintf(stderr, "File %s non trovato nel server, o non hai la lock sul file da rilasciare\n", pathname);
        free(data);
        return -1;
    }
    else if (strcmp(data, "0") == 0)
    {
        free(data);
        return 0;
    }
    // Errore
    free(data);
    return -1;
}

int closeFile(const char *pathname){
    if (pathname == NULL)
    {
        fprintf(stderr, "Errore: pathname non valido\n");
        errno = EINVAL;
        return -1;
    }

    char **params = malloc(sizeof(char *) * 3);
    for (int i = 0; i < 3; i++)
    {
        params[i] = malloc(sizeof(char) * 1024);
    }

    snprintf(params[0], sizeof(long), "%ld", fdSocket);
    snprintf(params[1], sizeof(char) * 1024, "%d %s", CLOSEFILE, pathname);
    snprintf(params[2], sizeof(int), "%d", NOFLAGS);

    msg request = buildRequest(params, "");
    if (writen(fdSocket, (void *)&request, sizeof(msg)) != 1)
    {
        fprintf(stderr, "Errore nell'invio della richiesta al server\n");
        return -1;
    }
    for (int i = 0; i < 3; i++)
    {
        free(params[i]);
    }
    free(params);

    msg response;
    if (readn(fdSocket, (void *)&response, sizeof(msg)) == -1)
    {
        fprintf(stderr, "Errore nella ricezione della risposta del server\n");
        return -1;
    }

    // La risposta del server sara' "0" o "-1"
    if (strcmp(response.data, "0") == 0)
        return 0;

    return -1;
}

int removeFile(const char *pathname){
    if (pathname == NULL)
    {
        fprintf(stderr, "Errore: pathname non valido\n");
        errno = EINVAL;
        return -1;
    }

    char **params = malloc(sizeof(char *) * 3);
    for (int i = 0; i < 3; i++)
    {
        params[i] = malloc(sizeof(char) * 1024);
    }

    snprintf(params[0], sizeof(long), "%ld", fdSocket);
    snprintf(params[1], sizeof(char) * 1024, "%d %s", REMOVEFILE, pathname);
    snprintf(params[2], sizeof(int), "%d", NOFLAGS);

    msg request = buildRequest(params, "");
    if (writen(fdSocket, (void *)&request, sizeof(msg)) != 1)
    {
        fprintf(stderr, "Errore nell'invio della richiesta al server\n");
        return -1;
    }
    for (int i = 0; i < 3; i++)
    {
        free(params[i]);
    }
    free(params);

    msg response;
    if (readn(fdSocket, (void *)&response, sizeof(msg)) == -1)
    {
        fprintf(stderr, "Errore nella ricezione della risposta del server\n");
        return -1;
    }

    // La risposta del server sara' "0" o "-1"
    if (strcmp(response.data, "0") == 0)
        return 0;

    return -1;
}