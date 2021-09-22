#if !defined(_QUEUE_H)
#define _QUEUE_H

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

//Struttura che rappresenta un nodo
typedef struct node
{
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

//Inserisce un dato e opCode associato alla fine di una coda
void insertQueue(queue* queue, unsigned int opCode, void* data);

<<<<<<< HEAD
//Restituisce la testa della coda (e successivamente viene rimossa dalla coda)
=======
//Restituisce la testa della coda (la testa non viene rimossa)
>>>>>>> 3cbc2e64c27f0024cfa9f45579bab529e527cfe1
node* popQueue(queue* queue);

//Rimuove la testa della coda
void deQueue(queue* queue);

//Cancella una coda in memoria
void deleteQueue(queue* queue);

#endif