#include "util.h"
#include "api.h"
#include "queue.h"

#define DEFAULT_SOCKETPATH "./socket"

void printUsage(char * executableName){
    printf("Usare: %s -h -f <filename> -w <dirname> -W <filename1,filename2...> -D <dirname> -r <filename1,filename2...>\
    -R <numFiles> -d <dirname> -t <time> -l <filename1,filename2...> -u <filename1,filename2...> -c <filename1,filename2...>\
    -p\n", executableName);
}

bool fFlag = false, pFlag = false, wFlag=false, WFlag=false, DFlag=false, rFlag=false, RFlag=true, dFlag=false;  //Flag per i parametri
char* D_Dirname = NULL;    //Nome della cartella in memoria secondaria usata dal client (comando -D)
char* d_Dirname = NULL;    //Nome della cartella in memoria secondaria usata dal client (comando -d)
msg response;    //Risposta del server contenente data e size
queue* requestQueue;    //Coda delle richieste che il client effettua
unsigned int sleepTime = 0;     //Tempo che intercorre tra una richiesta e l'altra in millisecondi

/* Metodo utilizzato per i comandi -W, -r, -l, -u, -c: rimuove la virgola e divide la stringa in N stringhe
    restituite in un array, imposta inoltre il valore corretto del numero di file
*/
char** tokenArgs(char** args, unsigned int *n){
    unsigned int arraySize = 0, index=0;
    char** stringArray = NULL;
    char* token = strtok(*args, ",");
    while (token != NULL)
    {
        //Nuovo nome di file
        token = remSpaces(token);
        arraySize++;
        stringArray = realloc(stringArray, sizeof(char*)*arraySize);
        if (!stringArray)
        {
            fprintf(stderr, "Errore fatale realloc\n");
            exit(EXIT_FAILURE);
        }
        stringArray[index] = token;
        index++;
        token = strtok(NULL, ",");
    }
    *n=arraySize;
    return stringArray;
}

/* Metodo utilizzato per il comando -w: analizza la stringa passata come argomento e la restituisce ripulita da eventuali spazi
    imposta inoltre il valore di n 
 */
char* parseArgs(char* arg, unsigned int *n){
    char* cleanString;
    char* delimiter = ",";
    char* token = strtok(arg, delimiter);
    cleanString = strdup(token);
    cleanString = remSpaces(cleanString);
    
    //Parsing per il valore di n
    char* number = strtok(NULL, " \r\n\t");
    if (number)
    {
        //Trovato un valore per n
        if (isNumber(number, (long*)n) != 0 || *n <= 0)
        {
            //L'argomento passato al comando non è un numero valido
            fprintf(stderr, "Il parametro per -w deve essere un numero positivo o zero\n");
            exit(EXIT_FAILURE);
        }
        
        *n = atoi(number);
    }
    return cleanString;
}

/* Metodo utilizzato per visitare ricorsivamente una cartella ed inserire in una coda tutti i file trovati 
    (nel limite di maxfiles) */
void recursiveFileQueue(char* dirname, queue** fileQueue, int maxfiles){
    DIR* dir;
    struct dirent *entry;
    if (!(dir = opendir(dirname)))
    {
        return;
    }

    while (maxfiles != 0 && (entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_DIR)
        {
            //Trovata nuova cartella
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            char* newpath = malloc(sizeof(char) * 1024);
            snprintf(newpath, 1024, "%s/%s", dirname, entry->d_name);
            recursiveFileQueue(newpath, fileQueue, maxfiles);
        }
        else
        {
            //Trovato nuovo file, aggiungo alla coda il suo path (opCode e data inutilizzati)
            char* newpath = malloc(sizeof(char) * 1024);
            snprintf(newpath, 1024, "%s%s", dirname, entry->d_name);
            insertQueue(*fileQueue, newpath, -1, NULL);
            maxfiles--;
        }
    }
    closedir(dir);
}

