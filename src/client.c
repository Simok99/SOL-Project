#include "util.h"
#include "api.h"
#include "queue.h"

void printUsage(char * executableName){
    printf("Usare: %s -h -f <filename> -w <dirname> -W <filename1,filename2...> -D <dirname> -r <filename1,filename2...>\
    -R <numFiles> -d <dirname> -t <time> -l <filename1,filename2...> -u <filename1,filename2...> -c <filename1,filename2...>\
    -p\n", executableName);
}

bool fFlag = false, pFlag = false, wFlag=false, WFlag=false, DFlag=false, rFlag=false, RFlag=true, dFlag=false;  //Flag per i parametri
char* D_Dirname = NULL;    //Nome della cartella in memoria secondaria usata dal client (comando -D)
char* d_Dirname = NULL;    //Nome della cartella in memoria secondaria usata dal client (comando -d)
msg request;    //Richiesta di un client contenente data e size
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
        remSpaces(&token);
        arraySize++;
        stringArray = realloc(stringArray, sizeof(char*)*arraySize);
        if (!stringArray)
        {
            fprintf(stderr, "Errore fatale realloc");
            exit(EXIT_FAILURE);
        }
        stringArray[index] = token;
        index++;
    }
    *n=arraySize;
    return stringArray;
}

/* Metodo utilizzato per il comando -w: analizza la stringa passata come argomento e la restituisce ripulita da eventuali spazi
    imposta inoltre il valore di n 
 */
char* parseArgs(char* arg, unsigned int *n){
    char* cleanString = NULL;
    char* token = strtok(arg, ",");
    remSpaces(&token);
    cleanString = token;
    
    //Parsing per il valore di n
    token = strtok(NULL, ",");
    if (token)
    {
        //Trovato un valore per n
        if (isNumber(token, (long*)n) == 0 || *n <= 0)
        {
            //L'argomento passato al comando non è un numero valido
            fprintf(stderr, "Il parametro per -w deve essere un numero positivo o zero");
            exit(EXIT_FAILURE);
        }
        
        *n = atoi(token);
    }
    return cleanString;
}

//Metodo utilizzato per inviare la prima richiesta nella coda requestQueue al server
void sendRequest(){
    node* requestNode = popQueue(requestQueue);
    char command = requestNode->opCode;
    switch (command)
    {
    //TODO Implement commands using API
    case 'w':
        /* code */
        break;

    case 'W':
        /* code */
        break;

    case 'r':
        /* code */
        break;

    case 'R':
        /* code */
        break;

    case 'l':
        /* code */
        break;

    case 'u':
        /* code */
        break;

    case 'c':
        /* code */
        break;

    default:
        break;
    }
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
    char *socketName = NULL;
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
            unsigned int n = 0;
            char* dirname = NULL;
            dirname = parseArgs(optarg, &n);
            if (dirname == NULL)
            {
                break;
            }
            //Inserisce in coda l'operazione in formato "dirname n"
            char* tmp = strcat(" ", n);
            char* buffer = strcat(dirname, tmp);
            insertQueue(requestQueue, 'w', (void*) buffer);
            wFlag = true;
            break;

        case 'W':
            unsigned int numFiles;
            char** array = tokenArgs(optarg, &numFiles);
            //Inserisce in coda più operazioni 'W' ciascuna con un nome di file
            for (int i = 0; i < numFiles; i++)
            {
                insertQueue(requestQueue, 'W', (void*)array[i]);
            }
            WFlag=true;
            break;

        case 'D':
            if (!wFlag || !WFlag)
            {
                fprintf(stderr, "Il comando -D va usato congiuntamente a -w o -W\n");
                return 0;
            }
            remSpaces(&optarg);
            D_Dirname = optarg;
            DFlag=true;
            break;

        case 'r':
            unsigned int numFiles;
            char **array = tokenArgs(optarg, &numFiles);
            //Inserisce in coda più operazioni 'r' ciascuna con un nome di file
            for (int i = 0; i < numFiles; i++)
            {
                insertQueue(requestQueue, 'r', (void *)array[i]);
            }
            rFlag=true;
            break;

        case 'R':
            unsigned int n = 0;
            if (isNumber(optarg, (long *)&n) != 0 || n <=0 )
            {
                //L'argomento passato al comando non è un numero valido
                fprintf(stderr, "Il parametro per -R deve essere un numero positivo o zero");
                return 0;
            }
            insertQueue(requestQueue, 'R', (void*)n);
            break;

        case 'd':
            if (!rFlag || !RFlag)
            {
                fprintf(stderr, "Il comando -d va usato congiuntamente a -r o -R\n");
                return 0;
            }
            remSpaces(&optarg);
            d_Dirname = optarg;
            dFlag = true;
            break;
        
        case 't':
            unsigned int time = 0;
            if (isNumber(optarg, (long *)&time) != 0 || n <= 0)
            {
                //L'argomento passato al comando non è un numero valido
                fprintf(stderr, "Il parametro per -t deve essere un numero positivo o zero");
                return 0;
            }
            sleepTime = time;
            break;

        case 'l':
            unsigned int numFiles;
            char **array = tokenArgs(optarg, &numFiles);
            //Inserisce in coda più operazioni 'l' ciascuna con un nome di file
            for (int i = 0; i < numFiles; i++)
            {
                insertQueue(requestQueue, 'l', (void *)array[i]);
            }
            break;

        case 'u':
            unsigned int numFiles;
            char **array = tokenArgs(optarg, &numFiles);
            //Inserisce in coda più operazioni 'u' ciascuna con un nome di file
            for (int i = 0; i < numFiles; i++)
            {
                insertQueue(requestQueue, 'u', (void *)array[i]);
            }
            break;

        case 'c':
            unsigned int numFiles;
            char **array = tokenArgs(optarg, &numFiles);
            //Inserisce in coda più operazioni 'c' ciascuna con un nome di file
            for (int i = 0; i < numFiles; i++)
            {
                insertQueue(requestQueue, 'c', (void *)array[i]);
            }
            break;

        default:
            fprintf(stderr, "Comando non riconosciuto: %c",opt);
            break;
        }
    }

    //TODO Setup connessione tramite API

    if (pFlag) printf("Client connesso al server!");
    
    //Invia le richieste specificate al server ed eventualmente aspetta x secondi tra una e l'altra

    while (requestQueue->head != NULL)
    {
        sendRequest();
        usleep(sleepTime*1000);
    }
    
    //TODO Chiusura connessione tramite API

    if (pFlag) printf("Client disconnesso!");

    //TODO Finire cleanup
    deleteQueue(requestQueue);
    return 0;
}
