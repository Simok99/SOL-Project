#if !defined(_QUEUE_H)
#define _QUEUE_H

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

//Struttura che rappresenta un nodo
typedef struct node
{
    char* id;   //Utilizzato lato server per realizzare code di file espulsi e per lock/unlock
    void* data;
    unsigned int opCode;
    struct node* next;
} node;

//Struttura che rappresenta una coda
typedef struct queue
{
    struct node* head;
    struct node* tail;
	int size;	
} queue;

//Metodo utilizzato per creare una coda
queue* createQueue();

//Restituisce la lunghezza della coda
int queueLength(queue* queuep);

//Inserisce un dato e opCode associato alla fine di una coda (id usato come informazione ausiliaria)
void insertQueue(queue *queue, char *id, unsigned int opCode, void *data);

//Restituisce la testa della coda (la testa non viene rimossa)
node* popQueue(queue* queue);

//Restituisce la prima occorrenza di un nodo con id = id passato come parametro
node* searchInQueue(queue* queuep, char* id);

//Rimuove la testa della coda
void deQueue(queue* queue);

//Rimuove la prima occorrenza di un nodo con id = id passato come parametro
void removeFromQueue(queue* queuep, char* id);

//Cancella una coda in memoria
void deleteQueue(queue* queue);

//Stampa gli id dei nodi di una coda
void printQueue(queue *queuep);

#endif