//Metodo utilizzato per inviare la prima richiesta nella coda requestQueue al server
void sendRequest(){
    memset(&response, 0, sizeof(response));
    node* requestNode = popQueue(requestQueue);
    char command = requestNode->opCode;
    switch (command)
    {
    //TODO Implement commands using API
    case 'w':
        {
            unsigned int n = 0;
            char *directory = requestNode->id;
            n = atoi((char*)requestNode->data);
            DIR *d = opendir(directory);
            if (d == NULL)
            {
                fprintf(stderr, "Impossibile trovare la cartella %s\n", directory);
                closedir(d);
                break;
            }
            closedir(d);
            
            //Crea la coda di file da inviare
            queue* fileQueue = createQueue();

            //Inserisce, visitando ricorsivamente le cartelle, i file all'interno della coda
            recursiveFileQueue(directory, &fileQueue, n);
            
            //Invia tutte le richieste tramite l'API, che si occupera' di salvare i file eventualmente scartati
            for (int i = 0; i < queueLength(fileQueue); i++)
            {
                node* newFile = popQueue(fileQueue);
                char *filePath = newFile->id;
                if (openFile(filePath, O_CREATE_OR_O_LOCK) != 0)
                {
                    fprintf(stderr, "Impossibile scrivere il file %s, non e' stato aperto dal server\n", filePath);
                    free(newFile->id);
                    free(newFile->data);
                    free(newFile);
                    break;
                }
                if (!DFlag)
                {
                    //Se ci sono file scartati dal server non vanno salvati
                    if (writeFile(filePath, NULL) != 0)
                    {
                        fprintf(stderr, "Impossibile scrivere il file %s, errore in scrittura\n", filePath);
                        free(newFile->id);
                        free(newFile->data);
                        free(newFile);
                        break;
                    }
                    //File caricato correttamente
                    if (pFlag)
                        printf("File %s caricato correttamente e lock acquisita\n", filePath);
                }
                else
                {
                    //Se il server ha scartato file, vanno salvati in D_Dirname
                    if (writeFile(filePath, D_Dirname) != 0)
                    {
                        fprintf(stderr, "Impossibile inviare il file %s sul server, errore in scrittura\n", filePath);
                        free(newFile->id);
                        free(newFile->data);
                        free(newFile);
                        break;
                    }
                    //File caricato correttamente
                    if (pFlag)
                        printf("File %s caricato correttamente e lock acquisita\n", filePath);
                }
                free(newFile->id);
                free(newFile->data);
                free(newFile);
            }
            free(fileQueue);
            break;
        }
    case 'W':
        {
            char* path = (char*) requestNode->data;
            if (openFile(path, O_CREATE_OR_O_LOCK) != 0)
            {
                fprintf(stderr, "Impossibile scrivere il file %s, non e' stato aperto dal server\n", path);
                break;
            }

            if (!DFlag)
            {
                //Se ci sono file scartati dal server non vanno salvati
                if (writeFile(path, NULL) != 0)
                {
                    fprintf(stderr, "Impossibile scrivere il file %s, errore in scrittura\n", path);
                    break;
                }
                //File caricato correttamente
                if (pFlag)
                    printf("File %s caricato correttamente e lock acquisita\n", path);
            }
            else
            {
                //Se il server ha scartato file, vanno salvati in D_Dirname
                if (writeFile(path, D_Dirname) != 0)
                {
                    fprintf(stderr, "Impossibile inviare il file %s sul server, errore in scrittura\n", path);
                    break;
                }
                //File caricato correttamente
                if (pFlag)
                    printf("File %s caricato correttamente e lock acquisita\n", path);
            }
        }
        break;

    case 'r':
        {
            //Apre il file richiesto
            char* path = (char*) requestNode->data;
            if (openFile(path, NOFLAGS) != 0)
            {
                fprintf(stderr, "Impossibile aprire il file %s\n",path);
                break;
            }

            //File aperto, leggo il contenuto

            if (readFile(path, (void**)&response.data, &response.size) == 0)
            {
                if (pFlag) printf("Letti %ld bytes del file %s\n", response.size, path);
            }
            else{
                if (pFlag) printf("Errore in lettura del file %s\n", path);
            }
            
            //Controlla se salvare il file in locale (opzione congiunta -d)
            if (dFlag)
            {
                if (writeOnDisk(path, response.data, d_Dirname, response.size) == -1)
                {
                    if (pFlag) printf("File %s salvato in locale nella cartella %s\n", path, d_Dirname);
                }
                else{
                    if (pFlag) printf("Impossibile salvare il file %s nella cartella %s\n", path, d_Dirname);
                }
            }
            
            //Chiude il file
            if (closeFile(path) == 0)
            {
                if (pFlag) printf("File %s chiuso correttamente\n", path);
            }
            else{
                if (pFlag) printf("Errore in chiusura del file %s\n", path);
            }
            
            break;
        }
    case 'R':
        /* code */
        break;

    case 'l':
        {
            char *path = (char *)requestNode->data;
            if (lockFile(path) == 0)
            {
                if (pFlag)
                {
                    printf("Ottenuta la lock sul file %s\n", path);
                }
            }
            else{
                if (pFlag)
                {
                    printf("Errore nell'ottenimento della lock per il file %s\n", path);
                }
            }
        }
        break;

    case 'u':
        {
            char *path = (char *)requestNode->data;
            if (unlockFile(path) == 0)
            {
                if (pFlag)
                {
                    printf("Lock sul file %s rilasciata\n", path);
                }
            }
            else{
                if (pFlag)
                {
                    printf("Errore nel rilascio della lock per il file %s\n", path);
                }
            }
        }
        break;

    case 'c':
        /* code */
        break;

    default:
        break;
    }
    free(requestNode->data);
    free(requestNode->id);
    free(requestNode);
}

