/**
 * @file icl_hash.c
 *
 * Dependency free hash table implementation.
 *
 * This simple hash table implementation should be easy to drop into
 * any other peice of code, it does not depend on anything else :-)
 * 
 * @author Jakub Kurzak
 */
/* $Id: icl_hash.c 2838 2011-11-22 04:25:02Z mfaverge $ */
/* $UTK_Copyright: $ */

#include "util.h"
#include "hash.h"

#define BITS_IN_int     ( sizeof(int) * CHAR_BIT )
#define THREE_QUARTERS  ((int) ((BITS_IN_int * 3) / 4))
#define ONE_EIGHTH      ((int) (BITS_IN_int / 8))
#define HIGH_BITS       ( ~((unsigned int)(~0) >> ONE_EIGHTH ))
/**
 * A simple string hash.
 *
 * An adaptation of Peter Weinberger's (PJW) generic hashing
 * algorithm based on Allen Holub's version. Accepts a pointer
 * to a datum to be hashed and returns an unsigned integer.
 * From: Keith Seymour's proxy library code
 *
 * @param[in] key -- the string to be hashed
 *
 * @returns the hash index
 */
unsigned int
hash_pjw(void* key)
{
    char *datum = (char *)key;
    unsigned int hash_value, i;

    if(!datum) return 0;

    for (hash_value = 0; *datum; ++datum) {
        hash_value = (hash_value << ONE_EIGHTH) + *datum;
        if ((i = hash_value & HIGH_BITS) != 0)
            hash_value = (hash_value ^ (i >> THREE_QUARTERS)) & ~HIGH_BITS;
    }
    return (hash_value);
}

int string_compare(void* a, void* b) 
{
    return (strcmp( (char*)a, (char*)b ) == 0);
}


/**
 * Create a new hash table.
 *
 * @param[in] nbuckets -- number of buckets to create
 * @param[in] hash_function -- pointer to the hashing function to be used
 * @param[in] hash_key_compare -- pointer to the hash key comparison function to be used
 *
 * @returns pointer to new hash table.
 */

icl_hash_t *
icl_hash_create( int nbuckets, unsigned int (*hash_function)(void*), int (*hash_key_compare)(void*, void*), long maxMemory )
{
    icl_hash_t *ht;
    int i;

    ht = (icl_hash_t*) malloc(sizeof(icl_hash_t));
    if(!ht) return NULL;

    ht->buckets = (icl_entry_t**)malloc(nbuckets * sizeof(icl_entry_t*));
    if(!ht->buckets) return NULL;

    ht->locks = (pthread_mutex_t*)malloc(nbuckets * sizeof(pthread_mutex_t));
    if(!ht->buckets) return NULL;

    ht->nbuckets = nbuckets;
    ht->nlocks = nbuckets;
    for(i=0;i<ht->nbuckets;i++){
        ht->buckets[i] = NULL;
    }
    for (i = 0; i < ht->nlocks; i++)
    {
        pthread_mutex_t newLock = PTHREAD_MUTEX_INITIALIZER;
        ht->locks[i] = newLock;
        pthread_mutex_init(&(ht->locks[i]), NULL);
    }

    pthread_mutex_t newLock = PTHREAD_MUTEX_INITIALIZER;
    ht->tableLock = newLock;
    pthread_mutex_init(&(ht->tableLock), NULL);

    ht->hash_function = hash_function ? hash_function : hash_pjw;
    ht->hash_key_compare = hash_key_compare ? hash_key_compare : string_compare;

    ht->nentries = 0;
    ht->currentMemory = 0;
    ht->maxMemory = maxMemory;

    return ht;
}

/**
 * Search for an entry in a hash table.
 *
 * @param ht -- the hash table to be searched
 * @param key -- the key of the item to search for
 *
 * @returns pointer to the data corresponding to the key.
 *   If the key was not found, returns NULL.
 */

void *
icl_hash_find(icl_hash_t *ht, void* key)
{
    icl_entry_t* curr;
    unsigned int hash_val;

    if(!ht || !key) return NULL;

    hash_val = (* ht->hash_function)(key) % ht->nbuckets;
    
    /*INIZIO SEZIONE CRITICA BUCKET*/

    pthread_mutex_lock(&(ht->locks[hash_val]));

    for (curr=ht->buckets[hash_val]; curr != NULL; curr=curr->next){
        if ( ht->hash_key_compare(curr->key, key))
        {
            pthread_mutex_unlock(&(ht->locks[hash_val]));

            /*ELEMENTO TROVATO FINE SEZIONE CRITICA BUCKET*/
            return(curr->data);
        }
    }
    pthread_mutex_unlock(&(ht->locks[hash_val]));

    /*ELEMENTO NON TROVATO FINE SEZIONE CRITICA BUCKET*/
    return NULL;
}

