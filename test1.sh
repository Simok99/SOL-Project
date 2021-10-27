#!/bin/bash
CONFIG_PATH="./config.txt"

echo "Script per il Test 1";

#Imposta il file di configurazione
echo -e "**Per numeri di default utilizzare 0, per path personalizzati rimuovere gli asterischi (Una opzione per riga, memoria in Bytes)
    **Parametri validi: numberOfFiles, memoryAllocated, numberOfThreads, socketPath, logPath
    numberOfFiles=10000
    memoryAllocated=128000000
    numberOfThreads=1
    **socketPath=./mysocketpath/socket
    **logPath=./mylogpath/log.txt" > $CONFIG_PATH

SERVER_CMD=./server
CLIENT_CMD=./client


valgrind --leak-check=full --show-leak-kinds=all $SERVER_CMD & #Avvia il server
pid=$!
sleep 3s

#Scrive tre file nel server con un ritardo di 200ms tra una richiesta e l'altra
$CLIENT_CMD -f socket -p -t 200 -W ./testfiles/prova.txt -W ./testfiles/wow.txt,./testfiles/testsubfolder/subfile.txt

#Legge un file con -r e uno con R 1, con 200ms tra una richiesta e l'altra, salvando i files letti in locale
$CLIENT_CMD -f socket -p -t 200 -r ./testfiles/prova.txt -R 1 -d SavedFiles

#Prova a leggere tutti i file con -R salvando i files letti in locale
$CLIENT_CMD -f socket -p -R 0 -d AllFiles

#Effettua lock e unlock di un file con un ritardo di 200ms tra le richieste
$CLIENT_CMD -f socket -p -t 200 -l ./testfiles/wow.txt -u ./testfiles/wow.txt

#Cancella un file dal server
$CLIENT_CMD -f socket -p -c ./testfiles/testsubfolder/subfile.txt

#Stampa le istruzioni d'uso del client
$CLIENT_CMD -h

sleep 3s

kill -s SIGHUP $pid #Chiude il server mandando il segnale SIGHUP
wait $pid
echo "Fine Test 1";