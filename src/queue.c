#include "queue.h"

//Implementazione dei metodi dichiarati in queue.h

queue* createQueue(){
    queue* q = malloc(sizeof(queue));
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
    return q;
}

int queueLength(queue* queuep){
    return queuep->size;
}

void insertQueue(queue* queue, char* id, unsigned int opCode, void* data){
    node* newNode = malloc(sizeof(struct node));
    if (!newNode)
    {
        fprintf(stderr, "Errore fatale malloc\n");
        return;
    }
    
    newNode->opCode = opCode;
    newNode->id = id;
    newNode->data = data;
    queue->size += 1;
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

    if (queue->head == NULL)
    {
        queue->tail = NULL;
    }
    queue->size-=1;
    return oldHead;
}

node* searchInQueue(queue* queuep, char* id){
    // Se coda vuota
    if (queuep == NULL)
    {
        return NULL;
    }
    queue* tmp = queuep;
    while (tmp->head != tmp->tail)
    {
        if (strcmp(tmp->head->id, id) == 0)
        {
            return tmp->head;
        }
        tmp->head = tmp->head->next;
    }
    return NULL;
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

void removeFromQueue(queue* queuep, char* id){
    node* prev = NULL;
    node* curr = queuep->head;

    if (curr != NULL && strcmp(curr->id, id) == 0)
    {
        queuep->head = curr->next;
        free(curr);
        return;
    }

    while (curr != NULL && strcmp(curr->id, id) == 0)
    {
        prev = curr;
        curr = curr->next;
    }

    if (curr == NULL)
    {
        //Elemento non trovato
        return;
    }

    prev->next = curr->next;
    free(curr);
}

void deleteQueue(queue* queue){
    while (queue->head != queue->tail)
    {
        node* toFree = queue->head->data;
        free(queue->head->id);
        queue->head = queue->head->next;
        free(toFree);
    }

    //Ultimo elemento rimasto
    if(queue->head) free(queue->head);

    free(queue);
    
}

void printQueue(queue* queuep){
    queue* tmp = queuep;
    while (tmp->head != tmp->tail)
    {
        node *newNode = tmp->head;
        tmp->head = tmp->head->next;
        printf("NODE:%s\n",newNode->id);
    }

    // Ultimo elemento rimasto
    if (tmp->head)
        printf("NODE:%s\n",tmp->head->id);

}