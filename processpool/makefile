CC=g++
OBJECTS=cgi makesum
CCFLAGS= -g -O0 -Wall -std=c++11

all:$(OBJECTS)

cgi: cgi.o cgiconn.o
	$(CC) $(CCFLAGS) $^ -o $@

makesum: makesum.cc
	$(CC) $(CCFLAGS) $^ -o $@

cgi.o:cgiconn.h processpool.h
	$(CC) -g -c cgi.cc
cgiconn.o:cgiconn.h
	$(CC) -g -c cgiconn.cc

clean:
	rm *.o $(OBJECTS)