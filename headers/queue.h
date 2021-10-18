#if !defined(_QUEUE_H)
#define _QUEUE_H

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

//Struttura che rappresenta un nodo
typedef struct node
{
    char* id;   //Utilizzato lato server per realizzare code di file espulsi
    void* data;
    unsigned int opCode;
    struct node* next;
} node;

//Struttura che rappresenta una coda
typedef struct queue
{
    struct node* head;
    struct node* tail;
} queue;

//Metodo utilizzato per creare una coda
queue* createQueue();

//Restituisce la lunghezza della coda
int queueLength(queue* queue);

//Inserisce un dato e opCode associato alla fine di una coda (id usato come informazione ausiliaria)
void insertQueue(queue *queue, char *id, unsigned int opCode, void *data);

//Restituisce la testa della coda (la testa non viene rimossa)
node* popQueue(queue* queue);

//Rimuove la testa della coda
void deQueue(queue* queue);

//Cancella una coda in memoria
void deleteQueue(queue* queue);

#endif