/**
 * Insert an item into the hash table.
 *
 * @param ht -- the hash table
 * @param key -- the key of the new item
 * @param data -- pointer to the new item's data
 *
 * @returns pointer to the new item.  Returns NULL on error.
 */

icl_entry_t *
icl_hash_insert(icl_hash_t *ht, void* key, void *data, long fileSize)
{
    icl_entry_t *curr;
    unsigned int hash_val;

    if(!ht || !key) return NULL;

    hash_val = (* ht->hash_function)(key) % ht->nbuckets;

    for (curr=ht->buckets[hash_val]; curr != NULL; curr=curr->next)
        if ( ht->hash_key_compare(curr->key, key))
            return(NULL); /* key already exists */

    /* if key was not found */

    //Controllo ci sia spazio per il nuovo file
    if (ht->currentMemory + fileSize > ht->maxMemory)
    {
        //Spazio non sufficiente
        return NULL;
    }
    curr = (icl_entry_t*)malloc(sizeof(icl_entry_t));
    if(!curr) return NULL;

    curr->key = key;
    curr->data = data;

    /*INIZIO SEZIONE CRITICA BUCKET*/

    pthread_mutex_lock(&(ht->locks[hash_val]));

    curr->next = ht->buckets[hash_val]; /* add at start */
    ht->buckets[hash_val] = curr;

    pthread_mutex_unlock(&(ht->locks[hash_val]));

    /*FINE SEZIONE CRITICA BUCKET*/

    LOCK(&(ht->tableLock));
    ht->currentMemory += (long)fileSize;
    ht->nentries++;
    UNLOCK(&(ht->tableLock));
    return curr;
}

/**
 * Replace entry in hash table with the given entry.
 *
 * @param ht -- the hash table
 * @param key -- the key of the new item
 * @param data -- pointer to the new item's data
 * @param olddata -- pointer to the old item's data (set upon return)
 *
 * @returns pointer to the new item.  Returns NULL on error.
 */

icl_entry_t *
icl_hash_update_insert(icl_hash_t *ht, void* key, void *data, void **olddata, long fileSize)
{
    icl_entry_t *curr, *prev;
    unsigned int hash_val;

    if(!ht || !key) return NULL;

    hash_val = (* ht->hash_function)(key) % ht->nbuckets;

    /*INIZIO SEZIONE CRITICA BUCKET*/

    pthread_mutex_lock(&(ht->locks[hash_val]));

    /* Scan bucket[hash_val] for key */
    for (prev=NULL,curr=ht->buckets[hash_val]; curr != NULL; prev=curr, curr=curr->next){
        /* If key found, remove node from list, free old key, and setup olddata for the return */
        //printf("HASHKEY:%s\nKEY:%s\n",(char*)(curr->key), (char*)(key));
        if (ht->hash_key_compare(curr->key, key))
        {
            if (olddata != NULL) {
                *olddata = curr->data;
                free(curr->key);
                LOCK(&(ht->tableLock));
                ht->currentMemory -= (long)strlen((char*)curr->data);
                ht->nentries--;
                UNLOCK(&(ht->tableLock));
            }
            else {
                 free(curr->key);
            } 
            if (prev == NULL)
                ht->buckets[hash_val] = curr->next;
            else
                prev->next = curr->next;
        }
        free(curr);
        break;
    }
    /* Since key was either not found, or found-and-removed, create and prepend new node */

    //Controllo ci sia spazio per il nuovo file
    if (ht->currentMemory + fileSize > ht->maxMemory)
    {
        //Spazio non sufficiente per il nuovo contenuto
        fprintf(stderr, "Impossibile aggiornare il file %s, sarebbe troppo grande per il server\n", (char*)key);
        return NULL;
    }
    curr = (icl_entry_t*)malloc(sizeof(icl_entry_t));
    if(curr == NULL) return NULL; /* out of memory */

    curr->key = key;
    curr->data = data;
    curr->next = ht->buckets[hash_val]; /* add at start */

    ht->buckets[hash_val] = curr;

    pthread_mutex_unlock(&(ht->locks[hash_val]));

    /*FINE SEZIONE CRITICA BUCKET*/

    LOCK(&(ht->tableLock));
    ht->currentMemory += (long)fileSize;
    ht->nentries++;
    UNLOCK(&(ht->tableLock));

    if(olddata!=NULL && *olddata!=NULL)
        *olddata = NULL;

    return curr;
}

