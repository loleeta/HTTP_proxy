all: proxy

 proxy: proxy.cpp
	g++ -lpthread -std=c++11 proxy.cpp -o proxy
