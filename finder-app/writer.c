#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>

int main(int argc, char *argv[]) {
    //set it up
    openlog("aesd-writer", LOG_PID, LOG_USER);

    //check num of para(3)
    if (argc < 3) {
        syslog(LOG_ERR, "Error: Invalid number of arguments. Usage: %s <file> <string>", argv[0]);
        fprintf(stderr, "Error: Missing arguments.\n");
        closelog();
        return 1;
    }

    char *writefile = argv[1];
    char *writestr = argv[2];

    //record writing
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    
    FILE *file = fopen(writefile, "w");//w for cover
    if (file == NULL) {
        syslog(LOG_ERR, "Error: Cannot open or create file %s", writefile);
        perror("File open error");
        closelog();
        return 1;
    }

    //write and check
    if (fprintf(file, "%s", writestr) < 0) {
        syslog(LOG_ERR, "Error: Failed to write to file %s", writefile);
        fclose(file);
        closelog();
        return 1;
    }

    
    fclose(file);
    closelog();
    return 0;
}
