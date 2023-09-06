all:
	g++ server.cpp -o animatour-server `pkg-config --cflags --libs gstreamer-1.0 gio-2.0`
	g++ client.cpp -o animatour-client `pkg-config --cflags --libs gstreamer-1.0 gio-2.0`
clean:
	rm -f animatour-server
	rm -f animatour-client
