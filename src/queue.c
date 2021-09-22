#include "queue.h"

//Implementazione dei metodi dichiarati in queue.h

queue* createQueue(){
    queue* q = malloc(sizeof(queue));
    q->head = NULL;
    q->tail = NULL;
    return q;
}

void insertQueue(queue* queue, unsigned int opCode, void* data){
    node* newNode = malloc(sizeof(node));
    if (!newNode)
    {
        fprintf(stderr, "Errore fatale malloc\n");
        return;
    }
    
    newNode->opCode = opCode;
    newNode->data = data;
    newNode->next = NULL;

    //Se coda vuota
    if (queue->head == NULL)
    {
        queue->head = newNode;
        queue->tail = newNode;
    }
    else
    {
        //Sposta la vecchia coda e aggiorna l'ultimo nodo
        node* oldTail = queue->tail;
        oldTail->next = newNode;
        queue->tail = newNode;
    }
}

node* popQueue(queue* queue){
    //Se coda vuota
    if (queue == NULL)
    {
        return NULL;
    }
    node* oldHead = queue->head;
    queue->head = queue->head->next;
    return oldHead;
}

void deQueue(queue* queue){
    //Se coda vuota
    if (queue == NULL)
    {
        fprintf(stderr, "Errore rimozione testa: coda vuota");
        return;
    }
    node* oldHead = queue->head;
    queue->head = oldHead->next;
    free(oldHead);
}

void deleteQueue(queue* queue){
    //Se coda vuota
    if (queue == NULL)
    {
        return;
    }
    
    while (queue->head != queue->tail)
    {
        node* toFree = queue->head;
        queue->head = queue->head->next;
        free(toFree);
    }

    //Ultimo elemento rimasto
    free(queue->head);

    free(queue);
    
}