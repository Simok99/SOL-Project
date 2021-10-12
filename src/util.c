#include "util.h"

config* readConfig(char* pathname){
    FILE* f = NULL;
    config* conf = NULL;
    char *buffer;

    if((f = fopen(pathname, "r")) == NULL){
        //fprintf(stderr, "Errore nell'apertura del file configurazione");
        return NULL;
    }

    if((conf = malloc(sizeof(conf))) == NULL){
        fprintf(stderr, "Errore fatale malloc\n");
        return NULL;
    }

    if ((buffer = malloc(sizeof(char)*256)) == NULL){
        fprintf(stderr, "Errore fatale malloc\n");
        return NULL;
    }
    
    //Legge il file di configurazione riga per riga
    while (fgets(buffer, 256, f) != NULL)
    {
        char* separator = "=";
        char* token = strtok(buffer, separator);

        if(strstr(token, "numberOfFiles") != NULL){
            conf->numFiles = atoi(strtok(NULL, separator));
        }

        if (strstr(token, "memoryAllocated") != NULL){
            conf->memorySpace = atol(strtok(NULL, separator));
        }

        if (strstr(token, "numberOfThreads") != NULL){
            conf->numWorkers = atoi(strtok(NULL, separator));
        }

        if (strstr(token, "socketPath") != NULL){
            remSpaces(&token);
            strcpy(conf->socketPath, strtok(NULL, "\r\n"));
        }

        if (strstr(token, "logPath") != NULL){
            remSpaces(&token);
            strcpy(conf->logPath, strtok(NULL, "\r\n"));
        }
    }
    fclose(f);
    free(buffer);
    
    return conf;
}

void remSpaces(char **string)
{
    //La stringa contiene spazi, la ripulisco
    int c = 0, i = 0;
    while (string[i])
    {
        if (*string[i] == ' ')
        {
            string[c++] = string[i];
        }
    }
    string[c] = '\0';
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
        fprintf(stderr, "Impossibile scrivere su disco: Errore nell'apertura del file\n");
        free(tmp_dirname);
        return -1;
    }
    int err;
    if (fwrite(data, sizeof(char), fileSize, stream) < 0)
    {
        SYSCALL_RETURN("fclose", err, fclose(stream), "Impossibile scrivere su disco: Impossibile chiudere il file\n", "");
        return -1;
    }
    if (fclose(stream) != 0)
    {
        fprintf(stderr, "Impossibile scrivere su disco: Errore nella chiusura del file\n");
        free(tmp_dirname);
        return -1;
    }
    free(tmp_dirname);
    return 0;
}