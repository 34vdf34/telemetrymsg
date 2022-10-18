CFLAGS = -Wall
INCLUDE = -I./include

all: telemetryclient.o telemetryserver.o log.o ini.o binn.o
	$(CC) telemetryclient.o log.o ini.o binn.o -o telemetryclient
	$(CC) telemetryserver.o log.o ini.o binn.o -o telemetryserver

telemetryclient.o: telemetryclient.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $?

telemetryserver.o: telemetryserver.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $?

log.o: log.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $?

ini.o: ini.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $?

binn.o: binn.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $?

clean:
		rm -rf *.o telemetryserver telemetryclient
		
