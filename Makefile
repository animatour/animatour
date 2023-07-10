all:
	g++ server.cpp -o server `pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0`
	g++ client.cpp -o client `pkg-config --cflags --libs gstreamer-1.0 gio-2.0`
