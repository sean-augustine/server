CC=g++
OBJECTS=threadpoolserv
CCFLAGS= -g -Wall -std=c++11 -pthread

all:$(OBJECTS)

$(OBJECTS):httpserv.cc http_conn.cc
	$(CC) $(CCFLAGS) $^ -o $@

clean:
	rm $(OBJECTS)