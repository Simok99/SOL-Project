#include "util.h"

config* readConfig(char* pathname){
    FILE* f = NULL;
    config* conf = NULL;
    char *buffer;

    if((f = fopen(pathname, "r")) == NULL){
        fprintf(stderr, "Errore nell'apertura del file configurazione");
        return NULL;
    }

    conf = malloc(sizeof(config));
    if (conf == NULL)
    {
        fprintf(stderr, "Errore fatale malloc\n");
        return NULL;
    }

    memset(conf->logPath, '\0', sizeof(conf->logPath));
    memset(conf->socketPath, '\0', sizeof(conf->socketPath));

    buffer = malloc(sizeof(char) * 1024);
    if (buffer == NULL)
    {
        fprintf(stderr, "Errore fatale malloc\n");
        return NULL;
    }

    //Legge il file di configurazione riga per riga
    while (fgets(buffer, 1024, f) != NULL)
    {
        if (strstr(buffer, "**"))
        {
            //E' un commento, lo ignoro
            continue;
        }

        char* separator = "=";
        char* param = strtok(buffer, separator);
        char* value = strtok(NULL, "\r\t\n");
        param = remSpaces(param);
        value = remSpaces(value);

        if(strcmp(param, "numberOfFiles") == 0){
            conf->numFiles = atoi(value);
            continue;
        }

        if (strcmp(param, "memoryAllocated") == 0){
            conf->memorySpace = atol(value);
            continue;
        }

        if (strcmp(param, "numberOfThreads") == 0){
            conf->numWorkers = atoi(value);
            continue;
        }

        if (strcmp(param, "socketPath") == 0){
            strcpy(conf->socketPath, value);
            continue;
        }

        if (strcmp(param, "logPath") == 0){
            strcpy(conf->logPath, value);
            continue;
        }
    }
    fclose(f);
    free(buffer);
    
    return conf;
}

char* remSpaces(char *string)
{
    //La stringa contiene spazi, la ripulisco
    int i = 0, j = 0;
    while (string[i])
    {
        if (string[i] != ' ')
        {
            string[j++] = string[i];
        }
        i++;
    }
    string[j] = '\0';
    return string;
}

int writeOnDisk(char *pathname, void *data, const char *dirname, size_t fileSize)
{
    if (!pathname)
    {
        fprintf(stderr, "Impossibile scrivere su disco: Pathname non valido\n");
        errno = EINVAL;
        return -1;
    }
    //Controllo che la cartella esista
    char *tmp_dirname = realpath(dirname, NULL); //Prendo path assoluto, se esiste
    if (!tmp_dirname)
    {
        DIR *dir = opendir(dirname);
        if (dir == NULL)
        {
            //Cartella non esiste, la creo
            if (mkdir(dirname, 0777) != 0)
            {
                fprintf(stderr, "Impossibile scrivere su disco: Impossibile creare la cartella %s\n", dirname);
                return -1;
            }
            tmp_dirname = realpath(dirname, NULL);
        }
    }
    FILE *stream = NULL;
    strcat(tmp_dirname, "/");
    char *fileName = basename(pathname); //Prende il nome del file da pathname
    if ((stream = fopen(strcat(tmp_dirname, fileName), "wb")) == NULL)
    {
        fprintf(stderr, "Impossibile scrivere su disco: Errore nell'apertura del file %s\n", pathname);
        free(tmp_dirname);
        return -1;
    }
    int err;
    if (fwrite(data, sizeof(char), fileSize, stream) < 0)
    {
        SYSCALL_RETURN("fclose", err, fclose(stream), "Impossibile scrivere su disco: Impossibile chiudere il file %s\n", pathname);
        return -1;
    }
    if (fclose(stream) != 0)
    {
        fprintf(stderr, "Impossibile scrivere su disco: Errore nella chiusura del file %s\n", pathname);
        free(tmp_dirname);
        return -1;
    }
    free(tmp_dirname);
    return 0;
}