int main(int argc, char *argv[])
{

    if (argc == 1)
    {
        //Nessun argomento specificato
        printUsage(argv[0]);
        return 0;
    }

    int opt;
    char *socketName = DEFAULT_SOCKETPATH;
    requestQueue = createQueue();
    char* buffer = NULL;
    while ((opt=getopt(argc, argv, "hpf:w:W:D:r:R:d:t:l:u:c:")) != -1)
    {
        switch (opt)
        {
        case 'h':
            printUsage(argv[0]);
            return 0;

        case 'p':
            pFlag = true;
            break;

        case 'f':
            if (!fFlag)
            {
                socketName = optarg;
            }
            fFlag = true;
            break;

        case 'w':
            {
                unsigned int n = 0;
                char* dirname = parseArgs(optarg, &n);
                if (dirname == NULL)
                {
                    break;
                }
                //Inserisce in coda l'operazione con id = dirname e data = n
                buffer = realloc(buffer, sizeof(int));
                snprintf(buffer, sizeof(int), "%u", n);
                insertQueue(requestQueue, dirname, 'w', (void*) buffer);
                wFlag = true;
                break;
            }

        case 'W':
            {
                unsigned int numFiles;
                char* arg = optarg;
                char** array = tokenArgs(&arg, &numFiles);
                //Inserisce in coda più operazioni 'W' ciascuna con un nome di file
                for (int i = 0; i < numFiles; i++)
                {
                    insertQueue(requestQueue, NULL, 'W', (void*)array[i]);
                }
                WFlag=true;
                break;
            }

        case 'D':
            if (!wFlag && !WFlag)
            {
                fprintf(stderr, "Il comando -D va usato congiuntamente a -w o -W\n");
                return 0;
            }
            optarg = remSpaces(optarg);
            D_Dirname = optarg;
            DFlag=true;
            break;

        case 'r':
            {
                unsigned int numFiles;
                char *arg = optarg;
                char **array = tokenArgs(&arg, &numFiles);
                //Inserisce in coda più operazioni 'r' ciascuna con un nome di file
                for (int i = 0; i < numFiles; i++)
                {
                    insertQueue(requestQueue, NULL, 'r', (void *)array[i]);
                }
                rFlag=true;
                break;
            }

        case 'R':
            {
                unsigned int n = 0;
                if (isNumber(optarg, (long *)&n) != 0 || n <=0 )
                {
                    //L'argomento passato al comando non è un numero valido
                    fprintf(stderr, "Il parametro per -R deve essere un numero positivo o zero\n");
                    return 0;
                }
                //Inserisce in coda l'operazione in formato "n"
                buffer = realloc(buffer, sizeof(unsigned int));
                memcpy(buffer, (char*)&n, sizeof(unsigned int));
                insertQueue(requestQueue, NULL, 'R', (void *)buffer);
                break;
            }

        case 'd':
            if (!rFlag || !RFlag)
            {
                fprintf(stderr, "Il comando -d va usato congiuntamente a -r o -R\n");
                return 0;
            }
            optarg = remSpaces(optarg);
            d_Dirname = optarg;
            dFlag = true;
            break;
        
        case 't':
            {
                unsigned int time = 0;
                if (isNumber(optarg, (long *)&time) != 0)
                {
                    //L'argomento passato al comando non è un numero valido
                    fprintf(stderr, "Il parametro per -t deve essere un numero positivo o zero\n");
                    return 0;
                }
                sleepTime = time;
                break;
            }

        case 'l':
            {
                unsigned int numFiles;
                char *arg = optarg;
                char **array = tokenArgs(&arg, &numFiles);
                //Inserisce in coda più operazioni 'l' ciascuna con un nome di file
                for (int i = 0; i < numFiles; i++)
                {
                    insertQueue(requestQueue, NULL, 'l', (void *)array[i]);
                }
                break;
            }

        case 'u':
            {
                unsigned int numFiles;
                char *arg = optarg;
                char **array = tokenArgs(&arg, &numFiles);
                //Inserisce in coda più operazioni 'u' ciascuna con un nome di file
                for (int i = 0; i < numFiles; i++)
                {
                    insertQueue(requestQueue, NULL, 'u', (void *)array[i]);
                }
                break;
            }
        case 'c':
            {
                unsigned int numFiles;
                char *arg = optarg;
                char **array = tokenArgs(&arg, &numFiles);
                //Inserisce in coda più operazioni 'c' ciascuna con un nome di file
                for (int i = 0; i < numFiles; i++)
                {
                    insertQueue(requestQueue, NULL, 'c', (void *)array[i]);
                }
                break;
            }
        default:
            fprintf(stderr, "Comando non riconosciuto: %c\n",opt);
            break;
        }
    }

    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    time.tv_sec += 5;

    if (openConnection(socketName, 1000, time) == 0)
    {
        if (pFlag) printf("Client connesso al server!\n");
    }
    else{
        //Errore di connessione con il server
        if (pFlag) printf("Impossibile connettersi al server!\n");
        return -1;
    }
    
    //Invia le richieste specificate al server ed eventualmente aspetta x secondi tra una e l'altra
    while (requestQueue->head != NULL)
    {
        sendRequest();
        usleep(sleepTime*1000);
    }
    
    if (closeConnection(socketName) == 0)
    {
        if (pFlag) printf("Client disconnesso dal server!\n");
    }
    else{
        //Errore in disconnessione
        if (pFlag) printf("Impossibile disconnettersi dal server!\n");
    }

    //TODO Finire cleanup
    free(requestQueue);
    return 0;
}
