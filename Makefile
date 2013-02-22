all: proxy

proxy: proxy.c
	gcc -o proxy -pthread proxy.c -lpthread

clean:
	rm -rf proxy
