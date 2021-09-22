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
            strcpy(conf->socketPath, strtok(NULL, "\r\n"));
        }

        if (strstr(token, "logPath") != NULL){
            strcpy(conf->logPath, strtok(NULL, "\r\n"));
        }
    }

    printf("PATH=%s0\n", conf->logPath);
    fclose(f);
    free(buffer);
    
    return conf;
}