/**
 * Free one hash table entry located by key (key and data are freed using functions).
 *
 * @param ht -- the hash table to be freed
 * @param key -- the key of the new item
 * @param free_key -- pointer to function that frees the key
 * @param free_data -- pointer to function that frees the data
 *
 * @returns 0 on success, -1 on failure.
 */
int icl_hash_delete(icl_hash_t *ht, void* key, void (*free_key)(void*), void (*free_data)(void*))
{
    icl_entry_t *curr, *prev;
    unsigned int hash_val;

    if(!ht || !key) return -1;
    hash_val = (* ht->hash_function)(key) % ht->nbuckets;

    prev = NULL;

    /*INIZIO SEZIONE CRITICA BUCKET*/

    pthread_mutex_lock(&(ht->locks[hash_val]));

    for (curr=ht->buckets[hash_val]; curr != NULL; )  {
        if ( ht->hash_key_compare(curr->key, key)) {
            if (prev == NULL) {
                ht->buckets[hash_val] = curr->next;
            } else {
                prev->next = curr->next;
            }
            if (*free_key && curr->key) (*free_key)(curr->key);
            if (*free_data && curr->data)
            {
                LOCK(&(ht->tableLock));
                size_t oldSize = strlen((char *)curr->data);
                ht->currentMemory -= (long)oldSize;
                ht->nentries--;
                UNLOCK(&(ht->tableLock));
                (*free_data)(curr->data);
                free(curr);
            }

            pthread_mutex_unlock(&(ht->locks[hash_val]));

            /*ELEMENTO RIMOSSO FINE SEZIONE CRITICA BUCKET*/
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }

    pthread_mutex_unlock(&(ht->locks[hash_val]));

    /*ELEMENTO NON RIMOSSO FINE SEZIONE CRITICA BUCKET*/
    return -1;
}

/**
 * Free hash table structures (key and data are freed using functions).
 *
 * @param ht -- the hash table to be freed
 * @param free_key -- pointer to function that frees the key
 * @param free_data -- pointer to function that frees the data
 *
 * @returns 0 on success, -1 on failure.
 */
int
icl_hash_destroy(icl_hash_t *ht, void (*free_key)(void*), void (*free_data)(void*))
{
    icl_entry_t *bucket, *curr, *next;
    int i;

    if(!ht) return -1;

    for (i=0; i<ht->nbuckets; i++) {
        bucket = ht->buckets[i];
        for (curr=bucket; curr!=NULL; ) {
            next=curr->next;
            if (*free_key && curr->key) (*free_key)(curr->key);
            if (*free_data && curr->data) (*free_data)(curr->data);
            free(curr);
            curr=next;
        }
    }

    for (i = 0; i < ht->nlocks; i++)
    {
        pthread_mutex_destroy(&(ht->locks[i]));
    }
    free(ht->locks);

    pthread_mutex_destroy(&(ht->tableLock));

    if(ht->buckets) free(ht->buckets);
    if(ht) free(ht);

    return 0;
}

/**
 * Dump the hash table's contents to the given file pointer.
 *
 * @param stream -- the file to which the hash table should be dumped
 * @param ht -- the hash table to be dumped
 *
 * @returns 0 on success, -1 on failure.
 */

int
icl_hash_dump(FILE* stream, icl_hash_t* ht)
{
    icl_entry_t *bucket, *curr;
    int i;

    if(!ht) return -1;

    fprintf(stream, "\n\nFile ancora presenti nel server:\n\n");
    for(i=0; i<ht->nbuckets; i++) {
        bucket = ht->buckets[i];
        for(curr=bucket; curr!=NULL; ) {
            if(curr->key)
                fprintf(stream, "Nome file: %s | Dimensione bytes: %ld\n", (char *)curr->key, strnlen((char*)curr->data, MAX_FILE_SIZE));
            curr=curr->next;
        }
    }

    fprintf(stream, "\n\n");

    return 0;
}


