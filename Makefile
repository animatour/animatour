all:
	g++ server.cpp -o animatrip-server `pkg-config --cflags --libs gstreamer-1.0 gio-2.0`
	g++ client.cpp -o animatrip-client `pkg-config --cflags --libs gstreamer-1.0 gio-2.0`
clean:
	rm -f animatrip-server
	rm -f animatrip-client
