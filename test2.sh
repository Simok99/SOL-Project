#!/bin/bash
CONFIG_PATH="./config.txt"

echo "Script per il Test 2";

#Imposta il file di configurazione
echo -e "**Per numeri di default utilizzare 0, per path personalizzati rimuovere gli asterischi (Una opzione per riga, memoria in Bytes)
    **Parametri validi: numberOfFiles, memoryAllocated, numberOfThreads, socketPath, logPath
    numberOfFiles=10
    memoryAllocated=1024000
    numberOfThreads=4
    **socketPath=./mysocketpath/socket
    **logPath=./mylogpath/log.txt" > $CONFIG_PATH

SERVER_CMD=./server
CLIENT_CMD=./client


valgrind --leak-check=full --show-leak-kinds=all $SERVER_CMD & #Avvia il server in background con valgrind e i flag settati
pid=$!
sleep 4s

#Prova a scrivere tre files in testfiles, se vengono scartati file vengono salvati in backup
$CLIENT_CMD -p -f socket -W ./testfiles/prova.txt,./testfiles/wow.txt,./testfiles/file14.txt,./testfiles/file15.txt -D backup

sleep 2s

kill -s SIGHUP $pid #Chiude il server mandando il segnale SIGHUP
wait $pid
echo "Fine Test 2";