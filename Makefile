all: suspend-time

suspend-time:
	g++ -I/opt/halon/include/ -I/usr/local/include/ -fPIC -shared suspend-time.cpp -o suspend-time.so
