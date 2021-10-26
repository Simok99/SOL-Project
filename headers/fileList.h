#if !defined(_FILELIST_H)
#define _FILELIST_H

#include "util.h"

//Struttura che rappresenta una lista di file, path è chiave univoca per un file come da testo (path assoluto)
typedef struct file
{
    long fd;    //File descriptor del client che ha effettuato la lock sul file
    bool isLocked;  //Funge da lock per il file
    char path[MAX_PATH_LENGTH];
    unsigned int size;
    unsigned int status;
    struct file* next;
} fileList;

//Metodo utilizzato per inserire un file in una lista, restituisce -1 in caso di errore
int insertFile(fileList** list, char* path, long descriptor);

//Metodo utilizzato per cercare un file in una lista, restituisce 0 se presente, -1 se non presente
int containsFile(fileList* list, char* path, long descriptor);

//Metodo utilizzato per rimuovere un file da una lista, restituisce -1 in caso di errore
int deleteFile(fileList** list, char* path, long descriptor);

//Metodo utilizzato per restituire la lunghezza della lista
int lengthList(fileList* list);

//Metodo utilizzato per restituire il path dell'ultimo file nella lista, restituisce NULL in caso di errore
char* getLastFile(fileList* list);

//Metodo utilizzato per restituire la lista dei file in stato locked di uno specifico client, imposta n alla dimensione dell'array
char** getLockedFiles(fileList* list, long fdSocket, int* length);

//Metodo utilizzato per restituire la lista dei file in stato unlocked, imposta n alla dimensione dell'array
char **getUnlockedFiles(fileList *list, int *length);

//Metodo utilizzato per rimuovere l'ultimo file della lista, restituisce -1 in caso di errore
int deleteLastFile(fileList** list);

//Metodo che stampa la lista di file
void printFileList(fileList* list);

//Metodo che elimina una lista di file
void destroyFileList(fileList** list);

//Restituisce true se il file specificato risulta locked, false altrimenti
bool isLocked(fileList *list, char *path);

//Effettua la lock del file se presente
void listLockFile(fileList **list, char *path, long fdSocket);

//Effettua la unlock del file se presente e se il descriptor risulta corretto
void listUnlockFile(fileList **list, char *path, long fdSocket);

#endif