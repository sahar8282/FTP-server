all: server

cmpe431:
	g++ -std=c++17 server.cpp -o server -pthread 

clean:
	rm server

test: cmpe431
	bash test.sh