#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>

int main(int argc, char *argv[]) {
    // Check if the correct number of arguments are provided
    if (argc != 3) {
        fprintf(stderr, "Error: Two arguments required. Usage: %s <writefile> <writestr>\n", argv[0]);
        syslog(LOG_ERR, "Error: Two arguments required. Usage: %s <writefile> <writestr>", argv[0]);
        return 1;
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];

    // Open the file for writing
    FILE *file = fopen(writefile, "w");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open file %s for writing\n", writefile);
        syslog(LOG_ERR, "Error: Could not open file %s for writing", writefile);
        return 1;
    }

    // Write the string to the file
    fprintf(file, "%s", writestr);
    fclose(file);

    // Setup syslog
    openlog("writer", LOG_PID | LOG_CONS, LOG_USER);
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
    closelog();

    printf("File %s created successfully with content: %s\n", writefile, writestr);
    return 0;
}
