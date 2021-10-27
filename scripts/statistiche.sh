if [ -f "log.txt" ]; then
    while read line; do
        echo $line
    done < "log.txt"
else
    echo "File log mancante!"
fi