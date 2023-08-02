all:
	g++ server.cpp -o server `pkg-config --cflags --libs gstreamer-1.0 gio-2.0`
	g++ client.cpp -o client `pkg-config --cflags --libs gstreamer-1.0 gio-2.0`
	g++ test_client.cpp -o test_client `pkg-config --cflags --libs gstreamer-1.0 gio-2.0`
