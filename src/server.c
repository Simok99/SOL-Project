#define _POSIX_C_SOURCE 2001112L
#define DEFAULT_CONFIG_PATHNAME "./config.txt"
#define INFO "[INFO] "
#define WARNING "\e[0;33m[WARNING] " //Warning giallo
#define ERROR "\e[1;31m[ERROR] "    //Errore rosso

#include "util.h"


static config* conf;
static char* logPath;
static FILE* logFile;

//Scrive un messaggio sul file di log
void wLog(char* message);

//Stampa informazioni di fine esecuzione
void printFinalInfos();

//Funzione di cleanup
void cleanUp();

int main(int argc, char const *argv[])
{
    //Imposta i parametri dal file di configurazione
    if ((conf = readConfig(DEFAULT_CONFIG_PATHNAME)) == NULL)
    {
        fprintf(stderr, ERROR "Errore in lettura del file di configurazione\n");
        exit(EXIT_FAILURE);
    }

    //Imposta il file di log
    if ((logPath = malloc(sizeof(char) * 256)) == NULL)
    {
        fprintf(stderr, ERROR "Errore fatale malloc\n");
        exit(EXIT_FAILURE);
    }
    if (strcmp(conf->logPath, "\0") || strcpy(logPath,conf->logPath) == NULL)
    {
        fprintf(stderr, WARNING "File di log non impostato, utilizzo log.txt\n");
        strcpy(logPath, "./log.txt");
    }
    else{
        fprintf(stdout, INFO "File di log impostato: %s\n" ,conf->logPath);
    }
    if ((logFile=fopen(logPath, "w")) == NULL)
    {
        fprintf(stderr, ERROR "Errore in apertura del file di log\n");
        exit(EXIT_FAILURE);
    }
    
    //Server pronto per l'esecuzione
    bool running = true;

    wLog(INFO "Server in avvio");

    while (running)
    {
        //Server Operativo
    }    

    printFinalInfos();

    cleanUp();

    return 0;
}


void printFinalInfos(){
    //Scrive sulla console
    fprintf(stdout, "\n\n-----Statistiche finali Server-----\n\n"
    "\nNumero di file massimi memorizzati: "
    "\nNumero massima raggiunta (in MB): "
    "\nNumero di esecuzioni dell'algoritmo di rimpiazzamento: "
    "\nFile presenti nello storage alla chiusura: ");

    //Scrive sul file di log
    fprintf(logFile, "\n\n-----Statistiche finali Server-----\n\n"
    "\nNumero di file massimi memorizzati: "
    "\nNumero massima raggiunta (in MB): "
    "\nNumero di esecuzioni dell'algoritmo di rimpiazzamento: "
    "\nFile presenti nello storage alla chiusura: ");
}

void cleanUp(){
    //TODO implement cleanup
}

void wLog(char* msg){
    fputs(msg, logFile);
}