#if !defined(_FILELIST_H)
#define _FILELIST_H

#include "util.h"

//Struttura che rappresenta una lista di file, path è chiave univoca per un file come da testo (path assoluto)
typedef struct file
{
    char path[MAX_PATH_LENGTH];
    unsigned int size;
    unsigned int status;
    struct file* next;
} fileList;

//Metodo utilizzato per inizializzare una lista di file
void initFileList(fileList* list);

//Metodo utilizzato per inserire un file in una lista, restituisce -1 se già presente (path già utilizzato)
int insertFile(fileList** list, char* path);

//Metodo utilizzato per rimuovere un file da una lista, restituisce -1 in caso di errore
int deleteFile(fileList** list, char* path);

//Metodo utilizzato per restituire il path dell'ultimo file nella lista, restituisce NULL in caso di errore
char* getLastFile(fileList* list);

//Metodo utilizzato per rimuovere l'ultimo file della lista, restituisce -1 in caso di errore
int deleteLastFile(fileList** list);

//Metodo che stampa la lista di file
void printFileList(fileList* list);

//Metodo che elimina una lista di file
void destroyFileList(fileList** list);

#endif