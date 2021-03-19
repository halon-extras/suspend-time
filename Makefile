all: suspend-time

suspend-time:
	clang++ -I/opt/halon/include/ -I/usr/local/include/ -fPIC -shared suspend-time.cpp -o suspend-time.so
