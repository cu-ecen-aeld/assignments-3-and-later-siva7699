#!/bin/bash

if [ "$#" -ne 2 ]; then
	echo 'need two argument <full path to a file >, <text string>'
	exit 1
fi


writefile=$1
writestr=$2

# Create the directory path if it doesn't exist
mkdir -p "$(dirname "$writefile")"

# Write the content to the file, overwriting if it exists
echo "$writestr" > "$writefile"

# Check if the file was created successfully
if [ $? -ne 0 ]; then
    echo "Error: Could not create file $writefile"
    exit 1
fi

echo "File $writefile created successfully with content: $writestr"
