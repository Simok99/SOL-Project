#!/bin/bash
CONFIG_PATH="./config.txt"

echo "Script per il Test 3";

#Imposta il file di configurazione
echo -e "**Per numeri di default utilizzare 0, per path personalizzati rimuovere gli asterischi (Una opzione per riga, memoria in Bytes)
    **Parametri validi: numberOfFiles, memoryAllocated, numberOfThreads, socketPath, logPath
    numberOfFiles=100
    memoryAllocated=32000000
    numberOfThreads=8
    **socketPath=./mysocketpath/socket
    **logPath=./mylogpath/log.txt" > $CONFIG_PATH

SERVER_CMD=./server
CLIENT_CMD=./client


pids=()

$SERVER_CMD & #Avvia il server
pid_server=$!
sleep 4s

end=$((SECONDS+30))

#Scrive tre file nel server con un ritardo di 200ms tra una richiesta e l'altra
$CLIENT_CMD -f socket -p -t 200 -W ./testfiles/prova.txt -W ./testfiles/wow.txt,./testfiles/testsubfolder/subfile.txt

for i in {0..9}; do
    $CLIENT_CMD -f socket -r ./testfiles/wow.txt,./testfiles/prova.txt,./testfiles/file1mb.txt -t 0 &
done

sleep 30s


kill -s SIGINT $pid_server #Chiude il server mandando il segnale SIGHUP
wait $pid
echo "Fine Test 3";