#include "fileList.h"

/* IMPLEMENTAZIONE METODI FILELIST.H */

int insertFile(fileList **list, char *path, long descriptor){
    fileList* new = malloc(sizeof(fileList));
    if (new == NULL)
    {
        fprintf(stderr, "Errore fatale malloc\n");
        return -1;
    }
    strncpy(new->path, path, MAX_PATH_LENGTH);
    new->fd = descriptor;
    new->size = 0;
    new->next = *list;
    *list = new;
    return -1;
}

int containsFile(fileList *list, char *path, long descriptor){
    fileList *head = list;
    while (head != NULL)
    {
        if (head->path != NULL)
            if ((strncmp((char *)head->path, path, MAX_PATH_LENGTH) == 0) && head->fd == descriptor)
                //File trovato
                return 0;
        head = head->next;
    }
    return -1;
}

int deleteFile(fileList **list, char *path, long descriptor){
    fileList *head = *list;
    fileList *prev = NULL;
    if (*list == NULL || list == NULL)
        return 0;
    while (head != NULL)
    {
        if ((strncmp(head->path, path, MAX_PATH_LENGTH) == 0) && head->fd == descriptor)
        {
            if (prev == NULL)
            {
                *list = (*list)->next;
                free(head);
                return 0;
            }
            else
            {
                prev->next = head->next;
                free(head);
                return 0;
            }
        }
        prev = head;
        head = head->next;
    }
    return -1;
}

char *getLastFile(fileList *list){
    fileList *curr = list;
    fileList *next = curr->next;
    while (next != NULL)
    {
        curr = next;
        next = curr->next;
    }
    if (curr->path)
        return curr->path;
    else
        return NULL;
}

int deleteLastFile(fileList **list){
    fileList *curr = *list;
    fileList *prev = NULL;
    if (*list == NULL)
        return -1;
    while (curr->next != NULL)
    {
        prev = curr;
        curr = curr->next;
    }
    if (prev == NULL)
    {
        free(curr);
        *list = NULL;
        return 0;
    }
    free(prev->next);
    prev->next = NULL;
    return 0;
}

void printFileList(fileList *list){
    while (list != NULL)
    {
        printf("PATHNAME: %s\n", (char *)list->path);
        list = list->next;
    }
}

void destroyFileList(fileList **list){
    fileList *curr = *list;
    fileList *next = NULL;
    while (curr != NULL)
    {
        next = curr->next;
        free(curr);
        curr = next;
    }
    *list = NULL;
}